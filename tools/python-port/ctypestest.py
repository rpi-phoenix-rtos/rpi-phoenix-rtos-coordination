# _ctypes / ctypes smoke test on Phoenix. Granular so we see which layer works.
# Prints CTYPES-OK if forward FFI calls work, else shows which stage failed.
import sys
ok = True
def chk(cond, label):
    global ok
    print(("  ok  " if cond else " FAIL ") + label); ok = ok and cond
try:
    import _ctypes
    import ctypes
    from ctypes import c_int, c_double, c_char_p, c_size_t, Structure, sizeof, CDLL, POINTER, byref
    # --- 1. type system (no FFI call) ---
    chk(sizeof(c_int) == 4, "sizeof(c_int)==4")
    chk(sizeof(c_double) == 8, "sizeof(c_double)==8")
    class Pt(Structure):
        _fields_ = [("x", c_int), ("y", c_int)]
    p = Pt(3, 4)
    chk(p.x == 3 and p.y == 4 and sizeof(Pt) == 8, "Structure field access")
    arr = (c_int * 4)(10, 20, 30, 40)
    chk(arr[2] == 30 and len(arr) == 4, "array type")
    # --- 2. pointer / byref (no FFI call) ---
    v = c_int(99); pv = ctypes.pointer(v)
    chk(pv.contents.value == 99, "pointer deref")
    # --- 3. forward FFI call into statically-linked libc via self handle ---
    try:
        libc = CDLL(None)                 # handle to the main program (dlopen(NULL))
        libc.strlen.argtypes = [c_char_p]; libc.strlen.restype = c_size_t
        chk(libc.strlen(b"phoenix") == 7, "FFI call libc.strlen(b'phoenix')==7")
        libc.strcmp.argtypes = [c_char_p, c_char_p]; libc.strcmp.restype = c_int
        chk(libc.strcmp(b"abc", b"abc") == 0, "FFI call libc.strcmp equal==0")
        chk(libc.strcmp(b"abc", b"abd") < 0, "FFI call libc.strcmp less<0")
        libc.getpid.restype = c_int
        chk(libc.getpid() > 0, "FFI call libc.getpid()>0 (no-arg)")
    except Exception as e:
        chk(False, "FFI forward call: " + repr(e))
    print("CTYPES-OK" if ok else "CTYPES-PARTIAL")
except Exception as e:
    print("CTYPES-FAIL exc:", repr(e))
