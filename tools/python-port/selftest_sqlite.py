import sqlite3
con = sqlite3.connect(":memory:")
cur = con.cursor()
cur.execute("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL)")
cur.executemany("INSERT INTO t(name,score) VALUES(?,?)", [("alice",3.5),("bob",7.25),("carol",1.0)])
con.commit()
cur.execute("SELECT name,score FROM t ORDER BY score DESC")
assert cur.fetchall() == [("bob",7.25),("alice",3.5),("carol",1.0)], "order"
cur.execute("SELECT COUNT(*), SUM(score) FROM t")
c,s = cur.fetchone(); assert c==3 and abs(s-11.75)<1e-9, (c,s)
cur.execute("SELECT name FROM t WHERE name LIKE 'a%'")
assert cur.fetchall() == [("alice",)], "like"
# a small transaction rollback test
cur.execute("BEGIN"); cur.execute("DELETE FROM t"); con.rollback()
cur.execute("SELECT COUNT(*) FROM t"); assert cur.fetchone()[0] == 3, "rollback"
print("sqlite_version", sqlite3.sqlite_version)
con.close()
print("ALL-OK")
