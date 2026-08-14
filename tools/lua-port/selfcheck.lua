-- Self-validating Lua 5.4 feature check. Prints "ALL-OK" or asserts out.
-- Run: lua /selfcheck.lua   (psh-safe: script from a file, no -e quoting)
local function eq(a,b,msg) if a~=b then error((msg or "?")..": got "..tostring(a).." want "..tostring(b),2) end end

-- integers vs floats (5.4)
eq(math.type(3), "integer", "int type")
eq(math.type(3.0), "float", "float type")
eq(7 // 2, 3, "floordiv int")
eq(7 % 3, 1, "mod")
eq(2^10, 1024.0, "pow float")
eq(math.maxinteger + 1, math.mininteger, "int wrap")

-- bitwise (5.4)
eq(0xF0 & 0x0F, 0, "band"); eq(0xF0 | 0x0F, 0xFF, "bor")
eq(5 ~ 3, 6, "bxor"); eq(1 << 4, 16, "shl"); eq(256 >> 4, 16, "shr")

-- strings + patterns
eq(("hello"):upper(), "HELLO", "upper")
eq(string.format("%d-%s-%.2f", 3, "x", 1.5), "3-x-1.50", "format")
eq(select(2, ("a1b2c3"):gsub("%d","")), 3, "gsub count")
eq(("key=val"):match("(%w+)=(%w+)"), "key", "match cap")
eq(#"héllo", 6, "byte length"); eq(utf8.len("héllo"), 5, "utf8 len")

-- tables + sort + closures
local t = {5,3,1,4,2}; table.sort(t); eq(table.concat(t,","), "1,2,3,4,5", "sort")
local function counter() local n=0; return function() n=n+1; return n end end
local c = counter(); c(); c(); eq(c(), 3, "closure")

-- metatables
local V = setmetatable({}, {__index=function() return 42 end})
eq(V.anything, 42, "metatable __index")
local A = setmetatable({1}, {__add=function(a,b) return a[1]+b end})
eq(A+8, 9, "metatable __add")

-- coroutines
local co = coroutine.wrap(function() for i=1,3 do coroutine.yield(i*i) end end)
eq(co()+co()+co(), 14, "coroutine")

-- pcall / error
local ok,err = pcall(function() error("boom") end)
eq(ok, false, "pcall ok"); eq(err:match("boom"), "boom", "pcall err")

-- varargs + reduce
local function sum(...) local s=0; for _,v in ipairs({...}) do s=s+v end; return s end
eq(sum(1,2,3,4,5), 15, "varargs")

-- string.pack/unpack (5.3+)
local packed = string.pack("<I4", 0x01020304)
eq(string.unpack("<I4", packed), 0x01020304, "pack/unpack")

-- tostring/tonumber round-trip + goto
eq(tonumber("0x1A"), 26, "tonumber hex")
eq(tonumber("101", 2), 5, "tonumber base2")
local i,acc = 0,0
::top:: i=i+1; acc=acc+i; if i<5 then goto top end
eq(acc, 15, "goto loop")

print("ALL-OK")
