# Frame spike harness

`box_bench` adds `p50_ms`, `max_ms`, `lights`, `spike_factor`, `spike_budget_ms`, `attributed`,
`spike_count` and a `spikes` array of the worst eight frames (`frame`, `ms`, `system`,
`system_ms`). `--spike <factor>` budgets a frame at a multiple of the mean (4, 0 off) and fails the
run naming every bad frame, `--attribute` turns on the `SystemMonitor` so a frame carries its
slowest system, `--fps <n>` fixes the step and `--light-churn <n>` changes n static lights a frame
so their probes rebake. `src/monitor/spike.c` is the detection, with 10 of the 33 passing
`test/monitor` cases on it; `tools/integration.py bench` reads a `<scene>.args`, passes `--spike 4
--fps 60 --attribute` and fails on a spike, as does `tools/epic.py` in `bench_scenes`.

`spikes.flecs` runs the stressors at once over 40k static blocks: 30k `Prop` and 400 `Icon`
template instances expanding incrementally, each icon rasterizing a 256 px inline SVG on jobs, 48
static point lights whose probes rebake and a camera on `Velocity` that rebuilds the shadow cascade
every frame. Baseline of 300 frames, churn 1-1000, 60 Hz step, mean / p99 / max / budget in ms:
static_100k 2.23 / 4.42 / 4.79 / 8.92, material_100k 2.13 / 4.30 / 4.47 / 8.52, templates_100k 1.12
/ 1.99 / 2.35 / 4.48, spikes 7.37 / 17.43 / 24.98 / 29.49; none spikes. Its tail frames attribute
to `box.surface.BoxSurfaceAcquire`, the main thread waiting on the GPU, which leaves the 15% of
headroom; LOD generation is out, its epic has not shipped.
