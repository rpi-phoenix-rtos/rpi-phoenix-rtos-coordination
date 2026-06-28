# mc startup-crash repro programs (#55)

mc 4.8.31 builds for aarch64-phoenix (see ../GLIB2-MC-PORT-NOTES.md) but Data-Aborts
at startup on heap-metadata corruption. These two minimal programs link against the
SAME staged glib + stubs and were used to bisect the crash on HW:

- `glibtest.c`  — g_malloc/GString/GList(200×g_strdup_printf)/GHashTable. Runs CLEAN on
  the Pi → glib basic alloc is safe.
- `glibtest2.c` — setlocale(LC_ALL,"") + setlocale(LC_MESSAGES,NULL) + GOptionContext
  (entries/parse/get_help). Runs CLEAN on the Pi → option/locale path is safe.

Both passing => the crash is in mc's OWN early init (suspect: the UTF-8 strutil path
selected by str_init_strings), not in the ported libraries. Build line (both):

    TC=.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
    SYSROOT=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
    ZLIB=/tmp/x11-phoenix
    ${TC}gcc --sysroot=$SYSROOT -O0 -g \
      -I$SYSROOT/usr/include/glib-2.0 -I$SYSROOT/usr/lib/glib-2.0/include -I$ZLIB/include \
      -include tools/ports/glib-phoenix-shim.h \
      glibtest.c -static -L$SYSROOT/lib -L$ZLIB/lib -lglib-2.0 -lz -liconv -o glibtest
