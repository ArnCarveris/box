# Hover present on the first frame

## Built

`box_pick_hover` in `src/picking/pick.c` marks `hover_missed` when the raise that moves
`Hovered` onto an entity finds no waiter. While the hovered entity stays the same, the
update keeps retrying that one raise and clears the flag the first time it reaches a
waiter, so a handler mixed in after the hit still fires once, with no need for the hit
to change again. Press, release, click and drag are untouched, still edge triggered.

## Verified

A reproducer in `test/picking` hovers a plain entity, then reopens it from a script to
add the `on.hover` handler; it failed before the fix and passes after. `test/picking`
(40 tests), `tools/integration.py command` and the unchanged `picking_hover` visual
scene (exact match) all pass. Lint 0. `bench/picking` still under budget.

## Left out

The stale spatial index for an entity that moves under a still cursor, noted by
`picking/picking`, is unrelated and still open.
