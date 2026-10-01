# Self-hosting Phoenix-RTOS on the Pi 4: what it would take

**Date:** 2026-10-01. **Status:** research only. No code changed anywhere.
**Owner question:** can the Pi 4 become a Phoenix-RTOS development station? That means booting
Phoenix, fetching our fork's sources inside it, building the system or parts of it natively with a
C/C++ compiler, installing the result, rebooting when the kernel or loader changed, and testing.
Slow builds are acceptable.

**Short answer:** yes for the **core** (kernel, libphoenix, plo, drivers, filesystems, utils), and
it is cheaper than it sounds:

- The core is **100 % C plus assembly**. Not one C++ file is in the 11 core repos [V].
- The core compiles in **57 s of serial CPU time on this host** [V, measured; see §6].
- The rootfs already ships bash, GNU sed/grep/tar/gzip/xz, coreutils, curl with HTTPS, and
  Python 3.14 [V].

What is missing is the compiler itself, plus a short list of OS gaps. The compiler is GCC 16.2 built
**on the host** as a Canadian cross (build = x86_64-linux, host = target = aarch64-phoenix). Nobody
should build it on the Pi. The OS gaps are: **no software reboot at all**, `O_EXCL` ignored,
non-atomic `rename`, a read-only FAT driver, 1-second mtimes, and no executable page cache across
exec.

| Milestone | Estimate | Wall time on the Pi |
|---|---|---|
| First demo (edit a psh applet, `make` it with native gcc, run it) | **~1.5 agent-weeks** | — |
| Rebuild the kernel, libphoenix and a driver natively, install, reboot with rollback | **+2–3 agent-weeks** | — |
| Fully self-hosted core with git | **+2–3 agent-weeks** | core build ≈ 5–15 min at `-j4` with warm binaries, ≈ 15–40 min cold [E] |

Ports (Mesa, GTK, STK, Python) are a different scale. Building them on the Pi stays out of scope;
see §7, stage 4.

Legend: **[V]** = verified in our tree or measured today. **[R]** = recalled, not checked.
**[E]** = estimate derived from [V] numbers (the method is shown).

---

## 1. What the build needs from the host today

The full census is from a read-only survey of `rebuild-rpi4b-fast.sh`, `phoenix-rtos-build`,
`port_manager` and the image scripts [V]. "On Phoenix" is checked against
`.buildroot/_fs/aarch64a72-generic-rpi4b/root` [V]. Every one of the 371 ELFs there is **static**.

