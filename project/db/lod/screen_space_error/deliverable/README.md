# Screen space error

## What was built
- `box_lod_error_px(bounds, error, camera, viewport_px)` projects the error of a representation at
  the nearest point of the bounds of a node: perspective divides by that distance and the field of
  view, orthographic only by the visible height. `box_lod_error_scale` is the same metric for one
  world unit, so a node with several levels pays the distance once, and a camera inside the bounds
  clamps to `BOX_LOD_MIN_DISTANCE` instead of going infinite.
- `box_lod_select` takes the projected error of every level and the level of the previous frame,
  and moves coarser only under `threshold * (1 - hysteresis)` and finer only over
  `threshold * (1 + hysteresis)`, so a level holds inside the band; level zero is the floor.
- `LodSettings` on a view (enabled, quality, error_px 2, shadow_error_px 6, hysteresis 0.25) reads
  the view first and the singleton after like `CullSettings`; quality scales both thresholds and
  the extract writes them into the packet behind the view uniform.

## Verification
- `tests.json`: 25 new of 545. `lod:Screen` 12 for both projections, `lod:Hysteresis` 10 for the
  band, `lod:Walk` 3 recording the level of a 400 frame walk and of a camera on a boundary, `view`
  147 with the new `Lod` suite of 7, `render` 373. 74 visual scenes match, lint reports 0.
- `bench.json`: 100k projections take 357 us perspective, 97 us orthographic, 100k selections 259 us.

## Not in this task
- Nothing selects yet: `hierarchy_traversal` walks the nodes and reads the packet thresholds, and
  the shadow threshold is extracted but unused until then. No scene and no statistics.
