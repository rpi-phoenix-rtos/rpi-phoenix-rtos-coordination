# Phoenix-RTOS RPi4 — gcc-16.2.0 release (DRAFT notes + turnkey G5 steps)

Draft for the gcc-16 release (G-GCC/E10). Finalize once G3.2 (Docker `--no-cache`
reproducible build) + G3.3 (boot-verify the Docker image) pass. Do NOT publish
until both are green.

## Headline
First Phoenix-RTOS RPi4 release built with an up-to-date **GCC 16.2.0 +
binutils 2.47** cross-toolchain (was GCC 14.2.0 / binutils 2.43), produced
reproducibly from a single self-contained Dockerfile on Ubuntu 26.04.

## What's in it
- **Toolchain:** gcc-16.2.0 + binutils-2.47 is now the default `.toolchain`
  (a fresh `bootstrap-linux-host.sh` builds it). Core + framework ports + X11 +
  GPU + games all build clean under gcc-16 (C23 handled via `-std=gnu17` pins).
- **Interactive bash now works** (long-standing bug fixed): GNU bash 5.2 runs
  fully interactively over the console. Root cause was a libphoenix `select()`
  bug — a NULL (infinite) timeout returned 0 immediately instead of blocking,
  which made GNU readline abort at the first prompt. Fixed in
  `libphoenix sys/select.c` (033ee1f) — a general fix for any blocking
  `select(…, NULL)`. Regression test in `phoenix-rtos-tests` (ca616da).
  Note: the fix lives in the base libc, so it benefits **every** interactive
  program (the shipped `ash`/`sh`, readline apps, etc.). GNU bash itself is not
  yet in the default image's component set (it was built ad-hoc during the fix
  arc, not wired into `ports.yaml`) — FOLLOW-UP: add a `bash` port entry so the
  release image ships the shell the headline names.
- **libphoenix `siginterrupt()`** implemented (was declared but missing) —
  helps any port that references it (incl. job-control-off shells).
- **Reproducible build hardening:** persistent distfiles cache + reliable
  mirrors (artfiles for x.org, MacPorts for fontconfig/cairo) so a from-scratch
  build survives upstream CDN outages; Docker base bumped to Ubuntu 26.04 LTS;
  Docker fetches all three Quakes' demo data at build time (Q1/Q2/Q3).
- Full capability set unchanged from the prior release (drivers, NFS/SD boot,
  V3D GL/Vulkan, X11, the CLI/language ecosystem) — see README/KNOWN-ISSUES.

## Verification gates
- [x] **G3.2 GREEN (2026-08-25):** `scripts/build-sd-in-docker.sh` (--no-cache)
      → BUILD_RC=0, image at `docker-out/rpi4b-sd-2part.img`
      (835 MiB, sha256 `29e20cd84677f5eb9e536a89c5e0e1f264665b98f55ad6ad6131288223c48945`).
      Clean container build from committed sibling state; all reproducibility
      mirror fixes held (freetype/x11/toolchain/glib2).
- [x] **G3.3 userspace HW-verified (2026-08-25):** four docker-built binaries
      staged onto the gcc-16 netboot NFS root and executed on the Pi, 0 faults —
      `cksum`+`od` (the ustack Data-Abort regression) emit correct output with no
      fault; `sha256sum` bit-exact vs host; `lua -v` runs (Lua 5.4.7). ⇒ the
      reproducibly-built artifact produces working binaries on real HW.
- [~] **G3.3 residual — full-image SD-boot of the exact artifact: BANKED
      (owner-attended).** The image is `--variant sd` (its loader.disk mounts the
      ext2 SD root) and the netboot Pi has **no card in the slot** (`sdcard: no
      card present in slot 0`), so the exact artifact can't be SD-booted
      unattended. Mitigating evidence: the gcc-16 kernel/loader boot was already
      HW-verified 2026-08-22 (full netboot → psh + NFS + all drivers) and the
      docker kernel/loader are same-source. To close: self-flash via netboot-Linux
      → `dd` to the Pi's card → SD-boot (needs a card physically in the Pi).

## Turnkey G5 publish steps (run after both gates pass)
1. Snapshot the integration manifest:
   `./scripts/snapshot-integration-state.sh 2026-08-25-gcc16-release --note "gcc-16.2.0+binutils-2.47 release; Docker-reproducible; interactive bash fixed"`
2. Tag known-good across siblings + coord (rollback anchor + release marker):
   `TAG=known-good/gcc16-release-2026-08-25` — tag each sibling that changed
   this arc at its published HEAD (libphoenix, phoenix-rtos-ports,
   phoenix-rtos-build, phoenix-rtos-tests) + the coord repo, and push the tags
   to `publish` (do NOT force-push; lwip stays on the scrubbed-cherry-pick flow).
3. Update the org top-level README to state the release is **gcc-16.2.0-based**
   (the callout already says "final validation" — flip to "released"), and link
   the Docker one-liner + the image artifact.
4. Attach/announce the built `docker-out/rpi4b-sd-2part.img` (sha256) as the
   release artifact.

## Rollback
Keep gcc-14 `.toolchain-gcc14` + the prior known-good tag until the gcc-16
release is signed off on HW. Any regression → restore gcc-14 `.toolchain` +
rebuild.
