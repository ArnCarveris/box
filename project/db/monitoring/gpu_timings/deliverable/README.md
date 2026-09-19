# GPU timings

`GpuTimings {supported, frame, pass_count, frame_ms, passes[16]}` publishes the
GPU milliseconds of every render pass. While the singleton exists the renderer
creates a timestamp query set and writes a query around each pass it records
(`shadow`, `shadow.cull`, `cull`, `occlusion`, `depth`, `opaque`, `uncovered`,
`sky`, `sky.env`, `glass`, `sorted`, `lines`, `overlay`, `motion`, `gtao`,
`exposure`, `bloom` and `effect.0` and up for the chain of a view); the encoder
of the next frame resolves them, because Metal only completes the counters of a
command buffer with it, and the map is read back a frame or two later. Passes
that share a name report what they spent together. Nothing is created and no
pass is timed while the component is absent, and a device without
`timestamp-query` reports `supported: false` with no passes.

Verified: 16 new cases in `monitor.Gpu` (naming, overflow, ns to ms, summing per
name, the delayed resolve and the component of a world without a renderer) and 3
in `render.Gpu` against a real device; 442 tests over `test/monitor` and
`test/render`, `test/core` and `test/view` green, lint 0, and all 74 visual
scenes bit identical. `gpu_timings.log` is the headless pass table of a scene
with four effects: 10 passes, shadow 0.43 ms, opaque 0.39 ms, gtao 1.84 ms,
bloom 11.7 ms. The publish system costs 0.23 us per frame (`bench.json`).

Left out: no `gpu` command and no column in `fps`, because `src/command` belongs
to another task; the terminal reads the component with `get`. The scene has no
`.expected`, since GPU milliseconds differ per run. Enabling is the presence of
the singleton rather than a `Surface` field, which keeps `include/box/surface.h`
untouched and matches the other monitors. Collecting a read back costs one
blocking device poll every few frames while the monitor is on.
