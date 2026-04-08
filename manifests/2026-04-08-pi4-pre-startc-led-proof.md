# 2026-04-08 Pi 4 Pre-`_startc()` LED Proof

## Scope

Move the next persistent GPIO42 state change earlier than `_startc()`, into
late `plo` `hal/aarch64/generic/_init.S`, so the next board retry can answer
whether the primary core reaches late `plo` before branching to `_startc()`.

## Starting Point

The previous committed image had already proved:

- the custom Pi 4 armstub executes on real hardware
- `_startc()` is not yet proven

The next smallest question was therefore:

- does the real board reach late `plo` `_init.S` on the primary-core path?

## Temporary Diagnostic Change

The temporary image carried:

- the existing armstub GPIO42-high proof unchanged
- a temporary GPIO42-low transition in late `plo`
  `hal/aarch64/generic/_init.S`, just before the branch to `_startc()`

This change was introduced only as a bounded hypothesis probe.

## Board Result

The real Pi 4 retry on that temporary image produced:

- both red and green LEDs on
- blank screen
- no keyboard-visible reaction

## Conclusion

That result proves:

- the board still does not reach the late `plo` `_init.S` split point
- the failure window is narrower than before:
  - later than the custom armstub entry proof
  - earlier than late `plo` on the primary-core path

Because the hypothesis was false, the temporary `plo` `_init.S` probe was
removed instead of being committed.

## Temporary Artifact

- temporary exported image SHA-256:
  `5691dd9db1eb018ff131e00c9b2b2df08ec585e9acc9cca68ff3b7056660bc26`

## Next Step

Move the next persistent GPIO42 transition back into the custom armstub, at
the final primary-core handoff point just before the branch to `kernel8.img`.
