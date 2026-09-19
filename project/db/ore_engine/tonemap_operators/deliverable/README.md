# Tonemap operators: completion report

## What was built
- `ToneMapping.curve` picks the display transform of the tone map pass out of the new
  `ToneMapOperator` enum: `ToneMapNeutral`, the hue preserving curve the pass already had and the
  default every baseline holds, and `ToneMapTony`, the Tony McMapface lookup.
- The lookup is a 48 cube of RGB9E5 texels shipped as `etc/shaders/lut/tony_mc_mapface.lut`, so the
  shader search path and the wasm embed find it like any other engine asset. The effects module
  reads it the first time a pass selects the operator and sends it as one resource command; the
  renderer turns it into a 3D texture and binds it on the pass that declared the input, a single
  texel dummy otherwise. A cube that cannot be read is reported once and the pass falls back to the
  neutral curve.
- The pass folds the exposed stimulus into the cube with `x / (x + 1)` and samples half a texel
  inside it, the mapping the cube was baked with. An enum member of a parameter component now
  reaches the shader block as a float, which it was silently skipped as before.

## Verification
- 570 tests over the new `test/effects` (7), `test/view` (147), `test/render` (386) and
  `test/shader` (30), all passing; the `ToneMap` suite covers the default, the curve in the
  parameter block, the input bit, the cube reaching the packet once, and the lookup itself: black
  stays black, the diagonal rises with the stimulus and a bright red desaturates.
- `tonemap_tony` baselined beside `tonemap`; all 90 visual scenes pass, the other 89 unchanged.
- `python3 tools/lint.py` reports 0 problems; `bench/render` attached, unchanged by this task.

## Not in this task
- The cube is the operator's only asset; there is no component that points a pass at a cube of its
  own, which is what a project grading LUT would want.
