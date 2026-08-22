# Phoenix-RTOS on Raspberry Pi 4 — fully self-contained, reproducible SD-image build.
#
# ONE command builds a bootable Pi 4 SD-card image inside a container that controls
# the entire toolchain, so it works on any host with a Docker CLI regardless of the
# host OS. NOTHING is copied from the host: every source tree, package, font, and the
# Quake shareware game data is fetched over the network (git clone + downloads).
#
# Quick use (see README.md "Build with Docker"):
#   docker build -t phoenix-rpi https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
#   docker run --rm -v "$PWD/out":/out phoenix-rpi
#   # -> ./out/rpi4b-sd-2part.img
#
# Build from a not-yet-published local checkout (git served from the host): use
#   scripts/build-sd-in-docker.sh   (starts a git/http server + sets the ARGs below)
#
# Args (override with --build-arg):
#   UBUNTU_TAG    base image. Default 26.04 — the current LTS and the host the
#                 GCC 16.2.0 + binutils 2.47 toolchain is built + validated on, so the
#                 Docker build matches the known-good host environment. Change
#                 deliberately; a different LTS may shift apt package names / meson /
#                 gcc and break the toolchain build.
#   REPO_BASE     git base for the org repos (coord + 16 siblings + quakespasm +
#                 lwip forks). Default: public GitHub org rpi-phoenix-rtos. (mesa is
#                 NOT a fork — upstream Mesa @ a tag + patches/mesa/, see bootstrap.)
#   UPSTREAM_BASE fallback remote; also the org (self-contained set).
#   PAK0_URL      URL of the Quake SHAREWARE data (freely redistributable). Default:
#                 the official quake106.zip; the build extracts id1/pak0.pak from it
#                 (a direct pak0.pak URL is also accepted). Set to "" to build WITHOUT
#                 game data (engine still built). A NON-EMPTY URL that fails to
#                 download/extract/verify FAILS the build — never a half-baked image.
#   PAK0_SHA256   expected sha256 of the resulting pak0.pak; mismatch fails the build
#                 ("" disables the check).
#   BUILD_VARIANT sd (default) | nfsroot | netboot.
#   BUILD_FLAGS   extra rebuild flags. Default: --with-showcase --with-ports
#                 (GLQuake + X11/WindowMaker + busybox). Use "" for a base image.
ARG UBUNTU_TAG=26.04
FROM ubuntu:${UBUNTU_TAG}

ARG REPO_BASE=https://github.com/rpi-phoenix-rtos
ARG UPSTREAM_BASE=https://github.com/rpi-phoenix-rtos
ARG PAK0_URL=https://www.classicdosgames.com/files/games/id/quake106.zip
ARG PAK0_SHA256=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af
# Quake II / Quake III demo data. These are the freely-downloadable id Software
# demos (the same class as the Q1 shareware): the build downloads them at build
# time and bakes them into the image you build — this repo distributes only the
# build scripts, never a pre-built image containing the data. Set a URL to "" to
# build that engine without bundled data. Overridable, e.g. to a local mirror.
#   Q2: q2-314-demo-x86.exe (InstallShield self-extractor; needs p7zip) -> baseq2/pak0.pak
#   Q3: linuxq3ademo-1.11-6.x86.gz.sh (makeself installer)             -> demoq3/pak0.pk3
ARG PAK0Q2_URL=https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe
ARG PAK0Q2_SHA256=cae257182f34d3913f3d663e1d7cf865d668feda6af393d4ecf3e9e408b48d09
ARG PAK0Q3_URL=https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake3/linux/linuxq3ademo-1.11-6.x86.gz.sh
ARG PAK0Q3_SHA256=e77abad2466f45a0a7ea018445528f9b95a0fe7789fa1abc1a7718bbf0754b08
ARG BUILD_VARIANT=sd
ARG BUILD_FLAGS=--with-showcase --with-ports
ENV DEBIAN_FRONTEND=noninteractive

# Minimal bootstrap prerequisites. `sudo` is a passthrough as root but bootstrap-
# linux-host.sh calls `sudo apt-get`, so it must exist; bootstrap installs the full
# apt set itself.
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates sudo wget curl xz-utils unzip lhasa 7zip \
 && rm -rf /var/lib/apt/lists/*

# 1. Clone the coordination repo (carries every build script, incl. bootstrap).
RUN git clone "${REPO_BASE}/rpi-phoenix-rtos-coordination.git" /build/phoenix-rpi
WORKDIR /build/phoenix-rpi

# 2. Bootstrap: install all Ubuntu packages, clone the 16 sibling repos + quakespasm
#    + the lwip library + the Pi firmware, fetch upstream Mesa @ the pinned tag and
#    apply patches/mesa/, and build the cross toolchain.
#    FORK_BASE/UPSTREAM_BASE point the clones at REPO_BASE (GitHub, or a host server).
RUN PROJECT_DIR=/build/phoenix-rpi \
    PHOENIX_FORK_BASE="${REPO_BASE}" \
    PHOENIX_UPSTREAM_BASE="${UPSTREAM_BASE}" \
    EXTERNAL_FORK_BASE="${REPO_BASE}" \
    ./scripts/bootstrap-linux-host.sh

# 3. Quake game data: download the id Software demos and bake them into the
#    rootfs overlay so the showcase Quake engines have playable data. Delegated
#    to the shared scripts/fetch-quake-data.sh (same path used by SD + netboot
#    builds): Q1 shareware (quake106.zip), Q2 demo (baseq2/pak0.pak), Q3 demo
#    (demoq3/pak0.pk3). Each defaults to a verified upstream URL; set a PAK0*_URL
#    to "" to skip that game (engine still built). A non-empty URL that fails to
#    download/extract/verify FAILS the build (never ship a half-baked image).
RUN set -eu; \
    ./scripts/fetch-quake-data.sh q1 "${PAK0_URL}"   "${PAK0_SHA256}"; \
    ./scripts/fetch-quake-data.sh q2 "${PAK0Q2_URL}" "${PAK0Q2_SHA256}"; \
    ./scripts/fetch-quake-data.sh q3 "${PAK0Q3_URL}" "${PAK0Q3_SHA256}"

# 4. Full SD-card image build.
RUN ./scripts/rebuild-rpi4b-fast.sh --variant "${BUILD_VARIANT}" ${BUILD_FLAGS}

# 5. Export: `docker run -v <hostdir>:/out phoenix-rpi` copies the image out.
VOLUME /out
CMD ["bash","-lc","mkdir -p /out && (cp -v artifacts/rpi4b/rpi4b-sd-2part.img /out/ 2>/dev/null || cp -v artifacts/rpi4b/*.img /out/) && sha256sum /out/*.img && echo 'Phoenix-RTOS SD image exported to ./out'"]
