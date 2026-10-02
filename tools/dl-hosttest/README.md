# dl-hosttest: libphoenix's dlopen() on the build host

This runs libphoenix's `dl/dl.c` and the `libc/dlopen` Unity tests from phoenix-rtos-tests on
the build host, under `qemu-aarch64` in user mode. No Pi and no image build are needed, and one
run takes about a second.

```
make -C tools/dl-hosttest check          # the dl.c in sources/libphoenix passes every test
make -C tools/dl-hosttest check-old      # dl.c at OLD_REF must FAIL them
make -C tools/dl-hosttest check-bundle BUNDLE=<out>/libWPEInjectedBundle.so \
    [EXTENSION=<out>/phx-probe-extension.so]
```

`LIBPHOENIX=` and `TESTS=` point at other trees, such as a branch worktree. `OUT=` sets the
build directory (default `out/`). `LD_DEBUG=1` in the environment prints the loader's trace;
qemu's own dynamic loader warns about the value, which is harmless.

## How it works

Everything is compiled by the Phoenix toolchain against libphoenix's headers, with the target's
code generation. Every ELF that dl.c sees is therefore what it sees on the Pi:
- the program is static and linked `-Wl,--no-dynamic-linker -Wl,--dynamic-list=exports.list`,
  then stripped, as `test-libc-dlopen` is installed;
- the plugins are linked as `libc/dlopen/Makefile` links them.

Only the operating system underneath changes:
- `shim.c` implements, on Linux aarch64 system calls, the calls dl.c, Unity and the tests make:
  - `open`/`read`/`lseek`/`close`/`mmap`/`munmap`, translating Phoenix's `O_RDONLY`,
    `MAP_ANONYMOUS` and `MAP_FIXED` values;
  - a bump allocator;
  - a small `printf` family;
  - `getenv`;
  - `_start`, which sets `argv_progname`, the variable `crt0` sets on Phoenix.
- The pure functions (`string.h`, `setjmp`) come from the sysroot's `libphoenix.a`. The shim is
  linked first, so libphoenix's stdio, malloc and system calls are never pulled in.

`check-old` builds the same tests against `git show $(OLD_REF):dl/dl.c` and passes only if they
fail. That proves the tests catch the old loader. `OLD_REF` defaults to `56049ae`, the last dl.c
before the export table. Against it, the stripped program fails 10 of 11 tests. Linked unstripped (`out/dltest-old.unstripped`), it still fails 4 of them:
`unlisted_not_found`, `weak_undefined_is_null`, `no_dt_hash_refused` and `tls_refused`.

`check-bundle` loads WPE's injected bundle the way the WebProcess does: `dlopen()`, then
`dlsym("WKBundleInitialize")`, then the call. It runs against a mock of the bundle's three imports
(`bundle-check.c`), exported exactly as `wpe-browser` exports them. With `EXTENSION=`, the mock
`WebProcessExtensionManager::initialize` then loads the extension and calls its
`webkit_web_process_extension_initialize_with_user_data`, as the real one does.

## Limits

- This is Linux user mode, not Phoenix:
  - the kernel's mmap/W^X policy is not exercised;
  - neither is the NFS file system.
- The tests on the Pi (`test-libc-dlopen`) remain the authority.
- The shim implements only what these programs call. A new libc call in dl.c shows up as a
  multiple-definition or undefined-symbol link error naming the function to add.
