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
#   UBUNTU_TAG    base image. Default 24.04 — the LTS the toolchain + bootstrap are
#                 VALIDATED against. Bump deliberately; a newer LTS may shift apt
#                 package names / meson / gcc and break the toolchain build.
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
ARG UBUNTU_TAG=24.04
FROM ubuntu:${UBUNTU_TAG}

ARG REPO_BASE=https://github.com/rpi-phoenix-rtos
ARG UPSTREAM_BASE=https://github.com/rpi-phoenix-rtos
ARG PAK0_URL=https://www.classicdosgames.com/files/games/id/quake106.zip
ARG PAK0_SHA256=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af
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

# 3. Quake shareware game data (licensing-clean): stage id1/pak0.pak into the rootfs
#    overlay so the GLQuake showcase has playable demos. Default PAK0_URL is the
#    official quake106.zip (pak0.pak is inside an LHA-compressed resource.1). A
#    direct *.pak URL is also accepted. Empty PAK0_URL = intentional opt-out (engine
#    only). A non-empty URL that fails to download/extract/verify FAILS the build —
#    we never ship a half-baked image on a broken download (fix connectivity or
#    override PAK0_URL). PAK0_SHA256 guards integrity.
RUN set -eu; \
    if [ -z "${PAK0_URL}" ]; then \
      echo "PAK0_URL empty — building WITHOUT Quake game data (engine only)"; \
    else \
      dst=sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/usr/share/quake/id1; \
      mkdir -p "$dst"; \
      tmp="$(mktemp -d)"; \
      echo "Quake shareware: fetching ${PAK0_URL}"; \
      case "${PAK0_URL}" in \
        *.pak) \
          curl -fSL --retry 5 --retry-delay 5 -o "$dst/pak0.pak" "${PAK0_URL}" \
            || { echo "ERROR: Quake pak0.pak download failed from ${PAK0_URL} — fix connectivity or override PAK0_URL"; exit 1; } ;; \
        *) \
          curl -fSL --retry 5 --retry-delay 5 -o "$tmp/q.zip" "${PAK0_URL}" \
            || { echo "ERROR: Quake shareware download failed from ${PAK0_URL} — fix connectivity or override PAK0_URL"; exit 1; }; \
          unzip -oq "$tmp/q.zip" -d "$tmp" \
            || { echo "ERROR: could not unzip the Quake shareware archive from ${PAK0_URL}"; exit 1; }; \
          ( cd "$tmp" && lha xf resource.1 ) \
            || { echo "ERROR: could not LHA-extract resource.1 from the Quake shareware archive"; exit 1; }; \
          pak="$(find "$tmp" -iname pak0.pak | head -1)"; \
          [ -n "$pak" ] \
            || { echo "ERROR: pak0.pak not found inside the Quake shareware archive"; exit 1; }; \
          cp "$pak" "$dst/pak0.pak" ;; \
      esac; \
      got="$(sha256sum "$dst/pak0.pak" | cut -d' ' -f1)"; \
      sz="$(stat -c%s "$dst/pak0.pak")"; \
      echo "Quake pak0.pak staged: size=$sz sha256=$got"; \
      if [ -n "${PAK0_SHA256}" ] && [ "$got" != "${PAK0_SHA256}" ]; then \
        echo "ERROR: pak0.pak sha256 mismatch (got $got, expected ${PAK0_SHA256}) — refusing to ship a bad image"; exit 1; \
      fi; \
      rm -rf "$tmp"; \
      echo "Quake shareware pak0.pak OK -> $dst"; \
    fi

# 4. Full SD-card image build.
RUN ./scripts/rebuild-rpi4b-fast.sh --variant "${BUILD_VARIANT}" ${BUILD_FLAGS}

# 5. Export: `docker run -v <hostdir>:/out phoenix-rpi` copies the image out.
VOLUME /out
CMD ["bash","-lc","mkdir -p /out && (cp -v artifacts/rpi4b/rpi4b-sd-2part.img /out/ 2>/dev/null || cp -v artifacts/rpi4b/*.img /out/) && sha256sum /out/*.img && echo 'Phoenix-RTOS SD image exported to ./out'"]
