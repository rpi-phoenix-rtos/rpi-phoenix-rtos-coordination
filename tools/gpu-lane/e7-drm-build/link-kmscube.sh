#!/bin/bash
# E7: link kmscube-lite statically from the Mesa build's archives + the objects
# of its shared-library targets (which cannot link as .so on Phoenix: libc is non-PIC).
set -u
E=/home/houp/.claude/jobs/c8f1289c/tmp/e7; M=$E/mesa-build; R=/home/houp/phoenix-rpi
B=$R/.buildroot/_build/aarch64a72-generic-rpi4b; S=$B/sysroot
CC=$E/bin/phx-gcc; CXX=$E/bin/phx-g++
F="-I$E/compat-libc -idirafter $E/compat --sysroot=$S/ -B$S/lib/ -iprefix $S/ -mcpu=cortex-a72 -mstrict-align -mno-outline-atomics"
$CC $F -I$E/mesa-src/include -I$E/mesa-src/src/gbm/main -I$E/prefix/include -I$E/prefix/include/libdrm -c $E/kmscube-lite.c -o $E/kmscube-lite.o || exit 1
$CC $F -c $E/compat/e7-compat.c -o $E/compat/e7-compat.o || exit 1
OBJS="$(ls $M/src/egl/libEGL.so.1.0.0.p/*.o $M/src/gbm/libgbm.so.1.0.0.p/*.o $M/src/gbm/backends/dri/dri_gbm.so.p/*.o $M/src/gallium/targets/dri/libgallium-26.2.0.so.p/*.o $M/src/mesa/glapi/es2api/libGLESv2.so.2.0.0.p/*.o)"
A="src/gallium/winsys/kmsro/drm/libkmsrowinsys.a src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a src/gallium/drivers/v3d/libv3d.a src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a src/gallium/drivers/vc4/libvc4.a src/broadcom/libbroadcom_v3d.a src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/compiler/libbroadcom_compiler.a src/broadcom/qpu/libbroadcom_qpu.a src/broadcom/cle/libbroadcom_cle.a src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libbroadcom_perfcntrs.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a src/mesa/libmesa.a src/compiler/glsl/libglsl.a src/compiler/glsl/glcpp/libglcpp.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/compiler/spirv/libvtn.a src/gallium/auxiliary/libgalliumvl.a src/gallium/auxiliary/libgallium.a src/mesa/glapi/shared-glapi/libglapi.a src/gallium/auxiliary/pipe-loader/libpipe_loader_static.a src/loader/libloader.a src/util/libxmlconfig.a src/gallium/winsys/sw/null/libws_null.a src/gallium/winsys/sw/wrapper/libwsw.a src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a"
AA=""; for a in $A; do [ -f "$M/$a" ] && AA="$AA $M/$a" || echo "missing archive $a" >&2; done
$CXX $F -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -o $E/kmscube-lite \
  $E/kmscube-lite.o $OBJS -Wl,--whole-archive $M/src/gallium/frontends/dri/libdri.a -Wl,--no-whole-archive \
  -Wl,--start-group $AA -Wl,--end-group \
  $E/prefix/lib/libdrm.a $B/lib/libexpat.a $B/lib/libz.a "$@" 2>&1
