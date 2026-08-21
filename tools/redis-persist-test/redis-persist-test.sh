#!/bin/bash
# Redis RDB persistence: SAVE then a real server restart proves cross-restart reload.
echo "RP: run1 redis-server (bg)"
redis-server /redis-persist.conf &
sleep 6
redis-cli SET pkey "phoenix-persist-value"
redis-cli RPUSH plist a b c d
redis-cli HSET phash f1 v1 f2 v2
echo "RP: SAVE"
redis-cli SAVE
ls -l /dump.rdb
echo "RP: SHUTDOWN run1 (keep dump.rdb)"
redis-cli SHUTDOWN NOSAVE 2>/dev/null
sleep 4
echo "RP: run2 redis-server (bg) — should load /dump.rdb on startup"
redis-server /redis-persist.conf &
sleep 6
echo "RP: verify after RESTART (data must come from dump.rdb, not memory)"
echo "GET pkey -> $(redis-cli GET pkey)"
echo "LRANGE plist -> $(redis-cli LRANGE plist 0 -1 | tr '\n' ' ')"
echo "HGETALL phash -> $(redis-cli HGETALL phash | tr '\n' ' ')"
echo "DBSIZE -> $(redis-cli DBSIZE)"
redis-cli SHUTDOWN NOSAVE 2>/dev/null
echo "REDIS-PERSIST-DONE"
