PRAGMA locking_mode=EXCLUSIVE;
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v TEXT);
DELETE FROM t;
INSERT INTO t(v) VALUES ('phoenix-wal-1'),('phoenix-wal-2'),('phoenix-wal-3');
SELECT 'JMODE='||journal_mode FROM pragma_journal_mode;
SELECT 'WRITECOUNT='||count(*) FROM t;
PRAGMA wal_checkpoint(FULL);
SELECT 'INTEGRITY='||integrity_check FROM pragma_integrity_check;
.quit
