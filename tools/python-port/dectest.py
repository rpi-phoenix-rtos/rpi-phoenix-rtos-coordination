# _decimal / decimal stdlib smoke test on Phoenix. Prints DECIMAL-OK / DECIMAL-FAIL.
import sys
try:
    import _decimal
    from decimal import Decimal, getcontext, ROUND_HALF_UP
    ok = True
    def chk(cond, label):
        global ok
        print(("  ok  " if cond else " FAIL ") + label)
        ok = ok and cond
    # 1. exactness float cannot do: 0.1 + 0.2 == 0.3 exactly
    chk(Decimal('0.1') + Decimal('0.2') == Decimal('0.3'), "0.1+0.2 == 0.3 (exact)")
    chk((0.1 + 0.2) != 0.3, "float 0.1+0.2 != 0.3 (contrast)")
    # 2. arbitrary precision division
    getcontext().prec = 50
    q = Decimal(1) / Decimal(7)
    chk(str(q).startswith("0.14285714285714285714285714285714285714285714285714"), "1/7 to 50 digits")
    # 3. big integer exactness
    chk(Decimal(2) ** 100 == Decimal("1267650600228229401496703205376"), "2**100 exact")
    # 4. rounding modes
    chk(Decimal('2.5').quantize(Decimal('1'), rounding=ROUND_HALF_UP) == Decimal('3'), "ROUND_HALF_UP 2.5->3")
    # 5. is _decimal the C accelerator (not the pure-python fallback)?
    chk(Decimal("1").__class__.__module__ == "decimal", "Decimal class present")
    print("DECIMAL-OK" if ok else "DECIMAL-FAIL")
except Exception as e:
    print("DECIMAL-FAIL exc:", repr(e))
