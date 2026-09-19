# Resolution scale: completion report

## What was built
- `Surface.resolution_scale` divides the pixels the scene is drawn at: one and anything below it is
  the full resolution, two is a quarter of the pixels. `Surface.scaled_width` and
  `scaled_height` report what the scene got, next to `actual_*` and `point_*`.
- The packet carries both sizes. The swap chain keeps the window size; every view that draws into
  the surface takes the scaled one, so its depth target, its Hi-Z pyramid, its transparency copy,
  its effect chain and its overlay all follow without a line of their own. A render target view
  keeps the size of its target.
- `src/renderer/upscale.c` is the only new step of a frame: the surface views render into a texture
  of the scaled size and one full screen pass magnifies it onto the image that is presented, with a
  linear sampler and the `fs_blit` entry point of the existing blur shader. A frame at scale one
  creates nothing and records nothing, which is why every baseline is bit identical.
- Everything authored stays in points: the UI view, `box_picking_ray`, the input normalization and
  the command cursor already read `point_*`, and the capture reads the window sized image.

## Verification
- 773 tests over `test/render` (403, new `Resolution` suite of 7), `test/core` (43), `test/view`
  (147) and `test/ui` (180), all passing. The suite covers the view size, a render target keeping
  its own size, the reported sizes at scale 2, 1 and 0.5, and a capture at scale 2 whose PNG header
  is the window size.
- All 94 visual scenes pass; `resolution_scale` is new and the other 93 are unchanged. Rendering the
  same scene at scale 1, 2 and 4 gives a mean horizontal gradient of 5.14, 3.64 and 2.69 and an 8
  pixel block difference against the full resolution image of 2.0 and 3.9 of 255, so the shape is
  the same and only the detail goes.
- `python3 tools/lint.py` reports 0 problems; `bench/render` attached, unchanged by this task.

## Not in this task
- `test/picking` `Pick.matches_a_brute_force_pick_on_a_smaller_scene` fails after this change: the
  three new reflected members of `Surface` shift entity ids, which flips a tie between two
  coincident colliders. That is the already filed `ore_engine/pick_test_ties` bug, whose brief says
  the test is fragile to any change in entity count; the fix belongs to that task and no test was
  changed here.
- A scale below one is the full resolution rather than supersampling.
- The magnifying pass is a plain bilinear blit; a sharpening or temporal upscaler is open.
