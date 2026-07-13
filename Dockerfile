# Phoenix-RTOS on Raspberry Pi 4 — fully self-contained, reproducible SD-image build.
#
# ONE command builds a bootable Pi 4 SD-card image inside a container that controls
# the entire toolchain, so it works on any host with a Docker CLI regardless of the
# host OS. NOTHING is copied from the host: every source tree, package, font, and the
# Quake shareware game data is fetched over the network (git clone + downloads).
#
# Quick use (see README.md "Build with Docker"):
#   docker build -t phoenix-rpi https://raw.githubusercontent.com/houp/phoenix-rpi/main/Dockerfile
#   docker run --rm -v "$PWD/out":/out phoenix-rpi
#   # -> ./out/rpi4b-sd-2part.img
#
# Build from a not-yet-published local checkout (git served from the host): use
#   scripts/build-sd-in-docker.sh   (starts a git/http server + sets the ARGs below)
#
# Args (override with --build-arg):
#   UBUNTU_TAG    base image. Default 24.04 — the LTS the toolchain + bootstrap are
#                 VALIDATED against. Bump deliberately; a newer LTS may shift apt
#                 package names / meson / gcc and break the toolchain build.
#   REPO_BASE     git base for the phoenix-rpi repos (coord + siblings + mesa/
#                 quakespasm/vkquake forks). Default: public GitHub.
#   UPSTREAM_BASE phoenix-rtos upstream base (fallback remote).
#   PAK0_URL      URL of the Quake SHAREWARE pak0.pak (freely redistributable). Empty
#                 = build without game data (engine still built; demos need a pak0).
#   BUILD_VARIANT sd (default) | nfsroot | netboot.
#   BUILD_FLAGS   extra rebuild flags. Default: --with-showcase --with-ports
#                 (GLQuake + X11/WindowMaker + busybox). Use "" for a base image.
ARG UBUNTU_TAG=24.04
FROM ubuntu:${UBUNTU_TAG}

ARG REPO_BASE=https://github.com/houp
ARG UPSTREAM_BASE=https://github.com/phoenix-rtos
ARG PAK0_URL=
ARG BUILD_VARIANT=sd
ARG BUILD_FLAGS=--with-showcase --with-ports
ENV DEBIAN_FRONTEND=noninteractive

# Minimal bootstrap prerequisites. `sudo` is a passthrough as root but bootstrap-
# linux-host.sh calls `sudo apt-get`, so it must exist; bootstrap installs the full
# apt set itself.
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates sudo wget curl xz-utils \
 && rm -rf /var/lib/apt/lists/*

# 1. Clone the coordination repo (carries every build script, incl. bootstrap).
RUN git clone "${REPO_BASE}/phoenix-rpi.git" /build/phoenix-rpi
WORKDIR /build/phoenix-rpi

# 2. Bootstrap: install all Ubuntu packages, clone the 16 sibling repos + the
#    mesa/quakespasm/vkquake forks + the Pi firmware, and build the cross toolchain.
#    FORK_BASE/UPSTREAM_BASE point the clones at REPO_BASE (GitHub, or a host server).
RUN PROJECT_DIR=/build/phoenix-rpi \
    PHOENIX_FORK_BASE="${REPO_BASE}" \
    PHOENIX_UPSTREAM_BASE="${UPSTREAM_BASE}" \
    EXTERNAL_FORK_BASE="${REPO_BASE}" \
    ./scripts/bootstrap-linux-host.sh

# 3. Quake shareware game data (optional, licensing-clean): fetch pak0.pak into the
#    rootfs overlay so the GLQuake showcase has playable demos. Skipped if PAK0_URL
#    is empty; the engine is still built either way.
RUN if [ -n "${PAK0_URL}" ]; then \
      dst=sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/usr/share/quake/id1 ; \
      mkdir -p "$dst" ; \
      echo "fetching Quake shareware pak0 from ${PAK0_URL}" ; \
      wget -qO "$dst/pak0.pak" "${PAK0_URL}" && ls -l "$dst/pak0.pak" || echo "WARN: pak0 fetch failed; building without game data" ; \
    else echo "PAK0_URL empty — building without Quake game data" ; fi

# 4. Full SD-card image build.
RUN ./scripts/rebuild-rpi4b-fast.sh --variant "${BUILD_VARIANT}" ${BUILD_FLAGS}

# 5. Export: `docker run -v <hostdir>:/out phoenix-rpi` copies the image out.
VOLUME /out
CMD ["bash","-lc","mkdir -p /out && (cp -v artifacts/rpi4b/rpi4b-sd-2part.img /out/ 2>/dev/null || cp -v artifacts/rpi4b/*.img /out/) && sha256sum /out/*.img && echo 'Phoenix-RTOS SD image exported to ./out'"]
