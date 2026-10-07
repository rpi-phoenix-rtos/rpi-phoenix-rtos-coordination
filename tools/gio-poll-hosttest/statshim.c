/*
 * LD_PRELOAD shim for the GIO poll file monitor host test.
 *
 * Counts the libc calls the monitor makes (stat/lstat/opendir/readdir/
 * closedir) and lets the test simulate filesystems the host does not have:
 *
 *   SHIM_COARSE=1      every stat() result has tv_nsec = 0: a filesystem with
 *                      one-second timestamps, as Phoenix-RTOS stat() reports
 *   SHIM_WATCH=<path>  stat() calls on exactly this path count as "stat_dir"
 *   shim_hold(path)    from now on stat() of exactly @path reports the size,
 *                      link count, mtime and ctime it had at this call: the
 *                      directory "did not change", although its entries did
 *                      (a same-second change, or a filesystem that does not
 *                      update a directory's stamps)
 *   shim_release()     undo shim_hold()
 *
 * The test reaches shim_counts()/shim_hold()/shim_release() through dlsym().
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct shim_counts {
	long stat_dir;  /* stat() of SHIM_WATCH */
	long stat_other;
	long lstat;
	long opendir;
	long readdir;   /* calls, including the one that returns NULL */
	long closedir;
};

static struct shim_counts counts;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char *held_path;
static struct stat held;

#define BUMP(f) __atomic_add_fetch(&counts.f, 1, __ATOMIC_RELAXED)

static void *next(const char *name)
{
	return dlsym(RTLD_NEXT, name);
}

static void fixup(const char *path, struct stat *st)
{
	if (getenv("SHIM_COARSE") != NULL) {
		st->st_atim.tv_nsec = 0;
		st->st_mtim.tv_nsec = 0;
		st->st_ctim.tv_nsec = 0;
	}
	pthread_mutex_lock(&lock);
	if ((held_path != NULL) && (strcmp(path, held_path) == 0)) {
		st->st_size = held.st_size;
		st->st_nlink = held.st_nlink;
		st->st_mtim = held.st_mtim;
		st->st_ctim = held.st_ctim;
	}
	pthread_mutex_unlock(&lock);
}

static void count_stat(const char *path)
{
	const char *w = getenv("SHIM_WATCH");
	if ((w != NULL) && (strcmp(w, path) == 0)) {
		BUMP(stat_dir);
	}
	else {
		BUMP(stat_other);
	}
}

int stat64(const char *path, struct stat64 *st)
{
	static int (*real)(const char *, struct stat64 *);
	if (real == NULL) {
		real = next("stat64");
	}
	count_stat(path);
	int r = real(path, st);
	if (r == 0) {
		fixup(path, (struct stat *)st);
	}
	return r;
}

int stat(const char *path, struct stat *st)
{
	return stat64(path, (struct stat64 *)st);
}

int lstat64(const char *path, struct stat64 *st)
{
	static int (*real)(const char *, struct stat64 *);
	if (real == NULL) {
		real = next("lstat64");
	}
	BUMP(lstat);
	int r = real(path, st);
	if (r == 0) {
		fixup(path, (struct stat *)st);
	}
	return r;
}

int lstat(const char *path, struct stat *st)
{
	return lstat64(path, (struct stat64 *)st);
}

DIR *opendir(const char *path)
{
	static DIR *(*real)(const char *);
	if (real == NULL) {
		real = next("opendir");
	}
	BUMP(opendir);
	return real(path);
}

struct dirent64 *readdir64(DIR *d)
{
	static struct dirent64 *(*real)(DIR *);
	if (real == NULL) {
		real = next("readdir64");
	}
	BUMP(readdir);
	return real(d);
}

struct dirent *readdir(DIR *d)
{
	return (struct dirent *)readdir64(d);
}

int closedir(DIR *d)
{
	static int (*real)(DIR *);
	if (real == NULL) {
		real = next("closedir");
	}
	BUMP(closedir);
	return real(d);
}

void shim_counts(struct shim_counts *out)
{
	out->stat_dir = __atomic_load_n(&counts.stat_dir, __ATOMIC_RELAXED);
	out->stat_other = __atomic_load_n(&counts.stat_other, __ATOMIC_RELAXED);
	out->lstat = __atomic_load_n(&counts.lstat, __ATOMIC_RELAXED);
	out->opendir = __atomic_load_n(&counts.opendir, __ATOMIC_RELAXED);
	out->readdir = __atomic_load_n(&counts.readdir, __ATOMIC_RELAXED);
	out->closedir = __atomic_load_n(&counts.closedir, __ATOMIC_RELAXED);
}

int shim_hold(const char *path)
{
	static int (*real)(const char *, struct stat64 *);
	struct stat64 st;
	if (real == NULL) {
		real = next("stat64");
	}
	if (real(path, &st) != 0) {
		return -1;
	}
	pthread_mutex_lock(&lock);
	free(held_path);
	held_path = strdup(path);
	memcpy(&held, &st, sizeof(held));
	if (getenv("SHIM_COARSE") != NULL) {
		held.st_mtim.tv_nsec = 0;
		held.st_ctim.tv_nsec = 0;
	}
	pthread_mutex_unlock(&lock);
	return 0;
}

void shim_release(void)
{
	pthread_mutex_lock(&lock);
	free(held_path);
	held_path = NULL;
	pthread_mutex_unlock(&lock);
}
