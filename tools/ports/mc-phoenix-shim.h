/*
 * Phoenix-RTOS compatibility shim for Midnight Commander (force-included via
 * gcc -include). Fills libc gaps mc uses that Phoenix libphoenix lacks.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#ifndef MC_PHOENIX_SHIM_H
#define MC_PHOENIX_SHIM_H

#include <pwd.h>

/* libphoenix has getpwnam/getpwuid but not the passwd-enumeration API; mc uses
 * it only to tab-complete ~username. Stub it (no users enumerated) so mc builds;
 * ~name still resolves via getpwnam elsewhere. Same approach as the nano port. */
#ifndef MC_HAVE_PWENT_STUBS
#define MC_HAVE_PWENT_STUBS 1
static inline struct passwd *getpwent(void) { return (struct passwd *)0; }
static inline void setpwent(void) { }
static inline void endpwent(void) { }
#endif

/* P_tmpdir for temp-file fallbacks. */
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

#endif /* MC_PHOENIX_SHIM_H */
