/*
 * Phoenix-RTOS Raspberry Pi 4 — stress-test / micro-benchmark shared contract.
 *
 * Every stress util prints machine-greppable result lines on stdout (the launcher
 * / psh wires stdout to the UART, so the host capture sees them). The orchestrator
 * greps these to classify outcomes and hunt faults.
 *
 * THE CENTRAL RULE — three buckets, only FAULT is a finding:
 *   OK     : the operation succeeded as intended.
 *   LIMIT  : the OS correctly REFUSED at a resource boundary (malloc->NULL near
 *            full RAM, fork->EAGAIN at the process cap, open->EMFILE, ...). This
 *            is CORRECT behaviour and must NOT be reported as a fault.
 *   FAULT  : a real defect — crash (Exception), hang (no heartbeat), corruption
 *            (integrity mismatch), wrong result (lost update / bad readback), or
 *            an errno that is NOT a legitimate resource limit.
 *
 * Line formats (stable — the host analyzer depends on them):
 *   STRESS <suite>.<test> OK     <free-form metrics...>
 *   STRESS <suite>.<test> LIMIT  <what limit + errno>
 *   STRESS <suite>.<test> FAULT  <what went wrong>
 *   STRESS-HB <suite> <checkpoint>            (heartbeat: pin a hang to an op)
 *   STRESS-SUMMARY <suite> ok=<n> limit=<n> fault=<n> [extra]
 *
 * Discipline: a FAULT must be a real defect, not a test/libc bug. Re-check the
 * test logic and whether the errno is a legitimate limit before emitting FAULT.
 *
 * Copyright 2026 Phoenix Systems
 */

#ifndef _RPI4_STRESS_H_
#define _RPI4_STRESS_H_

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Unbuffer stdout once at startup so nothing is lost if a later op wedges/crashes. */
static inline void sr_init(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
}

/* Counters for the end-of-run summary. Each suite keeps its own. */
typedef struct {
	unsigned long ok;
	unsigned long limit;
	unsigned long fault;
} sr_tally_t;

#define SR_OK(tally, suite, test, fmt, ...) \
	do { (tally)->ok++; printf("STRESS %s.%s OK " fmt "\n", (suite), (test), ##__VA_ARGS__); } while (0)

#define SR_LIMIT(tally, suite, test, fmt, ...) \
	do { (tally)->limit++; printf("STRESS %s.%s LIMIT " fmt "\n", (suite), (test), ##__VA_ARGS__); } while (0)

#define SR_FAULT(tally, suite, test, fmt, ...) \
	do { (tally)->fault++; printf("STRESS %s.%s FAULT " fmt "\n", (suite), (test), ##__VA_ARGS__); } while (0)

/* Heartbeat: print a checkpoint so a hang/crash with no further output pins the
 * exact op that wedged. Use liberally around risky operations. */
#define SR_HB(suite, fmt, ...) \
	do { printf("STRESS-HB %s " fmt "\n", (suite), ##__VA_ARGS__); } while (0)

#define SR_SUMMARY(tally, suite, fmt, ...) \
	do { printf("STRESS-SUMMARY %s ok=%lu limit=%lu fault=%lu " fmt "\n", \
		(suite), (tally)->ok, (tally)->limit, (tally)->fault, ##__VA_ARGS__); } while (0)

/* Classify an errno as a legitimate resource LIMIT vs a FAULT. EAGAIN/ENOMEM/
 * EMFILE/ENFILE/ENOSPC are the OS correctly refusing at a boundary. */
static inline int sr_errno_is_limit(int e)
{
	return (e == EAGAIN || e == ENOMEM || e == EMFILE || e == ENFILE || e == ENOSPC);
}

#endif /* _RPI4_STRESS_H_ */
