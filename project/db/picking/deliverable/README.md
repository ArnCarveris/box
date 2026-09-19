# Picking: completion report

## What was built
- `box.picking`: `box_picking_ray` turns a normalized screen position into a world ray through the camera of a view (perspective or orthographic, aspect from its render target or the surface), `box_picking_pick` answers it from the spatial index of `box.collision`, and `box_picking_target` resolves the collider to its prefab or template root unless it or an entity below the root carries `Pickable`.
- `PickHit` (entity, collider, point, normal, cursor), `Hovered` on the entity under the cursor and `PickSettings` (reach, drag threshold, switch). `BoxPickingUpdate` runs in OnLoad after input, derives hover, press, release, click and drag from the button state, costs one ray query per surface view while the cursor moves and nothing while it is still.
- `on.hover`, `on.press`, `on.release`, `on.click` and `on.drag` are async script functions returning the hit; an event starts at the resolved entity and bubbles up the parent chain until a handler consumes it. A hover that finds no waiter is retried while the hovered entity stays the same, so a handler mixed in after the hit still fires once.
- `apps/box_bench` imports input and picking, so bench scenes pick for real.

## Verification
- 1372 unit tests across 16 projects, all passing; `test/picking` is new with 40 tests over the `Events`, `Pick`, `Ray` suites.
- Scenes in `integration/`: `picking_hover` sweeps the cursor onto the middle of five boxes and the `on.hover` handler tints it; `picking.flecs` in the command set clicks a crate through `mouse.*` commands and logs the click bubbling up to the scene; `picking_1m.json` records 50k roots with a million colliders picked from a flying camera at 0.5 ms a frame; every earlier scene bit identical.
- Benchmarks in `bench/` against the collision epic: a pick over a million nested colliders costs 13.4 µs against the one millisecond budget and stays flat with the leaf count.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Re-raising hover every frame while nothing changes resolves a looping async waiter forever; the single catch-up flag that clears on the first delivered raise is what keeps the command scene stable.
- A template instantiated in the frame of the first hit registers its future after the raise, so every edge triggered event needs a late waiter story like `on.pressed` in the input module.
- A template async block with two sequential awaits restarts on a `mut` reassignment; one async block per await.

## Not in this epic
- Only the left button drives the events, and a cached hit is not invalidated when an entity moves under a still cursor.
- Picking through the UI view and consuming events before they reach the scene is the UI events task.
