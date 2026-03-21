# Manifest: Resolve Path `/dev` Seam

- Date: `2026-03-21`
- Step: `STEP-0252`
- Focus: identify the exact node where `resolve_path("/dev/console")` fails on
  the fast lane

## Scope

- inspect `resolve_path()` / `_resolve_abspath()` source in `libphoenix`
- use bounded QEMU gdbstub stops on the generic fast lane to distinguish:
  - the `stat()`-side `resolve_path()` call
  - the direct `open()`-side `resolve_path()` call

## Key GDB Result

- first `resolve_path()` call:
  - return site: `stat()`
  - failure branch:
    - `errno = ENOENT`
    - `partial = "/dev"`
    - `is_leaf = 0`
    - `allow_missing = 0`
- second `resolve_path()` call:
  - return site: `open()`
  - failure branch:
    - `errno = ENOENT`
    - `partial = "/dev"`
    - `is_leaf = 0`
    - `allow_missing = 1`

## Source Correlation

- `sources/libphoenix/unistd/dir.c`
  shows that `allow_missing_leaf` only tolerates `ENOENT` for the final leaf
  node
- because the failing node is the intermediate `/dev` directory, both
  `stat("/dev/console")` and `open("/dev/console")` must fail before the
  `console` leaf is considered
- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
  shows that the root `dummyfs-root` instance auto-populates only `/syspage`
- the same file shows that `dummyfs;-N;devfs;-D` registers `devfs` by name in a
  non-filesystem namespace, not at `/dev`

## Conclusion

- the shared blocker is now narrower than “console path broken”
- the exact current fast-lane failure is:
  `lookup("/dev") -> -ENOENT`
- the next smallest fix should operate in project image/startup composition:
  make `/dev` exist in the filesystem namespace and bind `devfs` there before
  launching the shell
