#!/usr/bin/env python3
"""Generate public vk* trampolines that vkQuake calls DIRECTLY as link symbols.

vkQuake includes <vulkan/vulkan_core.h> WITHOUT VK_NO_PROTOTYPES, so it calls core
Vulkan commands (vkCreateImage, vkCmdPipelineBarrier, vkQueueSubmit, ...) as plain
prototyped symbols. A Mesa ICD (libv3dv-phoenix.a) does NOT export those public
symbols — it only exports the generated dispatch entrypoints (v3dv_* / vk_common_*)
plus the single bootstrap vkGetInstanceProcAddr. So every directly-called core vk*
function is an UNDEFINED symbol at link time unless we provide a public trampoline.

This generator parses the Vulkan command prototypes out of vulkan_core.h and emits,
for each command vkQuake calls directly, a public definition that lazily resolves the
real entrypoint through the ICD dispatch (vkGetInstanceProcAddr for instance-level
commands, vkGetDeviceProcAddr for device-level commands) and forwards the args.

The set of commands to emit = the directly-called set discovered from the vkQuake
source (passed in as a file, one name per line) intersected with the prototypes the
header declares. Extension functions vkQuake loads via fp* pointers are NOT emitted
(they are never link symbols).

Output: tools/vkquake-port/vk_trampolines.c  (a generated .c, committed for reuse).

Usage: python3 gen-vk-trampolines.py <called-syms.txt> <vulkan_core.h> <out.c>
"""
import re, sys

# Device-level commands take VkDevice/VkQueue/VkCommandBuffer as first arg and must be
# resolved via vkGetDeviceProcAddr. Everything else (instance/physical-device/global)
# resolves via vkGetInstanceProcAddr. We classify by the first parameter type.
DEVICE_FIRST_ARGS = {"VkDevice", "VkQueue", "VkCommandBuffer"}
# Global commands that must resolve with a NULL instance (pre-instance dispatch).
GLOBAL_CMDS = {"vkCreateInstance", "vkEnumerateInstanceExtensionProperties",
               "vkEnumerateInstanceLayerProperties", "vkEnumerateInstanceVersion"}


def parse_prototypes(header_text):
    """Return {name: (ret, [(type, pname), ...], first_arg_type)} for VKAPI_PTR-free
    VKAPI_CALL prototypes, i.e. lines like:
        VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
            VkDevice device, const VkImageCreateInfo* pCreateInfo, ...);
    The declarations span multiple lines up to the closing ');'."""
    protos = {}
    # Join the header into one string, then regex each VKAPI_ATTR ... VKAPI_CALL ... );
    decl_re = re.compile(
        r"VKAPI_ATTR\s+(?P<ret>[\w*\s]+?)\s+VKAPI_CALL\s+(?P<name>vk\w+)\s*\((?P<args>.*?)\);",
        re.DOTALL)
    for m in decl_re.finditer(header_text):
        ret = m.group("ret").strip()
        name = m.group("name")
        args_raw = m.group("args").strip()
        if args_raw == "void" or args_raw == "":
            params = []
        else:
            params = []
            for a in split_args(args_raw):
                t, p = split_type_name(a)
                params.append((t, p))
        first = params[0][0] if params else ""
        protos[name] = (ret, params, first)
    return protos


def split_args(s):
    """Split a parameter list on top-level commas (none nest in vulkan_core protos)."""
    return [a.strip() for a in s.split(",") if a.strip()]


def split_type_name(decl):
    """'const VkImageCreateInfo* pCreateInfo' -> ('const VkImageCreateInfo*', 'pCreateInfo').
    Handles array params like 'uint32_t foo[2]' minimally (rare in the called set)."""
    decl = decl.strip()
    # the parameter name is the trailing identifier (after last space, minus *).
    m = re.match(r"(.*?)(\b\w+)\s*((\[\d*\])*)$", decl)
    if not m:
        return decl, "arg"
    typ = (m.group(1) + (m.group(3) or "")).strip()
    name = m.group(2)
    return typ, name


