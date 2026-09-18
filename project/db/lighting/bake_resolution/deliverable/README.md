# Bake resolution

A static instance holds a grid of probes instead of one: each axis gets
`ceil(size / BakeSettings.probe_spacing)` probes (default 1.0 world unit)
clamped to `probe_max` (default 32), so a 2 by 0.4 by 2 tile gets 2 by 1 by 2
and a 10 by 0.2 by 10 floor 10 by 1 by 10. Probes span the bounds, so
neighbouring tiles share those on their seam. They live in one pool buffer per
mesh; a descriptor carries the pool index, grid dimensions and world span per
axis of its instance, and the pool is laid out again on add, resize or remove.

The bake pass takes a job per probe (slot and grid index), derives its world
position from the instance transform and marches occlusion from its own cell.
The budget counts probes, the mesh shader interpolates the eight probes around a
fragment trilinearly and the invalidation scan binary searches sorted regions.

`enabled` defaults to on and the budget to 16384 probes per frame. `baked_room`
holds 808 probes (was 196) and bakes them in one frame; step pixels across the
floor drop from 1.72% to 0.65%, matching `dynamic_room` (0.64%). `lights_city`
holds 18502 probes and stays within a mean absolute pixel difference of 4.3/255
of its clustered forward reference, with every light and no banding but softer
highlights, so that reference is kept as it is. The 100k scene holds 100000
probes (`Bake.probe_count_100k`). Frame cost: 0.104 ms against 0.087 ms with the
bake off in `baked_room`, 0.182 ms either way in `lights_city`, 4.2 ms (239 fps)
in the `static_100k` churn bench. `Bake.invalidate_one_of_100k` is 124 us.
