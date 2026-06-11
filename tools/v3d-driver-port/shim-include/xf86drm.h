/* Minimal xf86drm.h shim for the v3d gallium driver on Phoenix (GLQuake Path C).
 * Declares only the libdrm surface the v3d driver uses. drmIoctl() dispatches into
 * the Phoenix winsys backend (phoenix_v3d_ioctl); syncobj/prime are stubbed
 * (synchronous submit, no BO sharing). Impls live in v3d_libdrm_shim.c (port task). */
#ifndef PHX_XF86DRM_H
#define PHX_XF86DRM_H
#include <stdint.h>

int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);
static inline int drmIoctl(int fd, unsigned long request, void *arg)
{ return phoenix_v3d_ioctl(fd, request, arg); }

/* DRM syncobj (fences) — stubbed for synchronous submit. */
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL          (1u<<0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT   (1u<<1)
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles,
                   int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
char *drmGetRenderDeviceNameFromFd(int fd);
#endif