| Need | Used for | Core? | On Phoenix today |
|---|---|---|---|
| bash ≥4, GNU make ≥3.82 (`undefine`) | build.sh, makes/*.mk | yes | bash ✅, **make ❌** |
| GNU sed, awk, grep, coreutils (`stat -c`, `realpath`, `install`, `mktemp`, `truncate`) | Makefiles; the errno table in `libphoenix/string/Makefile:22` (awk) | yes | sed/grep/coreutils 9.5 ✅. awk, find, diff, cmp, xargs and bzip2 are **busybox applets** (411 312-byte binary). gawk ❌ |
| aarch64-phoenix gcc/g++/as/ld/ar/objcopy/readelf/strip | everything | yes | **❌**: the whole problem, §2 |
| python3 ≥3.10 + **PyYAML + jinja2** | `phoenix-rtos-build/scripts/image_builder.py` renders the plo scripts and builds `loader.disk` | **yes** | python 3.14 ✅ (67 MB static); yaml/jinja2 not installed (both have pure-Python fallbacks [R]) |
| `fdtput` | every `project` stage (`RPI4B_QEMU_MEMORY_SIZE`, `build.project:79-84`) | yes, for QEMU only | ❌. Skip it on a native build |
| mtools (`mformat`/`mcopy`), `sfdisk`, `mke2fs -d`, `e2fsck`, `debugfs` | bootfs and SD image assembly, verification | image only | ❌ (not needed to *install* in place, §5) |
| git | version strings (with fallbacks), `gen-build-versions.sh`, git-sourced ports | soft | ❌ (§4) |
| rsync, `shasum` (**a Perl script**), DejaVu fonts, `strings` | `prepare-buildroot.sh`, checksums, font staging | wrapper | ❌. A native driver script should not use them |
| `host` stage: phoenix-rtos-hostutils (metaelf, syspagen, psu, phoenixd, psdisk, mkrofs, mcxisp) | **nothing in the rpi4b build uses them** [V] (the comment at `rebuild-rpi4b-fast.sh:359` is wrong) | no | drop it |
| port_manager (Python + resolvelib, rich, packaging, pyparsing), jq, wget, patch | ports stage. libnfs is a port, so the default *nfsroot* variant needs it | ports | python/jq/wget/curl ✅, patch ❌ |
| perl, bison, gperf, meson ≥1.4 + ninja, cmake, pkg-config, wayland-scanner 1.24, glib codegen, glslang, matching host python 3.14, `CC_FOR_BUILD` | 73 ports: 32 autoconf, 22 make, 8 meson, 7 cmake, 4 mixed | ports | ❌. **Perl is the wall** (openssl `Configure`, python, xorg_fonts, xkeyboard_config) |

**Consequence:** a native core build should not run `rebuild-rpi4b-fast.sh` (Docker-, rsync-, perl-
and font-bound). It should run a small **native build driver** that calls the repos' own Makefiles
with `TARGET=aarch64a72-generic-rpi4b` and then `image_builder.py` for `loader.disk`.

---

## 2. The native toolchain

**The current cross toolchain** [V, `phoenix-rtos-build/toolchain/build-toolchain.sh`]:
- GCC 16.2.0 and binutils 2.47, with gmp 6.3.0, mpfr 4.2.2, mpc 1.3.1 and isl 0.24 built in-tree.
- Configured with `--enable-languages=c,c++ --with-newlib --enable-threads=posix`.
- The libc step is built with the stage-1 gcc; libstdc++ is configured separately with `--disable-shared`.
- binutils has `--enable-lto`, so `lto1` and `liblto_plugin.so` are installed.
- Sizes: build tree **6.7 GB**; installed tree **294 MB**. `cc1` is 44.7 MB and `cc1plus` 47.5 MB.

**Recommendation: a Canadian cross on the host.** Configure binutils and GCC with
`--build=x86_64-linux-gnu --host=aarch64-phoenix --target=aarch64-phoenix`, link them static against
libphoenix and our libstdc++, and stage them into the rootfs with a native sysroot (`/usr/include`,
`/usr/lib`: libphoenix.a, crt0, libgcc, libstdc++). The native tree is ≈150–250 MB [E].

What GCC needs as a *Phoenix program*:

- **Spawning cc1/as/collect2/ld.** libiberty `pex-unix.c` uses `posix_spawn` when present, else
  vfork/fork + exec [V, gcc-16.2 source].
  - libphoenix has **no posix_spawn** [V] but does have `vfork` (`include/unistd.h:183`), which is
    proven over ~4200 launches [V]. So pex takes the vfork path.
- **Process accounting and limits.** `getrusage` is a stub returning zeros, and
  `getrlimit`/`setrlimit` are stubs [V, `posix/stubs.c:125-166`]. Harmless.
- **Memory.** ggc uses `mmap(MAP_PRIVATE|MAP_ANONYMOUS)`, which works.
  - There is no swap [V], and the Pi has 4 GB.
  - C core TUs are small. cc1plus on a big C++ file can take ~1 GB [R], so do not run `-j4` on
    Mesa-class C++.
- **LTO plugin.** It needs `dlopen` inside `ld`. libphoenix's Phase-A dlopen resolves against the
  host's symtab, which is fragile here. Configure with `--disable-lto --disable-plugins`. The core
  does not use LTO [V: no `-flto` in `phoenix-rtos-build` makes or targets].
- **Driver details.** GCC 16 is written in C++, so cc1 links libstdc++. That already works for STK,
  Mesa and Poppler [V].
  - Use temp files on a RAM `/tmp` rather than `-pipe`. Pipes are posixsrv-served with a 4 KiB
    buffer (`posixsrv_private.h:24`) and posixsrv has not opted out of the 20 ms poll quantum (P9) [V].
- **Configure for a phoenix *host*.** `config.sub` already knows `*-phoenix` (the target patch
  exists). Host-side libiberty, libcpp and gmp may need cache variables and `--disable-assembly`
  for gmp [R]. Expect 2–4 rounds of "configure assumed X, libphoenix lacks X".

**Building the toolchain on the Pi is not sensible.** This is an [E] from three inputs:
- The host takes 9–60 min with all cores (`docs/BUILD.md:97`) [V].
- A72 cores are 6–12× slower per core than this host [R].
- configure runs thousands of serial fork+exec checks.

The result is ≈4–8 h per non-bootstrap pass in 6.7 GB of scratch, and ×3 for a bootstrap. Treat it
as an optional stunt to prove the toolchain can reproduce itself, never part of the dev loop.
Ship the toolchain prebuilt from the host.

**Alternatives, dismissed.**
- **TCC** has an arm64 backend but no arm64 inline asm or `.S` assembler [R]. The kernel has 45 `.S`
  files, libphoenix 29 and plo 41 [V], plus GCC attributes everywhere. Useful at most for a toy demo.
- **clang/LLVM** has no Phoenix target, and its build is several times larger than GCC's [R].
- **GCC is the only real option.** Our port is already GCC-specific (specs patch, libstdc++
  feature patch), and a Canadian cross reuses all of it.

**C vs C++:** every core repo (kernel, libphoenix, devices, filesystems, utils, corelibs, lwip, plo,
usb, posixsrv, hostutils) has **0** `.cpp`/`.cc` files [V]. Only the ports need C++: STK, Mesa,
Dillo/FLTK, Poppler, Quake ports, and libstdc++ consumers. So C-only GCC is enough for stages 1–3.

---

## 3. Missing POSIX/runtime pieces for gcc, make and git

From a read-only survey of libphoenix, the kernel posix layer and the filesystems. The rows marked ★
were re-checked by hand.

| Gap | Status | Who it hurts | Fix size [E] |
|---|---|---|---|
| ★ **No software reboot** | `hal_cpuReboot` halts in place (`kernel hal/aarch64/generic/generic.c:141-149`) [V]. The PM-watchdog reset path lived only in diag-udp and was removed (`pi4-hardware-support-matrix.md:75`) [V] | install-and-reboot (§5) | 1–2 d (PM_RSTS/PM_WDOG encoding in `external/linux/drivers/watchdog/bcm2835_wdt.c:24-35,101-112` [V]) |
| ★ **`O_EXCL` ignored** | the kernel `posix_open` has `/* TODO: handle O_CREAT and O_EXCL */` and opens an existing file (`posix/posix.c:861, 930-947`) [V]. `mkstemp` inherits this | git (`index.lock`, `refs/*.lock`), make temp files, gcc temp names | 0.5–1 d |
| ★ **`rename` = link + unlink**, not atomic, no directories | `libphoenix/sys/stat.c:284-311` [V]. The NFS client has no rename op at all | git refs, index and objects; "install by rename" | 3–5 d (a real `mtRename` in ext2, nfs, dummyfs and libphoenix) |
| **1-second mtimes** | `st_mtim.tv_nsec` is always 0. The IPC attribute is seconds; ext2 stores `uint32_t mtime` [V, survey] | make misses same-second edits; git rechecks racy entries | live with it (or 3–5 d for ns attrs plus ext2 extra fields) |
| ★ **No executable page cache across exec** | `vm_objectPut` frees every page when the last ref drops (`vm/object.c:168-205`) [V]. Each new cc1 re-faults its text through 16-page (64 KiB) read-ahead (`object.c:211-219`) | **compile speed**, see §6 | 1 d userspace "pin" process that keeps cc1/as/ld mapped (the object tree is keyed by oid, `object.c:85-134`), or 2–3 d kernel LRU retention |
| fork+exec freeze (C3) | real `fork()`+exec froze the system 3/3 runs until kernel build 18, then 0/3 (1500 launches) [V, `docs/misc/2026-09-27-c9-ntpclient-faults.md:482-494`]. n = 3 per arm | make, git (no posix_spawn, so fork is used) | gate: run `spawn-storm -f` plus a `make -j4` soak |
| posix_spawn, pipe2, wait3/wait4, setitimer, `O_NOFOLLOW`/`O_DIRECTORY`, `utimensat` | missing [V, survey] | make (configure it to use fork/waitpid), git (`NO_SETITIMER`) | configure flags; ~1 d to add posix_spawn over vfork |
| SA_RESTART | not implemented, so calls return EINTR (`kernel include/signal.h:76-82`) [V, survey] | make under SIGCHLD | retry loops; watch for it |
| shebang args dropped | `#!/bin/sh -e` loses `-e`; `#!/usr/bin/env bash` breaks (`unistd/sys.c:210`) [V, survey] | build scripts, git hooks | 0.5 d |
| fsync | on the NFS root it is a silent no-op [V, survey, by reading] | git durability | — |
| C8 | on the NFS root a just-created file intermittently returns `ENOENT` (≈1 in 90 runs, `KNOWN-ISSUES.md:28`) [V] | **builds on the NFS root** create-then-read thousands of files | build on ext2 until C8 closes |
| P21 `MAP_SHARED == MAP_PRIVATE` | [V] | **not** git: packfiles are mapped read-only, which works with 16-page read-ahead | — |
| P24 mutex = syscall | 654 ns per malloc+free pair single-threaded, 9.1 µs multithreaded [V] | cc1, as, ld and make are single-threaded; only git `index-pack` threads | — |
| P25 lstat = 2 messages per component | ≈0.3 ms per depth-4 stat locally; an RPC per lookup on NFS (≈1.37 ms) [V] | header search (50–100 headers per TU [V, `.d` files] × several `-I` dirs), make stat sweeps, `git status` | — |
| present and fine | fork (COW), vfork, pipe, dup2, poll/select, waitpid, sigaction + SIGCHLD, fnmatch/glob/regex/wordexp, getopt_long, hard links and symlinks (ext2, NFS), real `fcntl` locks, 64-bit `off_t`, read-only file mmap, `getpwnam`, sysconf (4 CPUs), 1024 fds [V, survey] | — | — |

**Storage capacity:**
- **Sizes on this host** [V]:
  - `sources/` 1.4 GB, of which the core repos are ≈190 MB (ports 629 MB, project 342 MB, doc 110 MB).
  - Core build outputs ≈120 MB (kernel 6.7, libphoenix 12, lwip 20, sysroot 28, include 20 MB …).
  - Ports alone: port-sources **18 GB** plus versioned-ports **8.6 GB**.
- **A core dev station needs ≈1 GB** (toolchain + core sources with `.git` + build tree) [E]. Ports
  need >30 GB.
- **The SD root has 1 GiB of headroom by default** (`RPI4B_ROOTFS_FREE_MIB`,
  `build-rpi4b-rootfs-ext2.sh:83-88`), and Phoenix cannot grow the filesystem itself [V]. That is
  too tight.
  - The card is **64 GB** [V, memory]. Build the dev image with `RPI4B_ROOTFS_FREE_MIB=8192`, or
    add an ext2 p3 cut on the host.
  - ext2 files are likely capped at 4 GiB, since `sizeHi` is never used [V, survey], which is fine.
- **Speed of each build disk** [V]:

  | Disk | Read | Write |
  |---|---|---|
  | SD | 38 MB/s | 12.7 MB/s |
  | USB stick (umass), ext2 | 42–49 MB/s | 9.5–14 MB/s (59 MB/s raw) |
  | NFS | 25–30 MB/s bulk, but ≈1.4 ms per RPC | — |

  For a metadata-heavy compile, **local ext2 (SD p2/p3 or a USB stick) beats NFS**. NFS is the
  convenience lane, since the host sees the tree.
- **libext2 had 21 silent defects** [V, memory]. Run a build-shaped workload (create, write,
  rename, unlink, many small files) through `tools/libext2-hosttest` before trusting a self-hosted
  tree on it.

---

## 4. git on Phoenix

**Stage-0 alternative that works today, at no cost:** `curl -L
https://codeload.github.com/<org>/<repo>/tar.gz/<sha> | tar xz`. Phoenix has no shell pipes in psh,
so use bash, or `curl -o` then `tar xzf`. curl with HTTPS, tar and gzip are on the image [V]. This
fetches any commit of our fork. Getting changes back out goes as `diff -u`, then to the host via
NFS/scp (dropbear `scp` ships [V]).

**A real git 2.5x port** [R for git internals]:
- git's own Makefile cross-builds with a `config.mak`:
  - `NO_PERL NO_PYTHON NO_TCLTK NO_GETTEXT NO_SETITIMER`
  - `NO_ICONV` or our libiconv port
  - zlib ✅, curl+openssl ✅ for `git-remote-https`, expat optional
- Runtime needs:
  - fork+exec of the remote helper over pipes, plus poll
  - read-only mmap of packs, which works (`NO_MMAP` is available as a fallback)
  - lockfiles via `O_CREAT|O_EXCL`, **broken today**
  - rename of lockfiles onto refs and the index, **non-atomic today**
  - lstat sweeps for `git status`: ≈2000 core files × ~0.3 ms ≈ <1 s on ext2, ≈10–15 s on NFS [E]
- **Effort:** port plus clone/fetch/commit/push over HTTPS ≈ **3–5 agent-days**. Add the `O_EXCL`
  fix (required) and atomic rename (strongly advised: a crash mid-rename can lose a ref) at
  **+3–5 days**.
- **libgit2** is an alternative (CMake, C, the `lg2` example). It is not easier: same OS needs, and
  less familiar UX.

---

## 5. Install and reboot on the live system

**User-space binaries:**
- **NFS root:** write into the export.
- **SD root:** write on ext2.
- **Never overwrite a running binary in place.** Executables are demand-paged from the file
  (`object.c:211-219`), so a running process would fault in pages of the *new* image. Install as
  `x.new`, then rename. Until rename is atomic and unlink-while-mapped is confirmed safe on ext2 [R],
  **install to a staging dir and apply at boot**, or simply reboot.
- Everything is static, so **a libphoenix change takes effect only after relinking every consumer**.
  For the core that is ≈60 programs, ≈1–3 min [E]. Ports need host relinks, which the existing
  staleness model already tracks.

**Kernel and loader:** `kernel8.img` (plo) and `loader.disk` (5.2 MB: kernel plus the early programs)
live on the **FAT p1** [V]. The FAT driver is **read-only**: every write path returns `EROFS`
(`filesystems/fat/libfat.c:95-121`) [V]. Partition devnodes `/dev/mmcblk0pN` exist
(`sdstorage_dev.c:418`) [V]. There are three ways to write `loader.disk`:

| Option | Work | Notes |
|---|---|---|
| (a) netboot lane: write the new `loader.disk` into the host's TFTP dir (export it over NFS) | ≈0.5 d host config | works now; not self-contained |
| (b) **port mtools** and `mcopy -o -i /dev/mmcblk0p1 loader.disk ::` | 1–2 d | plain C; no kernel FAT writes. ⚠ coreutils `dd` cannot `fstat` `/dev/mmcblk0` [V, rpi4-flash-sd skill], so check that mtools copes. **Recommended** |
| (c) add in-place overwrite (no growth) to libfat (`fatio.c` is 689 lines) | 2–4 d | general FAT write is larger still |

**Reboot and rollback:**
- **Reboot needs the missing PM-watchdog reset** (§3). Today the host's relay power-cycles the Pi.
- **Rollback can use the firmware's `tryboot`** [V]. It is a one-shot flag set over the mailbox
  (`RPI_FIRMWARE_SET_REBOOT_FLAGS 0x00038064`, value 1;
  `external/linux/drivers/firmware/raspberrypi.c:204-220`), and the Pi 4 EEPROM supports the
  `[tryboot]` conditional (`rpi-eeprom/firmware-2711/release-notes.md:432,501`).
- Proposed flow:
  1. Write `loader-new.disk` to p1, where `config.txt` `[tryboot]` names it.
  2. Set the flag (`rpi4-vcmbox` already speaks the mailbox) and reset.
  3. If the new system comes up, userspace promotes it (rename on FAT, or a `config.txt` edit).
  4. If it hangs, **any** reset boots the old `loader.disk`.
- To make that reset automatic, plo or the kernel arms the PM watchdog early on a tryboot boot, and
  userspace pets it (≈1 d on top of the reboot work).
- The netboot lane stays the last-resort fallback: dnsmasq UP = netboot [V, memory].
- **Model to copy:** the existing self-flash flow (`.claude/skills/rpi4-flash-sd`). Phoenix `dd`s a
  whole image onto its own card at 12.9 MB/s, and that is proven unattended [V].

---

## 6. Performance estimate

**Measured on this host** [V]: AMD Ryzen 7 PRO 250 (Zen 4, 8C/16T), `nproc` = 16. Each core repo's
real compile commands were taken from `make -n -B` and run **serially** to `/dev/null` under the real
flags (method in the footnote). The table also gives the Pi estimate per repo [E]:

| Repo | TUs | Host serial (s) | Pi `-j4`, warm binaries [E] | Pi `-j4`, cold every exec [E] |
|---|---|---|---|---|
| kernel | 64 | 7.5 | ≈0.5–1.5 min | ≈1.5–4 min |
| libphoenix | 137 | 12.2 | ≈1–2 min | ≈3–6 min |
| devices (rpi4b set) | 37 | 8.8 | ≈0.5–1 min | ≈1–2 min |
| filesystems | 49 | 6.2 | | |
| utils (psh …) | 52 | 4.4 | | |
| corelibs | 33 | 2.9 | | |
| lwip | 110 | 11.3 | | |
| plo | 52 | 2.9 | | |
| usb + posixsrv | 13 | 1.1 | | |
| **core total** | **547** | **57.3** | **≈5–15 min** | **≈15–40 min** |

The host serial times cover compiling only: no link, no `ar`, no make parse, no image stage.

**How the Pi columns are derived** [E]:
- **CPU:** host seconds × 8 (A72 at 1.5 GHz with `force_turbo=1` vs Zen 4 per core, 6–12× [R]),
  divided by ≈3.5 effective cores.
- **Phoenix overhead per TU:** about 4 execs (sh → gcc → cc1 → as).
  - **Warm** (binaries pinned, §3): ≈30–100 ms each. The slowest `printenv` launch in a 500-run
    storm was 29–66 ms [V].
  - **Cold** (today, pages freed at exit): cc1 re-faults its touched text, so ≈0.5–3 s per cc1.
    A 24 MB binary needed 5.5–7 s to reach `main()` after read-ahead; `python3 -V` launched in
    ≤557 ms [V].
- **Header lookups:** 50–100 headers per TU through P25 add ≈0.05–0.2 s per TU on ext2.
- **Links:** ≈60 static links at 1–3 s each [E].

**Takeaways:**
- **The exec page cache matters more than CPU.** Pinning cc1/as/ld (1 d) roughly halves to thirds
  the cold numbers.
- **One-file edit, rebuild, relink of a driver or psh: ≈10–30 s.** That is a usable inner loop.
- **Rebuilding `loader.disk`** (python image_builder) adds ≈0.5–1 min [E].
- **The toolchain itself takes ≈4–8 h per pass** (§2). Not sensible; ship it prebuilt.
- **The whole system with ports would take days to a week of Pi time** [E], plus perl, cmake and
  meson ports. Keep ports host-built.

---

## 7. Staged plan

Efforts are in agent-days and are judgement estimates [E], not anchored to a dated precedent.

| Stage | Content | Exit demo | Effort |
|---|---|---|---|
| **0. Groundwork** | PM-watchdog **reboot** (kernel hal or a small `/dev/watchdog` driver); `O_EXCL` in `posix_open`; shebang args; a curl+tar source-fetch recipe; ext2 headroom (`RPI4B_ROOTFS_FREE_MIB=8192`) | `reboot` at psh resets the Pi; `test-libc` O_EXCL test passes | **2–4 d** |
| **1. Native compiler (C)** | Canadian-cross binutils + GCC 16.2 (C, `--disable-lto --disable-plugins`, static) built by a new script next to `build-toolchain.sh`; native sysroot in the rootfs; GNU make port; PyYAML + jinja2 into site-packages; executable pin process | **"edit a psh applet in nano, `make`, run the new psh"** on Phoenix, and `gcc hello.c && ./a.out` | **5–8 d** |
| **2. Rebuild core pieces + install + reboot** | a native build driver (no rsync/docker/perl/fonts; skips the hostutils `host` stage; `fdtput` off); kernel, libphoenix and one driver natively; relink core consumers; `loader.disk` via image_builder; mtools port to write p1; tryboot + watchdog rollback | **"rebuild libphoenix and `rpi4-wifi` on the Pi, install, reboot into it, roll back a deliberately broken kernel automatically"** | **8–15 d** |
| **3. Full core, self-hosted with git** | git port; atomic `rename` (`mtRename` through ext2, nfs, dummyfs); a `make -j4` soak gate (fork storm, C3); optional nanosecond mtimes | `git clone` our fork on the Pi, build the full core, boot it, pass the smoke/showcase gate, `git push` a commit | **10–15 d** |
| 4. (optional) C++ and small ports | cc1plus + libstdc++ in the native sysroot; ninja, then perl (perl cross-build is notoriously hard [R], 1–2 weeks); natively rebuild a few plain-make/autoconf ports (sqlite3, lua, jq) | rebuild sqlite3 on the Pi | 2–4 weeks |
| — | Toolchain built on the Pi | stunt only | 4–8 h per pass + debugging |

**Totals:**
- **First impressive demo after stages 0–1: ≈1.5–2 weeks.** Reboot is not needed for stage 1, so
  the demo can come first.
- **Self-hosted core after stage 3: ≈5–8 agent-weeks.**

**Biggest risks:**
1. **The fork/exec storm** of a parallel build. The C3 freeze fix is days old, n = 3.
2. **libext2** under a create/rename-heavy workload, since it had 21 silent defects.
3. **Configure surprises** in the Canadian cross. Each one is cheap, but there will be several.
4. **Exec cold-start cost** if pinning does not work as inferred. Sharing the vm object via the oid
   tree is [V]; that warm pages actually serve `MAP_PRIVATE` text faults is [R].
5. **NFS-root C8 `ENOENT`** if someone builds on NFS.

**Overlaps with open items:**
- **P25** (header search, make, git status) and **P24** (only git index-pack) are shared with the
  browser study (`docs/research/2026-10-01-web-browser-options.md`, which also flags P24).
- **P21** is *not* a blocker here.
- **T-DYNLINK** is *not* needed: everything stays static; just disable the LTO plugin.
- **Missing posix_spawn and pipe2** also show up in the 2026-09-30 port-feature audit.
- **The reboot/watchdog work** is the hardware matrix's open "Watchdog / reboot" row and is worth
  doing for its own sake.
- **Atomic rename and `O_EXCL`** fix latent bugs for every port that uses lockfiles or
  temp-file-then-rename (sqlite, glib, Window Maker).

---

<sub>**Method for §6 host numbers** (reproducible, writes nothing): for each repo in `.buildroot/`,
`TARGET=aarch64a72-generic-rpi4b make -n -B all` with the plain `export NAME=value` lines of the
project `build.project` imported (as `scripts/syntax-check.sh` does). Keep the `aarch64-phoenix-gcc
… -c` lines, strip `-o/-MD/-MF/-MT/-MP`, and run each serially with `-o /dev/null -I<project dir>`;
all 547 commands returned 0. Run 2026-10-01 ~20:30 while a Pi gate was running (the host was mostly
idle); expect ±20 %.</sub>
