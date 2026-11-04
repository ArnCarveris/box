# Performance scenes

Every `<scene>.flecs` in this folder is a performance test: it must hold its requested frame rate
with a camera that orbits the whole scene (or that its controller flies), and every frame's
`delta_time` must match the wall time that passed. `tools/integration.py perf` runs each scene
headless with `boxrun` and fails a scene on any frame outside 10% of its target, any delta off by
more than 1 ms. The engine does not pace the frames: the run sets `App.fps`, so flecs caps them
to the target, and the run measures the intervals.

```sh
python3 tools/integration.py perf --env <name>                  # every scene
python3 tools/integration.py perf --env <name> trivial          # some scenes
python3 tools/integration.py perf --env <name> --max-load 4     # wait for a quiet machine
python3 tools/integration.py perf --env <name> --stall 20 --out test/integration/out/slow trivial
python3 tools/integration.py perf --env <name> --json tests.json  # bake3 test report
python3 tools/integration.py perf --env <name> --seconds 10 town_fly_60  # a shorter run
```

The runner builds and runs `boxrun` in release (`--cfg release`, the default since
testing/town_stutter; `--cfg debug` overrides it). Earlier baselines, the scene targets and the
`perf.json` of testing/perf_orbit and of the ui epic, were measured in debug builds.

## A scene

A scene declares its content, camera and views like a visual scene and opts in with the
`box.perf.PerfRun` singleton. The runner adds an offscreen `Surface` of `--size` (1280x720) unless
the scene declares its own, like the town scenes, which measure the retina window of the town
(1280x720 points at `pixel_ratio: 2`, 2560x1440 pixels):

```
$ {
  box.perf.PerfRun: {fps: 120, frames: 600, warmup: 120, period: 5, elevation: 30, camera: camera}
}
```

Every scene spells out every setting, so a changed default never changes what a scene measures.

A scene contains all of its content and includes no other script, so changing an example scene in
`etc/scenes` does not change what a perf scene measures.

## Rules every scene follows

The runner fails a scene that breaks one of these, whatever its frame times:

