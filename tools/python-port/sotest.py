try:
    import spam
    r = spam.add(3, 4)
    print("SO-EXT add(3,4)=%r file=%s" % (r, getattr(spam, "__file__", "?")))
    print("SO-EXT-OK" if r == 7 else "SO-EXT-FAIL")
except Exception as e:
    print("SO-EXT-FAIL exc:", repr(e))
