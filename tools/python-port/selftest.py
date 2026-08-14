import sys, os
assert sum(range(100)) == 4950
assert [x*x for x in range(5)] == [0,1,4,9,16]
assert sorted({'b':2,'a':1}) == ['a','b']
assert "héllo".upper() == "HÉLLO"
assert list(map(lambda x: x+1, [1,2,3])) == [2,3,4]
assert list(i for i in range(6) if i%2==0) == [0,2,4]
try:
    1/0
except ZeroDivisionError:
    pass
else:
    raise SystemExit("fail-zde")
assert os.getpid() > 0
class A:
    def __init__(self, x): self.x = x
    def dbl(self): return self.x*2
assert A(21).dbl() == 42
assert {**{'a':1}, 'b':2} == {'a':1,'b':2}
print("PYVER", sys.version.split()[0])
print("ALL-OK")
