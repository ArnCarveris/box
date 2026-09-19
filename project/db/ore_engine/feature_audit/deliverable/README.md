# Feature audit: completion report

## What was built
- `deliverable/NOTES.md`: the table from every flecs-engine component, `flecsEngine_*` call and
  script construct Ore Else uses to its box equivalent, with a mapping rule and a gap column, plus a
  list of which of the game's sixteen C files become script under the porting rule.
- `box_ray_plane` and `box_ray_plane_y` in `include/box/math.h`, the ground plane query every cursor
  to cell mapping in the game needs, beside `box_picking_ray`.
- Seven gap tasks filed in this epic: `instance_wear`, `one_shot_effects`, `visibility`,
  `resolution_scale`, `follow_camera`, `any_key_binding` and `app_args`, each with a README in the
  style of the existing ones.

## Verification
- `test/core` 34 tests, all passing (`tests.json`); the new `Math` suite holds 7 cases over the
  ground plane, an arbitrary plane, the parallel and behind-the-origin misses and a test without an
  out point.
- `python3 tools/lint.py` reports 0 problems, `python3 project/pm.py check` 0 problems and 0
  warnings.

## Not in this task
- No scene and no benchmark: the helpers are inline math and nothing else changed at runtime.
- The gaps are filed, not fixed; `component_id_drift` and the five feature tasks of this epic stay
  the porting agents' blockers until they land.
