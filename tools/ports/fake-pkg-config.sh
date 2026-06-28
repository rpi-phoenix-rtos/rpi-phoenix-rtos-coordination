#!/usr/bin/env bash
#
# Minimal fake pkg-config for cross-builds without a real pkg-config DB. Answers
# just enough for mc's configure (glib/gmodule queries): version checks succeed,
# --cflags/--libs return the values from the matching <MOD>_CFLAGS/<MOD>_LIBS env
# the caller already exported, and --variable=gmodule_supported returns "true".
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
case "$*" in
	*--version*)            echo "0.29.2"; exit 0 ;;
	*--variable=gmodule_supported*) echo "true"; exit 0 ;;
	*--variable=glib_genmarshal*)   echo "true"; exit 0 ;;
	*--variable=*)          echo ""; exit 0 ;;
	*--modversion*)         echo "2.56.4"; exit 0 ;;
	*--atleast-version*|*--exists*) exit 0 ;;     # claim every queried module present
	*--cflags*)             echo "${GLIB_CFLAGS:-}"; exit 0 ;;
	*--libs*)               echo "${GLIB_LIBS:-}"; exit 0 ;;
esac
exit 0
