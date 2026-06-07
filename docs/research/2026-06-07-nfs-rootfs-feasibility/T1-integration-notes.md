# T1 — nfs-smoke integration into the rpi4b netboot image

**Status:** DONE (host-only build; NO hardware). `nfs-smoke` builds, links the
`libnfs` port, is bundled into the syspage program set (loader.disk), and is
launched at boot via `user.plo.yaml`. The HW network test is run separately by
the orchestrator.

---

## 1. What was wired

| File | Change |
|------|--------|
| `sources/phoenix-rtos-utils/nfs-smoke/Makefile` | NEW. Util Makefile (modeled on `spitool/Makefile`). Links libnfs via `LOCAL_LDLIBS := -lnfs`; relaxes one `-Werror` diagnostic. |
| `sources/phoenix-rtos-utils/_targets/Makefile.aarch64a72-generic` | `DEFAULT_COMPONENTS := psh nfs-smoke` (registers the util in the core build). |
| `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml` | Added `- name: libnfs` so the ports stage builds + stages it. |
| `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml` | One launch line after lwip, before psh (see §4). |
| `sources/phoenix-rtos-ports/libnfs/port.def.sh` | Two fixes found at first real port build (see §3). |
| `sources/phoenix-rtos-utils/nfs-smoke/nfs-smoke.c` | Two minimal source tweaks (see §3). |

## 2. The Makefile (link recipe — simpler than T0 predicted)

T0 assumed the versioned-ports link recipe (`-I<install>/include/nfsc -L<install>/lib -lnfs`).
In practice libnfs declares **no `conflicts`**, so the port harness
(`port_manager` `InstallableCandidate.install_path`) installs it as a *normal
lib* into the **flat** sysroot — `${PREFIX_BUILD}/lib/libnfs.a` and
`${PREFIX_BUILD}/include/nfsc/`. Those dirs are already on every component's
global flags (`Makefile.common`: `LDFLAGS += -L$(PREFIX_A)`,
`CFLAGS += -I$(PREFIX_H)`), so the util needs only:

```make
NAME := nfs-smoke
SRCS := $(wildcard $(LOCAL_PATH)*.c)
LOCAL_INSTALL_PATH := /bin
LOCAL_LDLIBS := -lnfs
LOCAL_CFLAGS := -Wno-error=stringop-truncation
include $(binary.mk)
```

No `LIBS := libnfs` / `LOCAL_PORTS_VERSIONS` needed — those are for *versioned*
(conflictable) ports installed under `versioned-ports/<name>-<ver>/`, which is
NOT where a no-conflicts port lands. (`#include <nfsc/libnfs.h>` resolves via
the flat `-I$(PREFIX_H)`.)

## 3. Bugs fixed at first real port build

The T0 notes were validated with a standalone `make`, not through the in-tree
port harness, so three real-harness gaps surfaced:

1. **`port.def.sh` size pin** — the two pinned metrics measure **different file
   sets**: `sha256` is `git archive HEAD | sha256sum` (honors `.gitattributes`
   export-ignore and excludes submodule contents), while `size` is
   `find -type f ! -path '*/.git/*' -printf '%s'` over the raw working tree
   (includes export-ignored files and any `git submodule update --init`
   checkouts). So the recomputed `size` (this host: `2112263` vs the pinned
   `2112336`, 73-byte delta) can legitimately differ even when the cryptographic
   `sha256` **matches exactly** — which it did, so the content is correctly
   pinned and only the redundant size guard tripped. Repinned `size="2112263"`.
   (This reproduces: the post-repin ports run passed the size gate and compiled.
   The size-vs-sha256 file-set mismatch is a pre-existing property of the guard's
   design, so the pinned `size` is inherently more host/clone-sensitive than the
   `sha256`; that fragility is not introduced here.)
