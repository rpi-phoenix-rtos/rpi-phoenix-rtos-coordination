PRAGMA locking_mode=EXCLUSIVE;
SELECT 'JMODE2='||journal_mode FROM pragma_journal_mode;
SELECT 'READCOUNT='||count(*) FROM t;
SELECT 'READROW='||v FROM t WHERE id=2;
SELECT 'INTEGRITY2='||integrity_check FROM pragma_integrity_check;
.quit
