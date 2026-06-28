/*
 * Phoenix-RTOS compatibility shim for GNU nano (force-included via gcc -include).
 * Fills two small libc gaps nano uses; the bundled-gnulib-free nano 2.2.x needs
 * nothing more.
 */
#ifndef NANO_PHOENIX_SHIM_H
#define NANO_PHOENIX_SHIM_H

#include <pwd.h>

/* Phoenix <stdio.h> lacks P_tmpdir (used for nano's mkstemp temp files). */
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

/* libphoenix has getpwnam/getpwuid but not the passwd-enumeration API; nano uses
 * it only to tab-complete ~username. Stub it (no users enumerated) so nano
 * builds; ~name still resolves via getpwnam elsewhere. */
static inline struct passwd *getpwent(void) { return (struct passwd *)0; }
static inline void setpwent(void) { }
static inline void endpwent(void) { }

#endif /* NANO_PHOENIX_SHIM_H */
