# Perf

Performance runs: cap frames at a target rate, orbit a camera around the bounds of every shape
(or fly it with its controller), and count every frame interval outside 10% of the target and
every `delta_time` that differs from wall time. Module `box.perf` (`BoxPerfImport`); the perf
scenes in `test/integration/perf/*.flecs` are run by `tools/integration.py perf`. No perf runs
in the browser.

## Components

| Name (script) | C type | Role |
|---|---|---|
| `box.perf.Run` | `PerfRun` | Settings singleton; setting it (script, C, REST) (re)starts the run |
| `box.perf.Status` | `PerfStatus` | Result singleton, written by the engine, read only |
| `box.perf.State` | `PerfState` | `PerfIdle`, `PerfWarmup`, `PerfMeasure`, `PerfDone`, `PerfInvalid` (settings rejected) |
| `box.perf.Range` | `PerfRange` | `min`, `max`, `mean` of a sampled count |
| `box.perf.Checks` | `PerfChecks` | Delta, present, CPU and fly checks (`Status.checks`) |
| `box.perf.TextureGraphs` | `PerfTextureGraphs` | Texture graph cache hits/misses/evictions/updates/cached_kb per frame |

`PerfRun` fields (constructor defaults):

| Field | Default | Meaning |
|---|---|---|
| `fps` | 60 | Target: 30, 60, 90 or 120; anything else makes the run `Invalid` (error logged). Written to `App.fps` |
| `frames` | 300 | Frames measured |
| `warmup` | 60 | Capped, orbiting frames before measuring, at least 2 |
| `camera` | 0 | Camera to orbit, 0 = first entity with `Camera` |
| `period` | 10 | Seconds per revolution; 0 stands still |
| `elevation` | 30 | Degrees the camera looks down on the center |
| `stall` | 0 | Busy ms added per frame, only to prove the checks catch a slow scene |
| `fly` | false | `CameraController` flies the camera: W and Left held, Shift toggled every 2 s; no orbit |
| `quit` | false | Quit the world when done (also when invalid) |
| `report` | NULL | JSON file the result is written to |

`PerfStatus` highlights: `state`, `passed`, `frame`, `errors` (= `slow + fast +
checks.delta_errors`), `slow` (> 1.1 T), `fast` (< 0.9 T), `target_ms`, `min/mean/p50/p99/max_ms`,
`slept_ms` (CPU budget sleep), `stalls` (frames waiting for a render packet), `shapes`, `center`,
`radius`, `distance`, `revolutions`, `views`, `cull_samples`, `cull_changes`, ranges `visible`,
`frustum_culled`, `occlusion_culled`, `batches_skipped`, `casters_skipped`, `audio_load`,
`texture_graphs`, and `checks` (`delta_errors`, `delta_max_ms`, `present_mean_ms`,
`cpu_percent`, `cpu_ms`, `fly`, `travelled`).

## A perf scene

A scene declares its content, camera and views like a visual scene and sets `box.perf.Run`.
It includes no other script (so editing `etc/scenes` never changes what it measures) and spells
out every setting (so a changed default never does either):

```flecs
using box.transform
using box.geometry
using box.camera
using box.view
using box.perf

camera {
  Camera: {fov: 50}
}

main_view {
  View: {camera: camera, clear: {enabled: true, color: {24, 28, 36, 255}}}
}

cube {
  Position: {0, 0.5, 0}
  Box: {1, 1, 1}
}

$ {
  box.perf.Run: {fps: 120, frames: 600, warmup: 120, period: 5, elevation: 30, camera: camera}
}
```

The runner adds an offscreen `Surface` of `--size` (1280x720) unless the scene declares one (the
town scenes use `Surface: {width: 1280, height: 720, pixel_ratio: 2}`).

## Rules (the runner fails a scene that breaks one, whatever its times)

- A view renders the orbiting camera or a camera under it. A special camera (controller walk,
  fixed pose) is a second view next to the orbit view, never instead of it.
- The orbiting views draw something. The orbit distance `radius / sin(fov / 2)` lies beyond the
  default far plane (1000) for scenes wider than a few hundred units: give cameras `far_: 0`.
- Orbit runs cover at least one revolution: `frames >= fps * period`, and `period > 0`.
- Fly runs: the camera must move (`checks.travelled > 0`).
- `warmup <= fps` (at most one second of unmeasured frames).
- Every measured frame counts; one error fails the scene. A started run is never retried; only a
  run the load guard refused to start is.

## How a run measures

- Bounds: in the second warmup frame, the world box of every shape (mesh bounds through its
  `WorldTransform`). The camera circles the center at `elevation`, at `radius / sin(fov / 2)`
  (2 x radius for ortho), angle from the wall clock, so it moves every frame.