def base_first_arg_type(t):
    """Strip const/pointer noise; the dispatchable-handle first arg is a plain type."""
    t = t.replace("const", "").replace("*", "").strip()
    return t


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    called = [l.strip() for l in open(sys.argv[1]) if l.strip() and l.startswith("vk")]
    header = open(sys.argv[2]).read()
    out_path = sys.argv[3]
    protos = parse_prototypes(header)

    emitted, skipped = [], []
    body = []
    for name in sorted(set(called)):
        # vkGetInstanceProcAddr is provided by vk_icd_link.c (the bootstrap); never trampoline it.
        if name == "vkGetInstanceProcAddr":
            skipped.append((name, "bootstrap (vk_icd_link.c)"))
            continue
        if name not in protos:
            skipped.append((name, "no prototype in header (extension/typo?)"))
            continue
        ret, params, first = protos[name]
        first_base = base_first_arg_type(first)
        pfn = f"PFN_{name}"
        decl_params = ", ".join(f"{t} {p}" for t, p in params) or "void"
        call_args = ", ".join(p for _, p in params)
        is_void = (ret == "void")
        # Resolver choice is LOAD-BEARING and HW-proven by v3dv_harness.c:
        #   - global commands (pre-instance): vkGetInstanceProcAddr(NULL, ...)
        #   - device-level commands (first arg VkDevice/VkQueue/VkCommandBuffer): MUST use
        #     vkGetDeviceProcAddr(g_vk_device, ...). The harness proved that resolving a
        #     device command via vkGetInstanceProcAddr returns NULL in a loader-less ICD
        #     (Mesa's instance dispatch does NOT cover device entrypoints) -> calling it is
        #     a pc=0 instruction-abort. So device commands route through g_vk_device.
        #   - instance/physical-device commands: vkGetInstanceProcAddr(g_vk_instance, ...).
        if name in GLOBAL_CMDS:
            resolver = f'(PFN_{name})vkGetInstanceProcAddr(NULL, "{name}")'
        elif first_base in DEVICE_FIRST_ARGS:
            resolver = f'(PFN_{name})g_vkGetDeviceProcAddr(g_vk_device, "{name}")'
        else:
            resolver = f'(PFN_{name})vkGetInstanceProcAddr(g_vk_instance, "{name}")'

        body.append(f"""{ret} {name}({decl_params})
{{
\tstatic {pfn} fp = NULL;
\tif (!fp) fp = {resolver};
\t{'' if is_void else 'return '}fp({call_args});
}}
""")
        emitted.append(name)

    # NOTE: NOT an f-string — the C function body braces below would collide with f-string
    # field syntax. The only interpolation needed (the emitted count) is spliced in.
    header_comment = ("/*\n"
" * vk_trampolines.c — GENERATED by gen-vk-trampolines.py. DO NOT EDIT BY HAND.\n"
" *\n"
" * Public vk* command symbols that vkQuake calls directly (it includes vulkan_core.h\n"
" * without VK_NO_PROTOTYPES). A Mesa ICD does not export these; each forwards through\n"
" * the ICD dispatch resolved via vkGetInstanceProcAddr (aliased to v3dv_GetInstanceProcAddr\n"
" * in vk_icd_link.c). Global cmds: NULL instance. Instance/phys-dev cmds: g_vk_instance.\n"
" * Device cmds (VkDevice/VkQueue/VkCommandBuffer first arg): MUST resolve via\n"
" * vkGetDeviceProcAddr against g_vk_device — instance-proc-addr returns NULL for device\n"
" * commands in a loader-less ICD (proven by v3dv_harness.c; calling NULL = pc=0 abort).\n"
" *\n"
f" * {len(emitted)} commands trampolined. Regenerate when the directly-called set changes.\n"
" */\n"
"#include <vulkan/vulkan_core.h>\n"
"\n"
"PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName);\n"
"\n"
"/* Published by the Vulkan vid shim (pl_phoenix_vk_vid.c) right after vkCreateInstance /\n"
" * vkCreateDevice so the trampolines can resolve their real entrypoints. */\n"
"VkInstance g_vk_instance = VK_NULL_HANDLE;\n"
"VkDevice   g_vk_device = VK_NULL_HANDLE;\n"
"\n"
"/* Lazily resolved vkGetDeviceProcAddr (itself fetched via instance-proc-addr). */\n"
"static PFN_vkVoidFunction g_vkGetDeviceProcAddr(VkDevice dev, const char *name)\n"
"{\n"
"\tstatic PFN_vkGetDeviceProcAddr fp = NULL;\n"
"\tif (!fp)\n"
"\t\tfp = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(g_vk_instance, \"vkGetDeviceProcAddr\");\n"
"\treturn fp(dev, name);\n"
"}\n"
"\n")
    open(out_path, "w").write(header_comment + "\n".join(body))
    print(f"[gen-trampolines] emitted {len(emitted)} -> {out_path}")
    if skipped:
        print(f"[gen-trampolines] skipped {len(skipped)}:")
        for n, why in skipped:
            print(f"    {n}: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
