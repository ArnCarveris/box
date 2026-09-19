# Monitoring: completion report

## What was built
- `CpuMonitor` publishes process and machine CPU usage as a reflected singleton from one set of measurement primitives in `src/core/cpu.c` that the budget pacing and the startup oversubscription check share; machine load comes from the host on macOS and Linux and is zero on the web.
- `SystemMonitor`, off by default, turns on the flecs per system time measurement and publishes the frame total, the slowest eight systems and the time per pipeline phase; `EcsStats` mirrors the flecs stats addon into reflected fields every period, with entity and table counts, frame, system and merge time and the slowest systems.
- `FpsOverlay` draws a panel in a corner of a view: the frame rate as seven segment digits and one bar per frame of the last second, green above 60, yellow above 30, red below, drawn by the renderer after the effect chain so it appears in captures and the inspector; the `fps` command prints the window and toggles the panel.
- `GpuTimings`: while the singleton exists the renderer writes timestamp queries around every pass (shadow, cull, depth, opaque, sorted, sky, glass, lines, overlay, GTAO, exposure, bloom and each chained effect), resolves them in the next frame's encoder and reads them back a frame or two later; a device without the feature reports `supported: false`, and nothing is created while the component is absent.

## Verification
- 1657 unit tests across 19 projects, all passing; `test/monitor` is new with 69 tests over the `Cpu`, `Fps`, `Gpu`, `Spikes`, `Stats`, `Systems` suites.
- Scenes in `integration/`: `fps_overlay` baselined and bit identical over three runs; command scenes `monitor` (reading every singleton), `fps` (on, off, print and error) and `gpu_timings` (a ten pass table for a scene with four effects: shadow 0.43 ms, opaque 0.39 ms, GTAO 1.84 ms, bloom 11.7 ms); every other scene bit identical.
- Benchmarks in `bench/` against the UI epic: the CPU monitor costs 3.9 µs a frame, the system monitor 2.7 µs, ECS stats 11.8 µs, all three together 18.9 µs, the GPU timings publish 0.23 µs, and the overlay under 0.01 ms.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Flecs already measures per system time; the system monitor only differences the previous frame's totals, so attribution needed no hooks.
- The flecs frame counter advances at the end of a frame, which dropped the first frame of the FPS window until the monitor accounted for it.
- Metal completes timestamp counters only with the command buffer, so queries resolve in the next frame's encoder and the read back lags by one or two frames.

## Not in this epic
- No `gpu` command and no GPU column in `fps` yet; the terminal reads `GpuTimings` with `get`. Collecting a read back costs one blocking device poll every few frames while the monitor is on.
- The GPU timings scene has no expected output because milliseconds differ per run.
