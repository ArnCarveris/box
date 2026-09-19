# Splines: completion report

## What was built
- `box.spline` with `Spline`: a reflected vector of control points, a type (catmull rom, linear, bezier), tension and a loop flag. Every set rebuilds a cached arc length table of 16 samples per segment and writes `SplineInfo`; the module evaluates position, tangent and normal by parameter or by distance, and scripts call `spline.position`, `spline.tangent`, `spline.normal`, `spline.point_at`, `spline.tangent_at`, `spline.normal_at` and `spline.length` under `using box`.
- `FollowPath {spline, speed, offset, mode, orient}` moves any entity along a curve by arc length in OnUpdate with loop, ping pong and once modes, writes `Rotation` from the tangent when asked and adds `Position`, `Rotation` and `DynamicTransform`, so a light or a camera follows a curve the same way a mesh does.
- `SplineMesh {spline, profile, segments, width, height, closed, smooth}` sweeps a cross section polyline along a curve at even arc length and writes an ordinary `Mesh3`: roads, pipes and cables get lighting, shadows, materials, collision and mesh cache sharing with no renderer change, and the mesh is rebuilt on curve or component change only, so the entity stays a static instance.

## Verification
- 1322 unit tests across 15 projects, all passing; `test/spline` is new with 67 tests over the `ArcLength`, `Cache`, `Degenerate`, `Eval`, `Follow`, `Mesh`, `Script` suites.
- Scenes in `integration/`: `spline_path` draws a closed catmull rom loop as boxes at even arc length with four objects and a point light following at different offsets, `spline_road` a closed road loop with a centre line and a smooth hexagonal pipe; every earlier scene bit identical.
- Benchmarks in `bench/` against the 2D shapes epic: 10k followers move in 0.53 ms a frame (0.75 ms with orientation), a 1000 point road of 2000 slices rebuilds in 0.21 ms, 0.51 ms when the table is rebuilt with it.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- An arc length table on the component is what makes speed independent of curve shape and a fixed step deterministic; sampling by parameter alone bunches followers on tight bends.
- Building the road on the CPU as a `Mesh3` instead of the brief's vertex shader extrusion bought shadows, materials and collision for free and left the renderer untouched.

## Not in this epic
- The particle emitter shape along a spline waits for the particles module.
- A closed profile with `smooth` wraps its u back to zero on the last edge; a seam texture needs a duplicated ring.