2. **`port.def.sh` CROSS** — it passed `CROSS="${HOST}-"`, but `HOST` is the
   autotools triple `${TARGET_FAMILY}-phoenix` = `aarch64a72-phoenix`
   (`port_internal.subr`), which is NOT a real tool prefix. The installed
   toolchain is `aarch64-phoenix-`, exported by the build system as `$CROSS`.
   Changed to `CROSS="${CROSS}"` (the same var busybox's port uses).
3. **nfs-smoke.c** — two minimal tweaks:
   - `NFS_V3`/`NFS_V4` are defined only in libnfs's *internal* RPC headers
     (`nfs/libnfs-raw-nfs.h`, `nfs4/...-nfs4.h`), which the port does not
     install into the public `<nfsc/>` set. Added `#ifndef`-guarded fallbacks
     `NFS_V3 3` / `NFS_V4 4` (the stable wire program-version numbers that
     `nfs_set_version(nfs, int)` expects).
   - The util build is `-Werror`; `-Wstringop-truncation` flagged the
     intentional fixed-width `strncpy` in the `/dev/ifstatus` parser. Handled in
     the Makefile (`LOCAL_CFLAGS`), not by rewriting the source.

## 4. Boot launch line (user.plo.yaml)

Inserted after the lwip launch, before psh:

```yaml
  - app {{ env.BOOT_DEVICE }} -x lwip;genet:0xFD580000:189:190:PHY:bcm54213pe:0.1:irq:MAC ddr ddr
  # NFS client smoke test (#153): waits for a DHCP lease then mounts an NFS
  # export and reads/writes a file via the libnfs port. argv: server export file.
  - app {{ env.BOOT_DEVICE }} -x nfs-smoke;10.42.0.1;/;/etc/hostname ddr ddr
  - app {{ env.BOOT_DEVICE }} -x psh ddr ddr
```

`image_builder.py` resolves `app -x nfs-smoke` → `PREFIX_PROG_STRIPPED/nfs-smoke`
and embeds the binary into the kernel partition of loader.disk.

## 5. Build invocation used (build-order wrinkle solved)

`build.sh` always runs **core before ports** (regardless of argv order — it sets
flags then runs stages in a fixed sequence). So a single `--with-ports` pass
fails: nfs-smoke (core) would link `libnfs.a` before the ports stage stages it.
`prepare-buildroot.sh` excludes `_build`/`_boot` from its `--delete`, so a prior
ports build's staged artifacts survive across prepare runs. Hence a **two-pass**
build (nfs-smoke registered the whole time):

```sh
# Pass 1 — stage libnfs.a + headers (ports stage only):
./scripts/prepare-buildroot.sh --copy-components .buildroot
PATH=.venv/bin:.toolchain/aarch64-phoenix/bin:$PATH \
  env RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb RPI4B_VARIANT=netboot \
      TARGET=aarch64a72-generic-rpi4b \
  ./phoenix-rtos-build/build.sh ports      # (run inside .buildroot)

# Pass 2 — build nfs-smoke (links staged libnfs.a) + project + image:
./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot
```

The pass-1 ports build is needed only once (or after a libnfs/port change);
later core iterations reuse the staged `libnfs.a`.

## 6. Verification (host-only)

```
# staged port artifacts (flat sysroot):
.buildroot/_build/aarch64a72-generic-rpi4b/lib/libnfs.a
.buildroot/_build/aarch64a72-generic-rpi4b/include/nfsc/libnfs.h

# linked + stripped util:
.buildroot/_build/aarch64a72-generic-rpi4b/prog.stripped/nfs-smoke   (228608 bytes)

# bundled into loader.disk (image_builder log): nfs-smoke (offs=0x183000, size=0x37d00)

# task's canonical check:
strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c nfs-smoke
-> 5

# launch line embedded:
strings .../loader.disk | grep 'nfs-smoke;10.42'
-> app ram0 -x nfs-smoke;10.42.0.1;/;/etc/hostname ddr ddr
```

Full image built + exported: `artifacts/rpi4b/rpi4b-sd.img` (SHA256
`56b142a7a3b01bc9a7754a216a1922937f560de62373272ae5e50f60f2626b26`).

## 7. Not done here (orchestrator / later)

- The actual NFS-over-network HW test (DHCP lease → mount → read/write).
- Sibling/coord-repo commits are made; no upstream push (per project policy).
