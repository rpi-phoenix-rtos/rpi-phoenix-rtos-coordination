import sys
import array, struct, json, math, heapq, bisect, select, socket, csv, pickle, random, statistics
# array + struct (binary data)
a = array.array('i', [1,2,3]); assert a.tolist() == [1,2,3], "array"
assert struct.unpack('<I', struct.pack('<I', 0x01020304))[0] == 0x01020304, "struct"
# json
assert json.dumps({'x':[1,2]}, separators=(',',':')) == '{"x":[1,2]}', "json.dumps"
assert json.loads('{"a": 1, "b": [true, null]}') == {'a':1,'b':[True,None]}, "json.loads"
# math (+ the nextafter libm fix)
assert math.gcd(12,18) == 6 and math.factorial(5) == 120, "math int"
assert abs(math.sqrt(2) - 1.4142135623730951) < 1e-15, "math.sqrt"
assert math.nextafter(1.0, 2.0) == 1.0000000000000002, "math.nextafter"
assert math.isclose(math.hypot(3,4), 5.0), "hypot"
# heapq / bisect
h = [3,1,2]; heapq.heapify(h); assert heapq.heappop(h) == 1, "heapq"
assert bisect.bisect([1,3,5,7], 4) == 2, "bisect"
# pickle round-trip
assert pickle.loads(pickle.dumps({'k':[1,2,3]})) == {'k':[1,2,3]}, "pickle"
# csv
import io
buf = io.StringIO(); w = csv.writer(buf); w.writerow(['a','b',1]); assert buf.getvalue().strip() == 'a,b,1', "csv"
# random (seeded, deterministic)
random.seed(42); assert 0 <= random.random() < 1, "random"
# statistics
assert statistics.mean([1,2,3,4]) == 2.5, "statistics"
# socket module loads + creates an fd (lwip)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); fd = s.fileno(); s.close()
assert fd >= 0, "socket"
print("MODULES-OK array struct json math heapq bisect pickle csv random statistics socket")
print("PYVER", sys.version.split()[0])
print("ALL-OK")
