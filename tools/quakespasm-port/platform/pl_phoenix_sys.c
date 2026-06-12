/*
 * pl_phoenix_sys.c — Phoenix platform shim for Quakespasm: replaces sys_sdl_unix.c
 * + pl_linux.c. Pure POSIX (Phoenix provides open/read/write/lseek/close,
 * clock_gettime, mkdir, usleep), no SDL.
 */
#include "quakedef.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

qboolean isDedicated;          /* declared extern in quakedef.h */

#define MAX_HANDLES 64
static FILE *sys_handles[MAX_HANDLES];

static int findhandle(void)
{
	int i;
	for (i = 1; i < MAX_HANDLES; i++)
		if (!sys_handles[i])
			return i;
	Sys_Error("out of file handles");
	return -1;
}

void Sys_Init(void)
{
}

int Sys_FileOpenRead(const char *path, int *hndl)
{
	int i = findhandle();
	FILE *f = fopen(path, "rb");
	if (!f) {
		*hndl = -1;
		return -1;
	}
	sys_handles[i] = f;
	*hndl = i;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	return (int)len;
}

int Sys_FileOpenWrite(const char *path)
{
	int i = findhandle();
	FILE *f = fopen(path, "wb");
	if (!f)
		Sys_Error("Error opening %s: %s", path, strerror(errno));
	sys_handles[i] = f;
	return i;
}

void Sys_FileClose(int handle)
{
	if (handle <= 0 || handle >= MAX_HANDLES || !sys_handles[handle])
		return;
	fclose(sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek(int handle, int position)
{
	if (handle > 0 && handle < MAX_HANDLES && sys_handles[handle])
		fseek(sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead(int handle, void *dest, int count)
{
	if (handle <= 0 || handle >= MAX_HANDLES || !sys_handles[handle])
		return 0;
	return (int)fread(dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite(int handle, const void *data, int count)
{
	if (handle <= 0 || handle >= MAX_HANDLES || !sys_handles[handle])
		return 0;
	return (int)fwrite(data, 1, count, sys_handles[handle]);
}

int Sys_FileType(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;                       /* FS_ENT_NONE */
	if (S_ISDIR(st.st_mode))
		return 2;                       /* FS_ENT_DIRECTORY */
	return 1;                               /* FS_ENT_FILE */
}

void Sys_mkdir(const char *path)
{
	(void)mkdir(path, 0777);
}

void Sys_Printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void Sys_Quit(void)
{
	exit(0);
}

void Sys_Error(const char *error, ...)
{
	va_list ap;
	va_start(ap, error);
	fprintf(stderr, "\nQuake Error: ");
	vfprintf(stderr, error, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

double Sys_DoubleTime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void Sys_Sleep(unsigned long msecs)
{
	usleep(msecs * 1000);
}

void Sys_SendKeyEvents(void)
{
	IN_SendKeyEvents();
}
