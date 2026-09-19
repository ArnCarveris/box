# Render thread stall at frame 270

## What it was

`BoxSurfaceAcquire` waits for the render thread, and the render thread spent that frame in
`box_bake_update`. `box_bake_state_take` measured the distance of every job in the probe dirty
set and `qsort`ed the whole set before it took the budget off the front. Incremental template
instantiation grows the static bounds as the camera travels, `box_bake_update_grid` rebuilds the
voxel grid and re-queues all 50k instances, so that frame sorted a fresh 49k set: renderer frame
358 took 4.80 ms, 4.40 ms of it in the bake and 4.02 ms of that in the sort. The same happened at
frame 79 and cost about a millisecond every frame the set was large.

## What changed

`box_bake_state_take` now orders only as far as the budget reaches. One pass gives every job its
distance and drops the dead ones, a randomized quickselect moves the nearest run to the front, the
run is sized from the probes an instance was measured to carry, and only a run that reaches past
the budget is sorted inside itself. Nothing else about which probes bake changed.

## Verified

Both `.args` back to `--spike 8`. `tools/integration.py bench` passes all 12 scenes: first_person_1m
mean 0.89 -> 0.39 ms, max 6.42 -> 2.23 ms; picking_1m mean 0.77 -> 0.62 ms, max 8.19 -> 2.43 ms
(`first_person_1m.json`, `picking_1m.json`). Worst `box_bake_update` frame 4.40 -> 0.74 ms.
375 `test/render` cases, 2 of them new on the selection; 84 visual scenes unchanged; lint clean.

## Left out

The grid rebuild still re-queues every probe; that costs 0.3 ms and drains under the budget.
`bench/render` has no case on `box_bake_state_take` because it is outside this task's files.
picking_1m's remaining tail is `wgpuCommandEncoderFinish` in the driver, 5 ms on occasional
frames, which predates this task.
