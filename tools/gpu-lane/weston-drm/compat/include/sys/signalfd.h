/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm compat: signalfd emulation (see sys/epoll.h). signalfd() installs a
 * handler for each signal of the mask that writes one struct signalfd_siginfo
 * (Linux layout, 128 bytes) into the descriptor's socket, then UNBLOCKS those
 * signals: callers block them first (libwayland's wl_event_loop_add_signal does),
 * and a blocked signal would never reach the handler. Closing the descriptor
 * restores the previous action and blocks the signals again. Only ssi_signo is
 * filled in.
 */
#ifndef WLPHX_SYS_SIGNALFD_H
#define WLPHX_SYS_SIGNALFD_H

#include <signal.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFD_CLOEXEC  0x4000 /* = O_CLOEXEC */
#define SFD_NONBLOCK 0x8000 /* the read end is always non-blocking */

struct signalfd_siginfo {
	uint32_t ssi_signo;
	int32_t ssi_errno;
	int32_t ssi_code;
	uint32_t ssi_pid;
	uint32_t ssi_uid;
	int32_t ssi_fd;
	uint32_t ssi_tid;
	uint32_t ssi_band;
	uint32_t ssi_overrun;
	uint32_t ssi_trapno;
	int32_t ssi_status;
	int32_t ssi_int;
	uint64_t ssi_ptr;
	uint64_t ssi_utime;
	uint64_t ssi_stime;
	uint64_t ssi_addr;
	uint16_t ssi_addr_lsb;
	uint16_t __pad2;
	int32_t ssi_syscall;
	uint64_t ssi_call_addr;
	uint32_t ssi_arch;
	uint8_t __pad[28];
};

int signalfd(int fd, const sigset_t *mask, int flags);

#ifdef __cplusplus
}
#endif

#endif
