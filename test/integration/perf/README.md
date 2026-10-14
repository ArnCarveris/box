# Performance scenes

Every `<scene>.flecs` in this folder is a performance test: it must hold its requested frame rate
with a camera that orbits the whole scene. `tools/integration.py perf` runs each scene headless
with `boxrun` and fails a scene on any frame outside 10% of its target.

```sh
python3 tools/integration.py perf --env <name>                  # every scene
python3 tools/integration.py perf --env <name> trivial          # some scenes
python3 tools/integration.py perf --env <name> --max-load 4     # wait for a quiet machine
python3 tools/integration.py perf --env <name> --stall 20 --out test/integration/out/slow trivial
python3 tools/integration.py perf --env <name> --json tests.json  # bake3 test report
```

## A scene

A scene declares its content, camera and views like a visual scene, never a `Surface`, and opts in
with the `box.perf.PerfRun` singleton:

```
$ {
  box.perf.PerfRun: {fps: 120, frames: 600, warmup: 120, period: 5, elevation: 30, camera: camera}
}
```

Every scene spells out every setting, so a changed default never changes what a scene measures.

A scene may share content with another scene through `include <name>` (flecs script resolves it
next to the script). The runner writes the script it runs into the output folder, so the scene
lists the files it includes in `<scene>.sources`, one path relative to the scene per line, and
the runner copies them next to it: `town.flecs` includes `town_content`, listed in `town.sources`
as `../../../etc/scenes/town_content.flecs`, the content of `etc/scenes/town.flecs`.

## Rules every scene follows

The runner fails a scene that breaks one of these, whatever its frame times:

