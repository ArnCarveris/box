# Bake cost under churn

Timers around every bake phase split the 4.2 ms mean frame of the stress scenes. The game
side was innocent: extract took 0.29 ms without the bake and 0.31 ms with it. The renderer
thread went from 2.56 to 5.56 ms per packet and the game blocks on it, so the 2.9 ms landed
on the frame: invalidation scan 2.06 ms, descriptor upload 0.77 ms, dirty sort 0.10 ms, rest
0.03 ms. The scan walked all 100000 descriptors because each of the ~780 instances moved per
frame pushed an invalidation sphere, and the upload wrote the whole 9.6 MB descriptor array
because those slots span it; a 96 byte write costs 2.4 us, so per slot writes are no cheaper.

A light can only be blocked by geometry it reaches itself, so a moved, added or removed
instance that no static light or emitter reaches now pushes no sphere, leaves the
occupancy grid alone and parks its descriptor instead of uploading it: its probes stay
zero whatever the descriptor holds. Its own probe is still queued. Parked descriptors are
flushed into the upload range, with the grid, as soon as a light or emitter changes or
the scene is marked whole. A move still never lays the pool out again; only a resize, add
or remove does.

Both stress scenes are back at the figure from before baking existed: 1.31 ms mean and
2.73 ms p99 (`material_100k`, was 4.20 and 7.33), 1.34 ms and 2.90 ms (`static_100k`, was
4.21 and 7.47), against 1.29 ms and 2.65 ms with the bake off. `Bake.invalidate_one_of_100k`
fell from 124 us to 0.04 us, the new `Bake.churn_1000_of_100k` costs 77 us and no other
case moved. The five visual scenes render byte for byte as before and 40 tests in
`render.Bake` pass. Still open: a lit move refills the whole occupancy grid on the GPU.
