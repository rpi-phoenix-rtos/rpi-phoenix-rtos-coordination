# Code Quality and Upstreaming

This document defines the coding-quality bar for the Phoenix RTOS Raspberry Pi port.

The goal is not just "working code". The goal is code that is:

- readable
- small
- consistent with existing Phoenix sources
- easy to review
- likely to be accepted upstream once mature

This document is based on direct inspection of current Phoenix sources in:

- `phoenix-rtos-kernel`
- `plo`
- `phoenix-rtos-devices`
- `phoenix-rtos-build`

## 1. Core Rule

Every change should optimize first for:

1. correctness
2. readability
3. consistency with surrounding Phoenix code
4. minimal patch size
5. upstream reviewability

Do not optimize for cleverness, abstraction density, or personal style.

## 2. Phoenix Style Signals Observed In Upstream Code

The current upstream codebase consistently shows these patterns:

- Phoenix file header blocks at the top of C and header files
- tabs used for indentation in C sources
- short helper functions instead of large multi-purpose routines
- explicit, direct control flow
- register layouts and magic-index enums kept close to the code that uses them
- `static const` tables for fixed platform metadata
- limited comments, but useful comments around non-obvious hardware behavior
- `clang-format off/on` used only around inline assembly, register maps, or tightly structured tables
- low-level code kept close to the hardware rather than hidden behind deep abstraction stacks
- warning-clean builds matter because Phoenix build flags already include `-Wall`, `-Wstrict-prototypes`, `-Wundef`, and `-Werror`
- some kernel code carries Parasoft / MISRA suppression comments; these are used narrowly and with explicit justification

Implication:

- new Raspberry Pi code should look like a natural extension of nearby Phoenix code
- avoid introducing a noticeably different house style

## 3. Mandatory Coding Rules For This Project

### 3.1 Match surrounding style first

For every file you touch:

- follow the style already present in that directory and neighboring files
- use the same include ordering pattern used nearby
- use the same naming patterns used by that subsystem
- keep data structures and helper function layout consistent with surrounding code

### 3.2 Keep code short and local

Prefer:

- small helper functions
- explicit hardware steps
- local tables near the code that uses them
- one concept per patch where practical

Avoid:

- broad framework inventions before they are clearly needed
- deep abstraction layers for a single board
- large files containing several unrelated bring-up topics
- helper layers that make register-level behavior harder to audit

### 3.3 Comment only where the reader would otherwise lose time

Good comments:

- explain hardware sequencing requirements
- explain firmware dependencies
- explain why a register write order matters
- explain a subtle DTB or memory-layout assumption
- justify a MISRA or low-level suppression

Bad comments:

- narrate obvious assignments
- restate the function name
- add generic prose around simple code

### 3.4 No gratuitous reformatting

Do not:

- reformat whole files just because they are touched
- change indentation or wrapping unrelated to the functional change
- mass-rename symbols for aesthetics during bring-up work

If formatting is needed:

- keep it scoped to the changed region
- ensure the result still matches the surrounding Phoenix style

### 3.5 Keep diffs easy to review

A good implementation step should usually be reviewable in one sitting.

Preferred patch shapes:

- one subsystem
- one board primitive
- one driver stage
- one build-target addition
- one test-target addition

If a change becomes too wide, split it.

## 4. Upstreamability Rules

### 4.1 Prefer reusable AArch64 work over Raspberry Pi-only hacks

Whenever a problem is really a common AArch64 problem:

- solve it in common AArch64 code
- do not hide it inside `rpi4` or `rpi5` directories

Examples:

- generic DTB parser improvements
- generic architectural timer handling
- generic platform hook cleanup

### 4.2 Transitional hacks must be marked

If a temporary workaround is necessary:

- document it as transitional
- explain what the final design should replace it with
- avoid letting temporary debug aids become architecture

Examples:

- firmware-preserved Pi 5 RP1 state
- early hardcoded addresses before full DT parsing exists

### 4.3 Keep board code boring

Board-specific code should be:

- explicit
- unsurprising
- small
- mostly glue to common mechanisms

Do not make board files into mini-frameworks.

### 4.4 Use existing Phoenix infrastructure where it already fits

Before adding a new mechanism, check whether the same role already exists in:

- `plo`
- Phoenix HAL structure
- `phoenix-rtos-project` target composition
- `phoenix-rtos-tests` harnesses
- existing driver library code in `phoenix-rtos-devices`

## 5. Quality Gates For Each Successful Step

A step should not be treated as complete until the following are true.

### Mandatory

- builds pass with the existing Phoenix warning policy
- no new warnings are introduced in touched code
- the change is validated on the strongest relevant test lane
- the patch is small enough to explain clearly
- docs are updated if the step introduced new assumptions or operator requirements
- the change is committed in the touched upstream repository or repositories

### Strongly recommended

- a second pass is made to simplify names, control flow, or helper boundaries before committing
- dead debug prints and temporary scaffolding are removed before the step is closed
- unrelated edits are removed from the diff

## 6. Recommended Review Checklist

Before committing, check:

- does the code match nearby file style?
- can any helper be made shorter?
- is each function doing one thing?
- are register values and bit meanings clear enough?
- is any abstraction hiding simple hardware behavior unnecessarily?
- are error and cleanup paths easy to follow?
- are there any temporary debug paths that should not be committed?
- would an upstream maintainer understand why this change belongs in this layer?

## 7. Tooling Policy

Use tools to reinforce quality, not to fight the codebase.

### 7.1 Mandatory tools and checks

- normal Phoenix build with its warning flags
- target-specific build verification
- relevant emulator or hardware tests
- `git diff --check` before committing

### 7.2 Recommended tools

- `clang-format` in tightly scoped form only
- `rg` for consistency checks across similar drivers
- compiler warnings as the first line of static analysis

### 7.3 Optional advisory tools

These can be useful, but they are not the primary source of truth:

- `clang-tidy`
- `cppcheck`
- spell-check or doc lint tools for documentation

Use them selectively when they help, and do not let them drive large style churn.

## 8. Formatting Guidance

If future sessions introduce formatting automation, it should follow these rules:

- never reformat entire upstream repositories
- do not run a repo-wide formatter pass as part of bring-up
- if a local `.clang-format` is introduced later, validate it against existing Phoenix style first
- use `clang-format off/on` only where the existing codebase already uses that pattern

## 9. Additional Planning Steps For Quality

The implementation plan should include these explicit quality-oriented steps.

### Phase 0 additions

- establish a pre-commit quality checklist for the touched repos
- verify how closely future code matches neighboring Phoenix style before large feature work begins
- record any subsystem-specific style or review conventions discovered upstream

### Per-milestone additions

For every milestone:

1. implement the smallest coherent change
2. build with Phoenix warning policy
3. run the fastest relevant tests
4. simplify the patch before committing
5. commit only after the diff is reviewable and stylistically consistent

## 10. When To Add New Quality Rules

Update this file if future sessions discover:

- an upstream Phoenix coding-style document
- maintainers' review preferences
- subsystem-specific naming or layering conventions
- a useful and low-noise static-analysis workflow
- a formatting rule that is consistently followed across the touched subsystem
