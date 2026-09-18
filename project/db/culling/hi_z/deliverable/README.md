# Hi-Z occlusion culling

Every view draws its compute culled static batches in two passes. The first
keeps what the depth pyramid of the previous frame accepted, tested with the
matrix of that frame; its depth is halved into an `r32float` mip chain with the
reverse Z rule of `src/renderer/hiz.h` (a level keeps the minimum, the farthest
surface, and covers the odd edge its parent would drop); the second tests every
instance against the new pyramid and draws what it uncovers and the first pass
did not already draw, which it knows by repeating the first test. Nothing is
remembered between frames but the two pyramids, and nothing is ever dropped on
older depth, so a fast camera shows every object.

Which path a batch takes: a static batch above `CullSettings.gpu_threshold`
runs both passes as compute dispatches with two indirect draws; a smaller static
batch, the dynamic batches, the sorted draws and the shadow cascades keep the
CPU or compute frustum path of the culling epic and are not occlusion culled.
`CullSettings.occlusion` (default true) and `hiz_levels` (0 for the whole chain)
read from the view first and the singleton after. The first pass is submitted in
a command buffer of its own because a texture read that a command buffer records
before the render pass that fills it reads nothing on wgpu-native.

`city.flecs` (20k buildings, street level camera) draws 2025 of 20001 with
occlusion at 0.80 ms and 13225 without at 0.25 ms: the pyramid hides 12669
instances but boxes are too cheap to draw for that to pay, and the win is the
count, not the time, until shading costs more than the chain. `static_100k` is
2.26 ms against 2.20 ms in the views epic, `occlusion.flecs` draws 2 of 2402
with 2400 occluded, and its capture is byte for byte equal to `occlusion_off`.
`tools/occlusion_sweep.py --env <env>` sweeps a camera at 60 units per second
past walls and compares 120 frames of both settings: all identical.

14 tests in `view.HiZ`, eight projects green (585 cases), `HiZ.project_100k`
2.26 ms for 100k spheres, lint 0, wasm builds, 24 visual scenes unchanged.
Open: the `Bake` and `Extract` microbenchmarks in `bench.json` sit 5 to 12
percent over the views baseline on a machine shared with other agents, and the
frustum count the occlusion pass reports runs a few percent over the one the
plain culling pass reports, which only shifts `culled` against `occluded`.
