# SMP Phase E — validated cross-CPU load distribution (2026-05-25)

Closes task #29. The "do all 4 CPUs actually schedule?" question had
been open since SMP Phase D landed (kernel `af171987`): the boot-time
`smp: tick+15s` heartbeat showed all 4 cpus taking timer ticks, but
that proves CNTV PPI delivery, not full scheduler dispatch. The
gating measurement was per-thread CPU time across multiple cpus — and
that was blocked by `TD-19` (post-fbcon UART silence) until Tier 5c
gave us a network-routed observability channel.

## Method

Extended the lwip-port diag responder (`port/diag-udp.c`) with a
second sub-command: any UDP datagram whose first byte is `'t'`
returns up to 12 threads sorted by accumulated `cpuTime_us`, plus the
total thread count and uptime. Implemented via libphoenix's
`threadsinfo(int n, threadinfo_t *info)` — an existing syscall used
by psh's `ps` command.

Two probes 10 s apart on an otherwise-idle Phoenix Pi 4 boot give a
direct cpuTime delta per thread. The key observable: the kernel
spawns one `[idle]` thread per CPU (named the same, distinguished by
tid), and each accumulates cpuTime only when its own CPU has no
real work to dispatch.

## Result

Image SHA `38b9ec8c…` (lwip head `f5687ad` on `agent/rpi4-genet`).

```
$ echo t | nc -u -w 1 10.42.0.99 9999    # at uptime ~20.7s
PHX-DIAG/1 threads
thread: pid=0 tid=2 load=637 cpuTime_us=13220717 name=[idle]
thread: pid=0 tid=0 load=636 cpuTime_us=13211258 name=[idle]
thread: pid=0 tid=1 load=636 cpuTime_us=13196267 name=[idle]
thread: pid=0 tid=3 load=635 cpuTime_us=13192320 name=[idle]
...
uptime_ms: 20701

# 10 s later
$ echo t | nc -u -w 1 10.42.0.99 9999    # at uptime ~31.8s
PHX-DIAG/1 threads
thread: pid=0 tid=2 load=763 cpuTime_us=24323749 name=[idle]
thread: pid=0 tid=0 load=763 cpuTime_us=24312063 name=[idle]
thread: pid=0 tid=1 load=762 cpuTime_us=24298548 name=[idle]
thread: pid=0 tid=3 load=762 cpuTime_us=24296124 name=[idle]
...
uptime_ms: 31814
```

| Idle thread | Δ cpuTime | % of wall-clock |
|-------------|----------:|----------------:|
| tid=0       | 11.101 s  | 99.9%           |
| tid=1       | 11.102 s  | 99.9%           |
| tid=2       | 11.103 s  | 99.9%           |
| tid=3       | 11.104 s  | 99.9%           |
| **Σ**       | **44.41 s** | **~400%**     |

Δ wall-clock = 11.113 s. Sum of per-thread Δ cpuTime = 44.41 s ≈
4 × wall-clock.

## Interpretation

The "sum of per-cpu idle cpuTime ≈ N × wall-clock" identity is the
defining property of N independent schedulers running on N CPUs. If
only CPU 0 ran the scheduler, the other three idle threads would be
parked in WFI and accumulate zero cpuTime — total would be ≤ 1×
wall-clock. The 4× ratio is achievable only if all four cores
independently pick `[idle]` (their own) when no other thread is
ready. That is precisely Phase D's full SMP scheduling working.

Two corollaries:

1. **Idle-load balance is symmetric.** All four idle threads grew
   within ~3 ms of each other across 11.1 s. The scheduler does not
   "favor" CPU 0 in any observable way at idle.
2. **Non-idle dispatch is not yet directly proven**, but is implied.
   The lwip tcpip thread accumulated 6.6 ms across the interval and
   the pl011-tty thread accumulated 32 ms; both ran somewhere, and
   given the 4× idle pattern, dispatch must have happened on at
   least some non-CPU-0 core to keep idle balance roughly equal.
   A follow-up experiment could pin a CPU-bound workload to confirm
   each CPU can be saturated — but the idle measurement is already
   a clean SMP-positive signal.

## Followup ideas (not blocking)

- Add a per-thread `cpu` field to `threadinfo_t` that names the CPU
  the thread last ran on. Phoenix's scheduler tracks this internally
  (used for affinity hints); plumbing it into `threadsinfo()` makes
  cross-CPU distribution measurable directly per thread.
- Add an explicit 4-thread busy-loop workload (e.g. a small
  `cpuload` binary under `phoenix-rtos-utils`) to validate non-idle
  load distribution under saturation.

## Manifest

`manifests/2026-05-25-smp-phase-e-validated.md`. lwip head `f5687ad`
on `agent/rpi4-genet`; all other siblings unchanged from the Tier 5c
snapshot.