- A view renders the orbiting camera or a camera under it (a narrower lens on the orbit, like
  `offscreen_100k`'s `eye`). A scene with a special camera (a controller walk, a fixed pose)
  keeps it only as a second view next to the orbit view, never instead of it. A fly run
  (`fly: true`) has no orbit: its controller flies the camera and the camera must move.
- The orbiting views draw something. The orbit fits the bounds at `radius / sin(fov / 2)`, which
  for a scene wider than a few hundred units lies beyond the default far plane (1000) of a
  `Camera`: such a scene gives its cameras `far_: 0` (no far plane), like `walk_1m`.
- The measurement covers at least one full revolution: `frames >= fps * period`, so every angle
  is measured and objects enter and leave the view during the measurement (orbit runs only).
- The warmup, the only frames that are not measured, is at most one second (`warmup <= fps`). It
  lets the first frames load the scene, build the batches and the spatial index and compile the
  pipelines; nothing after it is excused.
- Every measured frame counts: a frame interval outside 10% of the target, a delta that differs
  more than 1 ms from the wall time since the previous frame are errors, and one error fails the
  scene. There is no retry of a run that started; only a run the load guard refused to start is
  retried.

| Member | Meaning |
| --- | --- |
| `fps` | Target: 30, 60, 90 or 120 (60 when added without a value). Anything else makes the run `PerfInvalid`. Also written to `App.fps`. |
| `frames` | Frames measured (300). At least `fps * period` for the runner. |
| `warmup` | Capped, orbiting frames before the measurement (60), at least 2. At most `fps` for the runner. |
| `camera` | Camera to orbit, 0 for the first entity with `Camera`. |
| `fly` | The camera's `CameraController` flies it with synthetic input (W and the left arrow held, Shift toggled every 2 s) instead of the orbit. |
| `period`, `elevation` | Seconds per revolution (10) and degrees the camera looks down (30). A period of 0 stands still, which the runner fails. |
| `stall` | Busy milliseconds added to every frame, only to prove that the checks catch a slow scene. |
| `quit`, `report` | Quit when the run completes; JSON file the result is written to. |

Setting `PerfRun` again, from a script, C or the REST API, restarts the run. The engine writes the
result into the `box.perf.PerfStatus` singleton (state, errors, slow, fast, frame interval
statistics, bounds, revolutions, orbit views, culling ranges and, in `checks`, the delta, present
and CPU checks and the distance a fly run travelled), which the command port and REST read like
any other component.

## How a run measures

- Orbit: in the second warmup frame the engine takes the world space box around every shape
  (mesh bounds times the instance scale, through its `WorldTransform`). The camera circles its
  center at `elevation` degrees, looking at it, at the distance that fits the sphere around the box
  in the vertical field of view, `radius / sin(fov / 2)` (twice the radius for an orthographic
  camera), so the whole box stays in view from every angle. The angle follows the wall clock, a
  full turn per `period` seconds, and the camera moves every frame.
- Frame interval: the wall time between the ends of two consecutive frames, taken in the last
  `EcsPostFrame` system after the CPU budget sleep. The run writes the target to `App.fps`, so
  `ecs_set_target_fps` sleeps the frame out to `T = 1 / fps`; the engine has no pacer, wake up
  learning or thread priority of its own, so the interval is what flecs and the OS deliver. The
  browser has no perf runs.
- Error rule: a measured interval `i` is an error when `i > 1.1 T` (slow) or `i < 0.9 T` (fast).
  Warmup frames are not measured. A run passes only when all `frames` intervals were measured and
  none is an error.
- Culling: every measured frame reads the `CullStatus` of the views that render the orbiting
  camera (or a camera under it) and, when the GPU published a new result, adds a sample: the
  visible, frustum culled (outside the frustum or below the smallest size) and occlusion culled
  instances, summed over those views. `PerfStatus.visible`, `frustum_culled` and
  `occlusion_culled` hold the minimum, maximum and mean, `cull_changes` how many samples differ
  from the sample before: the proof that the orbit makes culling decide again every frame.
- Delta: a run leaves `App.delta_time` at 0, so flecs measures `delta_time`. Every measured frame
  compares the unscaled `delta_time` with the wall time since the start of the previous frame
  on the same OS clock; a difference over `BOX_FRAME_TOLERANCE_MS` (1 ms) is a delta error. The
  tolerance is absolute because the error of a measured delta is wake up jitter, which does not
  grow with the frame interval; 1 ms is 12% of a 120 FPS frame.
- CPU: the process CPU time (all threads) over the wall time of the measurement is reported as
  `cpu_percent` and per frame as `cpu_ms`. A present that busy waits shows up as CPU time
  that grows with the idle time of a frame; on minime the town costs about 3.8 ms of CPU per frame
  at every target.

## CPU budget and load guard

The scenes run like every headless app, guarded and with a CPU budget, and neither may hide or fake
a result:

- The load guard refuses to start when the load average exceeds the core count; the runner retries
  six times, ten seconds apart. `perf.json` keeps the one minute load average before and after
  every scene, and `--max-load` waits (up to `--load-wait` seconds) until the load is low enough.
  Other agents building on the same machine make frames late: compare runs at similar load.
- The budget sleep of `App` (by default one core) happens inside the measured interval. With frames
  capped to the target it only sleeps when the process needs more than its budget on average, and
  then the late frame counts as an error and `slept_ms` reports the sleep, so a budget-limited run
  fails visibly instead of passing slowly. Raise it for a heavy scene with `--cpu-budget`.

## Report

`test/integration/out/perf.json` holds one result per scene in the format the pm page charts as a
scene (`mean_ms`, `p50_ms`, `p99_ms`, `max_ms`, `fps`, `entities`, `drawn` = mean visible,
`culled` = mean frustum plus occlusion culled) plus `target_fps`, `errors`, `slow`, `fast`, the
`slept_ms`, `stalls`, the load averages, `warmup`, `period`, `revolutions`,
`deg_per_frame`, `views`, the culling ranges (`visible_min`, `visible_max`, `frustum_culled_min`,
... and `cull_changes` of `cull_samples`), the `problems` that broke a rule and every interval in
`intervals_ms`. The epic pipeline (`tools/epic.py`, step `perf`) runs every scene once the load
average is at most half the cores and fails on any error of any scene. `test/integration/out/perf/` keeps the report boxrun wrote, the log and the script
of every scene. `--out` moves both.

## Scenes

Targets are what minime (Apple silicon, 12 cores, debug build, 1280x720 offscreen) holds with every
frame inside 10% while other agents build: every scene held 120 FPS in three consecutive full
passes at load averages of 2.5 to 5, except
`instanced_dynamic`, whose 10,000 dynamic transforms cost 8 to 10 ms of work per frame, so it
targets 60. Earlier passes failed the 1M walks twice with one or two late frames (9 to 38 ms)
whose work was under 2 ms: the sleep woke late on a busy machine, not the scene; run with
`--max-load` on a quiet machine. Counts are the ranges over the orbit of one pass.

| Scene | Feature | Target | Content | Visible | Frustum culled | Occlusion culled |
| --- | --- | --- | --- | --- | --- | --- |
| `trivial` | baseline | 120 | A floor, a box and a sphere. | 3 | 0 | 0 |
| `instanced_100k` | static instancing | 120 | 100,000 static boxes in one instanced batch on a floor. | 95k..100k | 0 | 0..4.6k |
| `instanced_dynamic` | dynamic instancing | 60 | The 100,000 static boxes plus 10,000 `DynamicTransform`/`DynamicMaterial` boxes on a turntable that turns every frame (a camera controller whose Look action holds a fixed delta). | 97k..110k | 0 | 0.1k..12.6k |
| `offscreen_100k` | frustum culling | 120 | The 100,000 boxes seen through a 12 degree lens on the orbit, turned off the center, so it sweeps a ring and boxes enter and leave the frustum every frame. | 8.9k..28k | 72k..90k | 0..4.2k |
| `occluded_city` | Hi-Z occlusion | 120 | 2,500 tall blocks with 100,000 small boxes in the streets between them. | 2.9k..8.2k | 60k..65k | 33k..37k |
| `occluded_city_fast` | Hi-Z history | 120 | The city with a revolution per second at 15 degrees: 3 degrees per frame, so the history the early phase draws is out of date every frame. | 2.7k..8k | 58k..62k | 36k..40k |
| `shadows_40k` | cascaded shadows | 120 | 40,000 pillars casting into four 2048 texel cascades of a low sun; the cascades are fitted to the moving frustum every frame. | 39k..40k | 0 | 0..1k |
| `lights_1000` | clustered lights | 120 | 1,000 colored point lights over a floor, one above each of 1,000 boxes; the clusters are culled again for the moving view every frame. | 1,001 | 0 | 0 |
| `effects_chain` | post effects | 120 | Auto exposure, GTAO, height fog, bloom and tonemapping over 10,000 lit blocks and 2,500 glowing spheres. | 11k..12.3k | 0 | 0.1k..1.6k |
| `text_10k` | text | 120 | 10,000 labels, about 50,000 glyphs, lying on a board. | 20k..22k | 26k..28k | 0..0.1k |
| `shapes2d_100k` | 2D shapes | 120 | 100,467 bordered rectangles, bordered circles and lines on the ground, one batch. | 87k..88k | 12.5k..13.4k | 0..1k |
| `polygons_10k` | 2D polygons | 120 | 10,092 `Polygon2D` shapes on the ground: bordered concave L outlines of 6 edges, curved plazas of 34 edges and stroked roads of 32 edges, one batch. | not yet measured | not yet measured | not yet measured |
| `polygons_large` | 2D polygons, stress | 60 | 12 overlapping, translucent rings of 512 curved edges each with a hole, each covering most of the view, so a pixel measures up to 6,144 edges. | 13 | 0 | 0 |
| `box_shadow_ui` | 2D shadows | 120 | A dashboard of 37 shadowed panels lying on the ground: header, sidebar items, 12 cards with buttons and a dialog, blurs of 3 to 64. | 75 | 0 | 0 |
| `box_shadow_1000` | 2D shadows, stress | 120 | 1,000 opaque list items with a blur of 48, whose 312 by 188 shadow quads overlap 20 neighbours each: 2,001 instances (the shadows count as instances) drawn as 2,001 runs. | 2,001 | 0 | 0 |
| `walk_1m` | 1M collision | 120 | A first person controller walks a circle through 1,000,000 boxes, spheres, cylinders and cones (250,000 each), colliding through the spatial index, while the view looks at the field through a 5 degree lens on the orbit. | 78k | 922k | 0 |
| `town_orbit_30` .. `town_orbit_120` | windowed town | 30, 60, 90, 120 | The town at the retina size of its window for a minute, orbiting, presenting to its window. | 2.5k..3.6k | 0.1k | 1.1k..2.2k |
| `town_fly_30` .. `town_fly_120` | moving through the town | 30, 60, 90, 120 | The same, flown through the streets by the scene's `CameraController` with synthetic input for a minute. | 0..2.4k | 0.1k..4.8k | 0..2.4k |
| `town` | a real scene | 120 | The town of `etc/scenes/town.flecs` with its view: cascaded shadows, occlusion culling with a 4 pixel smallest size, GTAO and tonemapping. | 2.3k..3k | 1k | 0.8k..1.4k |
| `walk_1m_first_person` | 1M collision, two views | 120 | `walk_1m` with a second view through the walking controller's own camera, rendered after the orbit view. | 78k | 922k | 0 |

Frustum culled includes the size cull: shapes below one pixel (the far street boxes of the city,
far glyphs, the 1M field outside the lens) count there. `trivial`, `lights_1000` and the
`box_shadow` scenes see their whole scene from every angle, so their counts do not change; every
other scene changes its counts in nearly every sample.
