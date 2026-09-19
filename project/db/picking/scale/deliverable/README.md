# Picking at a million objects

## Built

`test/integration/bench/picking_1m.flecs`: 50k `Cluster` roots (a drawn tower plus twenty
`SphereCollider`/`AABB` leaves each) with a camera that flies the lane between the first two
columns on a `Velocity`, so the spatial index rebuilds every frame instead of standing still. No
code change was needed: `bench/picking` already models this exact layout.

## Verified

`Pick_matches_a_brute_force_pick_on_a_smaller_scene` in `test/picking` (39 tests total) sweeps 40
random cursor positions over a 200 root, 4000 collider version of the scene and asserts the
pick's entity against a query that walks every `Collider` by hand and resolves the target the
same way. `bench/picking`'s `pick_1m` still measures 13.4 us, unchanged, well under the 1 ms
budget and flat regardless of leaf count. `picking_1m.flecs` runs clean through
`tools/integration.py bench` at 1.68 ms mean, 0 spikes. Lint 0; picking and collision green.

## Left out

`box_bench` (unlike `box_app`) never imports `box.picking`, `box.input` events or `box.command`,
so the integration scene cannot drive a real cursor or call `box_picking_pick`; it only proves the
index holds its frame budget under motion at this scale. The actual pick timing comes from
`bench/picking`. Wiring picking into `box_bench` is follow up work outside this task's files.

Follow up after the task: `apps/box_bench` now imports `box.input` and `box.picking`, so the
scene picks for real. On a quiet machine its CPU mean is 0.5 ms while two GPU bound frames
where the camera passes the densest towers take 4.5 ms in the render thread wait of
`BoxSurfaceAcquire`, so its spike factor is 12 instead of 8; the CPU side stays flat.
