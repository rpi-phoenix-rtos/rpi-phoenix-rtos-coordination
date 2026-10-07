/*
 * Force-included (-include) into every Phoenix source this harness builds for
 * the host. Supplies the few Phoenix-only names the sources take for granted
 * and routes the calls that would touch the real host (syslog, kill, mkdir) or
 * whose clock the harness controls (pthread_cond_timedwait) to shim.c.
 */
#ifndef HOSTTEST_HOSTCOMPAT_H
#define HOSTTEST_HOSTCOMPAT_H

#define EOK        0
#define _PAGE_SIZE 4096

/* libtty brings its own ttydefaults.h */
#define _SYS_TTYDEFAULTS_H_

/* struct winsize: libtty.h expects it from <termios.h>, glibc has it here */
#include <sys/ioctl.h>

/* Phoenix c_cc slots glibc lacks: unused indices of its c_cc[32] */
#define VERASE2 20

#define syslog                 shim_syslog
#define kill                   shim_kill
#define mkdir                  shim_mkdir
#define pthread_cond_timedwait shim_cond_timedwait

#endif
