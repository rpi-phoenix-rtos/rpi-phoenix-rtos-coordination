/* SPDX-License-Identifier: Zlib
 *
 * Phoenix epoxy shim — <epoxy/egl.h>.
 *
 * glamor includes <epoxy/egl.h> only under #ifdef GLAMOR_HAS_GBM, which is
 * undefined on Phoenix (no EGL/GBM/DRM), so glamor's core never pulls this in.
 * It exists only so any stray include resolves cleanly.
 *
 * Copyright 2026 Phoenix Systems.
 */
#ifndef PHOENIX_EPOXY_EGL_SHIM_H
#define PHOENIX_EPOXY_EGL_SHIM_H

/* Intentionally empty: the EGL/GBM path is not built on Phoenix. */

#endif /* PHOENIX_EPOXY_EGL_SHIM_H */
