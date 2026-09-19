# Picking and script events

## Built

`box.picking`. `box_picking_ray` turns a normalized screen position into a world ray through the
camera of a view (perspective or orthographic, aspect from its render target or the surface),
`box_picking_pick` answers it from the spatial index of `box.collision` and `box_picking_target`
resolves the collider to the prefab or template root, unless it or an entity below that root
carries `Pickable`. `PickHit` carries the entity, collider, point, normal and cursor,
`PickSettings` the reach, drag threshold and switch, and the entity under the cursor carries
`Hovered`. `BoxPickingUpdate` runs in OnLoad after the input systems, derives hover, press,
release, click and drag from the button state, and runs one ray query per enabled view that
renders into the surface and none in a frame with a still cursor. `on.hover`, `on.press`,
`on.release`, `on.click` and `on.drag` are async script functions that return the hit; an event
starts at the entity the pick resolved to and bubbles up the parent chain until a handler
consumes it.

## Verified

38 tests in `test/picking` (Ray 11, Pick 13, Events 14): ray construction, nearest hit, root
versus `Pickable` targeting, view priority, the five events, bubbling and consumption, the query
budget and waiter cancel. `test/integration/command/picking.flecs` sweeps the cursor onto a crate
and clicks it through the `mouse.*` commands, its log showing the click bubble up to `scene`.
`bench/picking` picks a million nested colliders in 13.9 us against the 1 ms budget. Lint 0;
`test/core`, collision, input, view, script and every command scene green.

## Left out

No visual scene, `test/integration/visual` is not owned by this task. Only the left button drives
the events, and a cached hit is not invalidated when an entity moves under a still cursor.