- A view renders the orbiting camera or a camera under it (a narrower lens on the orbit, like
  `offscreen_100k`'s `eye`). A scene with a special camera (a controller walk, a fixed pose)
  keeps it only as a second view next to the orbit view, never instead of it.
- The orbiting views draw something. The orbit fits the bounds at `radius / sin(fov / 2)`, which
  for a scene wider than a few hundred units lies beyond the default far plane (1000) of a
  `Camera`: such a scene gives its cameras `far_: 0` (no far plane), like `walk_1m`.
- The measurement covers at least one full revolution: `frames >= fps * period`, so every angle
  is measured and objects enter and leave the view during the measurement.
- The warmup, the only frames that are not measured, is at most one second (`warmup <= fps`). It
  lets the first frames load the scene, build the batches and the spatial index and compile the
  pipelines; nothing after it is excused.
- Every measured frame counts: a frame interval outside 10% of the target is an error and one
  error fails the scene. There is no retry of a run that started; only a run the load guard
  refused to start is retried.

| Member | Meaning |
| --- | --- |
| `fps` | Target: 30, 60, 90 or 120 (60 when added without a value). Anything else makes the run `PerfInvalid`. Also written to `App.fps`. |
| `frames` | Frames measured (300). At least `fps * period` for the runner. |
| `warmup` | Paced, orbiting frames before the measurement (60), at least 2. At most `fps` for the runner. |
| `camera` | Camera to orbit, 0 for the first entity with `Camera`. |
| `period`, `elevation` | Seconds per revolution (10) and degrees the camera looks down (30). A period of 0 stands still, which the runner fails. |
| `stall` | Busy milliseconds added to every frame, only to prove that the checks catch a slow scene. |
| `quit`, `report` | Quit when the run completes; JSON file the result is written to. |

Setting `PerfRun` again, from a script, C or the REST API, restarts the run. The engine writes the
result into the `box.perf.PerfStatus` singleton (state, errors, slow, fast, frame interval and work
statistics, bounds, revolutions, orbit views and culling ranges), which the command port and REST
read like any other component.

## How a run measures

- Orbit: in the second warmup frame the engine takes the world space box around every shape
  (mesh bounds times the instance scale, through its `WorldTransform`). The camera circles its
  center at `elevation` degrees, looking at it, at the distance that fits the sphere around the box
  in the vertical field of view, `radius / sin(fov / 2)` (twice the radius for an orthographic
  camera), so the whole box stays in view from every angle. The angle follows the wall clock, a
  full turn per `period` seconds, and the camera moves every frame.
- Frame interval: the wall time between the ends of two consecutive frames, taken in the last
  `EcsPostFrame` system after the CPU budget sleep. A frame that finishes early waits until one
  target interval `T = 1 / fps` has passed since the previous end, so an early frame is never
  counted as fast. The wait sleeps 60% of the time left at a time (a process without a window gets
  up to 50% timer leeway on macOS: a 10 ms sleep returns after up to 15 ms) and spins the last
  0.2 ms, which lands within microseconds of `T`.
- Error rule: a measured interval `i` is an error when `i > 1.1 T` (slow) or `i < 0.9 T` (fast).
  Warmup frames are not measured. A run passes only when all `frames` intervals were measured and
  none is an error.
- Culling: every measured frame reads the `CullStatus` of the views that render the orbiting
  camera (or a camera under it) and, when the GPU published a new result, adds a sample: the
  visible, frustum culled (outside the frustum or below the smallest size) and occlusion culled
  instances, summed over those views. `PerfStatus.visible`, `frustum_culled` and
  `occlusion_culled` hold the minimum, maximum and mean, `cull_changes` how many samples differ
  from the sample before: the proof that the orbit makes culling decide again every frame.
- Work: the time from the previous frame end to the start of the wait, every system, the render
  submit and any wait for a free render packet included. `work_mean_ms` and `work_p99_ms` show the
  headroom a passing scene has left; frame interval statistics of a passing scene sit at `T`.
- Timing: a headless run still steps the simulation with a fixed delta of `1 / fps`, while the
  pacing, the orbit and the measurement use the wall clock. `App.fps` equals the target, so the
  flecs frame limiter agrees with the pacer.

## CPU budget and load guard

The scenes run like every headless app, guarded and with a CPU budget, and neither may hide or fake
a result:

- The load guard refuses to start when the load average exceeds the core count; the runner retries
  six times, ten seconds apart. `perf.json` keeps the one minute load average before and after
  every scene, and `--max-load` waits (up to `--load-wait` seconds) until the load is low enough.
  Other agents building on the same machine make frames late: compare runs at similar load.
- The budget sleep of `App` (by default one core) happens inside the measured interval. With frames
  paced to the target it only sleeps when the process needs more than its budget on average, and
  then the late frame counts as an error and `slept_ms` reports the sleep, so a budget-limited run
  fails visibly instead of passing slowly. Raise it for a heavy scene with `--cpu-budget`.

## Report

`test/integration/out/perf.json` holds one result per scene in the format the pm page charts as a
scene (`mean_ms`, `p50_ms`, `p99_ms`, `max_ms`, `fps`, `entities`, `drawn` = mean visible,
`culled` = mean frustum plus occlusion culled) plus `target_fps`, `errors`, `slow`, `fast`, the
work statistics, `slept_ms`, `stalls`, the load averages, `warmup`, `period`, `revolutions`,
`deg_per_frame`, `views`, the culling ranges (`visible_min`, `visible_max`, `frustum_culled_min`,
... and `cull_changes` of `cull_samples`), the `problems` that broke a rule and every interval in
`intervals_ms`. The epic pipeline (`tools/epic.py`, step `perf`) runs every scene once the load
average is at most half the cores and fails on any error of any scene. `test/integration/out/perf/` keeps the report boxrun wrote, the log and the script
of every scene. `--out` moves both.

## Scenes

Targets are what minime (Apple silicon, 12 cores, debug build, 1280x720 offscreen) holds with every
frame inside 10% while other agents build: every scene held 120 FPS in three consecutive full
passes at load averages of 2.5 to 5, with a work p99 of 0.7 to 1.6 ms out of 8.3, except
`instanced_dynamic`, whose 10,000 dynamic transforms cost 8 to 10 ms of work per frame, so it
targets 60. Earlier passes failed the 1M walks twice with one or two late frames (9 to 38 ms)
whose work was under 2 ms: the pacer's sleep woke late on a busy machine, not the scene; run with
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
| `effects_chain` | post effects | 120 | Auto exposure, SSAO, height fog, bloom and tonemapping over 10,000 lit blocks and 2,500 glowing spheres. | 11k..12.3k | 0 | 0.1k..1.6k |
| `text_10k` | text | 120 | 10,000 labels, about 50,000 glyphs, lying on a board. | 20k..22k | 26k..28k | 0..0.1k |
| `shapes2d_100k` | 2D shapes | 120 | 100,467 bordered rectangles, bordered circles and lines on the ground, one batch. | 87k..88k | 12.5k..13.4k | 0..1k |
| `walk_1m` | 1M collision | 120 | A first person controller walks a circle through 1,000,000 boxes, spheres, cylinders and cones (250,000 each), colliding through the spatial index, while the view looks at the field through a 5 degree lens on the orbit. | 78k | 922k | 0 |
| `town` | a real scene | 120 | The town of `etc/scenes/town.flecs` (content shared through `town.sources`) with its view: cascaded shadows, occlusion culling with a 4 pixel smallest size, SSAO and tonemapping. | 2.3k..3k | 1k | 0.8k..1.4k |
| `walk_1m_first_person` | 1M collision, two views | 120 | `walk_1m` with a second view through the walking controller's own camera, rendered after the orbit view. | 78k | 922k | 0 |

Frustum culled includes the size cull: shapes below one pixel (the far street boxes of the city,
far glyphs, the 1M field outside the lens) count there. `trivial` and `lights_1000` see their
whole scene from every angle, so their counts do not change; every other scene changes its counts
in nearly every sample.