- Warmup restarts while a light or reflection bake is still progressing; a bake stuck for 300
  frames is given up with an error and the run measures without it.
- Interval: wall time between the starts of consecutive frames (`EcsPreFrame` to `EcsPreFrame`),
  counted in `EcsPostFrame`. flecs caps the frames via `App.fps`; the engine has no pacer of its
  own, so the interval is what flecs and the OS deliver. Error when `i > 1.1 T` or `i < 0.9 T`.
- Delta: the run leaves `delta_time` measured by flecs; each frame compares the raw delta with
  the OS wall time since the previous frame start. Off by more than `BOX_FRAME_TOLERANCE_MS`
  (1 ms, absolute: wake-up jitter) is a delta error.
- Culling: each measured frame reads the `CullStatus` of the orbit views and samples it when the
  GPU published a new result; `cull_changes` > 0 proves culling re-decides as the camera moves.
  Frustum culled includes the sub-pixel size cull.
- CPU: process CPU time (all threads) over wall time, as `cpu_percent` and `cpu_ms` per frame.
  A busy-waiting present shows as CPU growing with idle time.
- Passes when all `frames` intervals were measured with zero errors.

## Running

```sh
python3 tools/integration.py perf --env <name>                      # every scene
python3 tools/integration.py perf --env <name> trivial town         # some scenes
python3 tools/integration.py perf --env <name> --max-load 4         # wait for a quiet machine
python3 tools/integration.py perf --env <name> --seconds 10 town_fly_60   # fps * 10 frames
python3 tools/integration.py perf --env <name> --stall 20 --out test/integration/out/slow trivial
python3 tools/integration.py perf --env <name> --cpu-budget 200 town
python3 tools/integration.py perf --env <name> --json tests.json    # bake3 test report
```

Builds `apps/boxrun` in release (`--cfg` overrides), copies the binary once and runs each scene
headless (`--port 0`) through a harness `<scene>.run.flecs` that sets `box.perf.Run` again with
the scene's members plus `quit: true, report: "<out>/perf/<scene>.json"`.

By hand: a scene whose `Run` sets `quit` and `report`, then
`bake3 run apps/boxrun --local-env -- --headless --timeout 240 --port 0 <scene>` (the runner
builds with `bake3 build apps/boxrun --local-env --cfg release`). Unit tests of the module:
`bake3 run test/cpu/perf --local-env`.

## CPU budget and load guard

- Headless runs refuse to start when the 1 min load average plus the run's claim (`cpu_budget`,
  default one core) exceeds the core count (`cpu oversubscribed` in the log). The runner waits
  for cores and retries, up to `--guard-wait` seconds (1800) for the whole run.
  `--max-load n` waits up to `--load-wait` (300 s) before each scene for load <= n. Other agents
  building on the machine make frames late: compare runs at similar load.
- The `App` budget sleep falls inside the measured interval. With capped frames it only sleeps
  when the process needs more than its budget on average; the late frame then is an error and
  `slept_ms` shows it, so a budget-limited run fails visibly. Raise it with `--cpu-budget`.

## Report

- `test/integration/out/perf.json` (or `--out`): one result per scene with `status`,
  `problems`, `detail`, `mean_ms`, `p50_ms`, `p99_ms`, `max_ms`, `fps`, `target_fps`, `errors`,
  `slow`, `fast`, `slept_ms`, `stalls`, `entities` (shapes), `drawn` (mean visible), `culled`
  (mean frustum + occlusion), culling min/max, `cull_changes`/`cull_samples`, `warmup`,
  `period`, `revolutions`, `deg_per_frame`, `views`, load before/after, and `intervals_ms`.
- `test/integration/out/perf/` keeps each scene's boxrun report, log and harness script.

## Scenes (test/integration/perf)

`trivial` (baseline), `instanced_100k`, `instanced_dynamic` (60 fps: 10k dynamic transforms),
`offscreen_100k` (frustum culling through a 12 degree lens), `occluded_city`,
`occluded_city_fast` (Hi-Z history at 3 degrees/frame), `shadows_40k`, `lights_1000`,
`effects_chain`, `text_10k`, `shapes2d_100k`, `polygons_10k`, `polygons_large` (60),
`box_shadow_ui`, `box_shadow_1000`, `walk_1m`, `walk_1m_first_person` (1M collision),
`texture_graph_clouds`, `water_1080`, `town`, `town_1080`, `town_planar_reflection`,
`town_orbit_{30,60,90,120}`, `town_fly_{30,60,90,120}`. Targets are in each scene's `Run`.
