# Baked lighting

`BakeSettings` in `box.light` switches the bake on per scene. Extract marks a light
without a dynamic tag on itself or an ancestor static, carries those unculled in
`bake_lights`, lists emissive static instances in `emitters` and flags the
instance data of static entities.

`src/renderer/bake.c` keeps one order 1 spherical harmonics probe (3 float4) per
static instance slot in a storage buffer per mesh that grows with the batch. One
compute pass fills a 128 by 32 by 128 bit occupancy grid over the static scene
bounds from the probe bounds, a second bakes a list of slots from the static
lights and the emitters, ray marching 32 steps through the grid from outside the
probe's own bounds to just before the light. The mesh shader evaluates the probe
with the surface normal and skips static lights in the cluster loop for baked
instances, so nothing is counted twice; `bake.h` holds the CPU twin of that math.

A static delta marks its probe, a changed light or emitter the probes in its range
and a moved instance those within the largest light range of it. The dirty set is
sorted by distance to the eye and at most `budget` probes (64) bake per frame.

Numbers: 24 tests in `render.Bake` pass and no case regressed against the ibl
baseline. Moving one entity of 100k marks 277 probes (0.28%) in 130 us
(`Bake.invalidate_one_of_100k`). `baked_room` holds 196 probes, converges in two
frames and runs 1000 headless frames in 0.09 s; rebaking every probe every frame
costs 0.07 ms per frame at a budget of 128.

Limitations: one probe per instance, so large geometry lights flat and the rooms
are tiled; diffuse only; a binary march; invalidation scans every probe; the
passes run on the render thread; `enabled` is off by default.
