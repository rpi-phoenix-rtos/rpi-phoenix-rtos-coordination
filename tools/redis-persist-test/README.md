# Redis RDB persistence — HW test (Phoenix-RTOS RPi4)

Closes the §C4 "Redis persistence" deferred feature (owner A20). Redis 7.2.4 was
already HW-verified in-memory (str/int/list/hash/set/expiry over lwip TCP); this
proves **RDB persistence** across a real server restart on the Pi.

- `redis-persist.conf` — `save 3600 1`, `dir /`, `dbfilename dump.rdb` (persistence
  on; the shipped `redis-min.conf` has `save ""`).
- `redis-persist-test.sh` — run on the Pi (`bash /redis-persist-test.sh`): starts
  redis-server (bg), SET/RPUSH/HSET, SAVE, SHUTDOWN, then RESTARTS redis-server
  (which loads dump.rdb) and verifies the data reloaded from disk.

HW result (2026-08-21, netboot, label redis-persist2): SAVE wrote /dump.rdb
(178 B, "Redis RDB file, version 0011", persisted to the NFS-backed disk); the
restarted server logged "Loading RDB produced by version 7.2.4" + "DB loaded from
disk"; and post-restart GET pkey=phoenix-persist-value, LRANGE plist=a b c d,
HGETALL phash=f1 v1 f2 v2, DBSIZE=3 — all reloaded from RDB. 0 faults. (Bonus:
non-interactive bash job control `&` + daemon restart work on Phoenix.)
