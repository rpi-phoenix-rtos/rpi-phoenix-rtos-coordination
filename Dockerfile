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
# Quake II / Quake III demo data. NO default URL: unlike the Q1 shareware
# (quake106.zip, freely redistributable), the Q2/Q3 demo paks have their own
# redistribution terms — supply a URL only after verifying rights for your
# distribution. Empty = the engine still builds, just without bundled data.
#   Q2: a *.zip containing baseq2/pak0.pak, or a direct *.pak
#   Q3: a *.zip/*.pk3 containing demoq3/pak0.pk3, or a direct *.pk3
ARG PAK0Q2_URL=
ARG PAK0Q2_SHA256=
ARG PAK0Q3_URL=
ARG PAK0Q3_SHA256=
ARG BUILD_VARIANT=sd
ARG BUILD_FLAGS=--with-showcase --with-ports
ENV DEBIAN_FRONTEND=noninteractive

# Minimal bootstrap prerequisites. `sudo` is a passthrough as root but bootstrap-
# linux-host.sh calls `sudo apt-get`, so it must exist; bootstrap installs the full
# apt set itself.
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates sudo wget curl xz-utils unzip lhasa \
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

# 3. Quake game data (licensing-clean): stage the freely-redistributable demo/
#    shareware paks into the rootfs overlay so the showcase Quake engines have
#    playable data. Delegated to the shared scripts/fetch-quake-data.sh (same
#    path used by SD + netboot builds). Q1 shareware (quake106.zip) is the
#    default; Q2/Q3 have no default URL (their demo paks carry their own
#    redistribution terms — pass PAK0Q2_URL / PAK0Q3_URL to opt in). An empty
#    URL skips that game (engine still built); a non-empty URL that fails to
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
