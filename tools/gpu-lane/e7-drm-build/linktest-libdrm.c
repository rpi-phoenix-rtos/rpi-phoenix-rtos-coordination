/* E7 link test: the libdrm surface a kmscube-class client + Mesa touch. */
#include <stdio.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
int main(void)
{
	int fd = open("/dev/dri/card0", O_RDWR);
	drmDevicePtr devs[4];
	int n = drmGetDevices2(0, devs, 4);
	drmVersionPtr v = drmGetVersion(fd);
	drmModeResPtr r = drmModeGetResources(fd);
	drmModeAtomicReqPtr req = drmModeAtomicAlloc();
	uint32_t h = 0, fb = 0; int pfd = -1; uint64_t cap;
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &cap);
	drmPrimeHandleToFD(fd, 1, DRM_CLOEXEC, &pfd);
	drmPrimeFDToHandle(fd, pfd, &h);
	drmSyncobjCreate(fd, 0, &h);
	drmModeAddFB2(fd, 64, 64, 0x34325258, &h, &h, &h, &fb, 0);
	drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
	drmEventContext ev = { .version = 4 };
	drmHandleEvent(fd, &ev);
	char *rn = drmGetRenderDeviceNameFromFd(fd);
	printf("%d %p %p %s %d\n", n, (void*)v, (void*)r, rn ? rn : "-", drmGetNodeTypeFromFd(fd));
	return 0;
}
