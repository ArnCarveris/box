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
  box.perf.PerfRun: {fps: 60, frames: 300, warmup: 60, camera: camera}
}
```

| Member | Meaning |
| --- | --- |
| `fps` | Target: 30, 60, 90 or 120. Anything else makes the run `PerfInvalid`. Also written to `App.fps`. |
| `frames` | Frames measured, 0 for five seconds of frames. |
| `warmup` | Paced, orbiting frames before the measurement, 0 for one second, at least 2. |
| `camera` | Camera to orbit, 0 for the first entity with `Camera`. |
| `period`, `elevation` | Seconds per revolution (10) and degrees the camera looks down (30). |
| `stall` | Busy milliseconds added to every frame, only to prove that the checks catch a slow scene. |
| `quit`, `report` | Quit when the run completes; JSON file the result is written to. |

Setting `PerfRun` again, from a script, C or the REST API, restarts the run. The engine writes the
result into the `box.perf.PerfStatus` singleton (state, errors, slow, fast, frame interval and work
statistics, bounds), which the command port and REST read like any other component.

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
scene (`mean_ms`, `p50_ms`, `p99_ms`, `max_ms`, `fps`, `entities`) plus `target_fps`, `errors`,
`slow`, `fast`, the work statistics, `slept_ms`, `stalls`, the load averages and every interval in
`intervals_ms`. `test/integration/out/perf/` keeps the report boxrun wrote, the log and the script
of every scene. `--out` moves both.

## Scenes

| Scene | Target | Content |
| --- | --- | --- |
| `trivial` | 60 | A floor, a box and a sphere. |
| `instanced_100k` | 60 | 100,000 static boxes in one instanced batch on a floor. |
| `lights_1000` | 60 | 1,000 colored point lights over a floor, one above each of 1,000 boxes, culled per cluster. |
| `walk_1m` | 60 | A first person controller walks a circle through 1,000,000 boxes, spheres, cylinders and cones (250,000 each), colliding through the spatial index. The orbit moves an unused `orbit` entity. |
