# Hover highlight scene

## Built

`test/integration/visual/picking_hover.flecs`: five boxes in a row, each mixing in a
`Hoverable` template whose `async` task awaits `on.hover(this)` and reactively sets a `Tint`
while it has fired. A `Terminal`/`TerminalInput` sweeps the cursor from off screen onto the
middle box over two frames, no `.cmd` file needed.

## Verified

Baselined with `tools/integration.py visual --update-baseline picking_hover`; the middle box
now renders tinted yellow. Every other visual scene still matches, and
`test/integration/command/picking.flecs` still passes. `test/picking` (39 tests) and lint (0)
unaffected.

## Left out

A hover that already holds on the first frame is missed: the pick fires before the template's
async task has registered its waiter, so the cursor must arrive after at least one frame off
the target, which is why the scene sweeps in rather than starting on the box. That one frame
race in `box.picking`/`box.script` scheduling is follow up work outside this task's files.
