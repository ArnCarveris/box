# One propagation model for input, picking and UI

## Built
`box_picking_raise` (`src/picking/events.c`) now delivers against the `Raise` record of the
world, one node at a time from the hit up its parents, with `target` the node, `source` the
hit entity and `priority` `BOX_PICK_PRIORITY`. A node takes the event when a script handler
of it resolves (a task resumes after the delivery, so it cannot stop the raise itself), when
a click lands on a node with an `Action` (the action fires and takes the click, so an
ancestor no longer hears a click a button answered), or when `box_event_stop` runs inside the
delivery; a taken event reaches no ancestor. `input.consume(action)` from a task that resumed
from a click takes the action it fired for the rest of the frame, as a key handler does.
The same record and rules serve the input maps, the UI nodes and the scene, and
`include/box/input.h` states the order: highest map, deepest UI node, scene.

A UI element keeps an event from the scene behind it, as before. A tree that sets
`Ui.passthrough` lets an event no node took go on: the pick keeps the ray hit of the views
behind the element (`PickState.fallback`, one more query only for such trees, decided by a
`box_picking_through_t` box.ui installs next to its source) and raises the event there; the
element keeps `Hovered` and `UiCapture`, so the pointer still counts as on the UI.

## Verified
`test/picking` Events: a click on a child with an Action no longer reaches the parent's
handler, a child without one still bubbles; `test/ui` Events: an untaken hover falls through
a passthrough tree to the scene ball, a handler on the element keeps it, a task resumed from
a click consumes its action. `test/ui` 246, `test/picking` 53, `test/input` and
`test/command` green; every visual and command scene unchanged.

## Left out
No native handler registry in box.picking: `box_event_stop` inside a pick delivery is only
reachable from code the delivery calls (the action emit), a script task cannot stop
synchronously and takes the event by resolving. `bench: false`, the extra query runs only
for passthrough trees.
