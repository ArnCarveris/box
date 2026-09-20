# Headless runs exceed their CPU budget

## Built
`box_run` paces every headless run: `App.cpu_budget` below 1 is the budget, otherwise a
headless run takes 1.0 and a windowed run stays unpaced. Before the loop a headless run sets
`JobSettings.workers` to `ceil(budget)` when the settings ask for the core count or for more
(a scene that asks for fewer, or for none like the visual harness, is left alone) and `box.job`
applies the settings on set (new OnSet observer in `src/job/job.c`), so the pool no longer
starts one worker per core; the flecs
pipeline was already single threaded and the renderer thread already blocks on the frame
queue. At exit a headless run prints `box: cpu budget B, share S (cpu over wall)`.

## Measured
A `sample` of the old process showed 15 job workers each awake ~27% of the time in
`pthread_cond_wait` churn: `box_job_pool_configure` broadcasts the wake condition every
frame and an uncapped headless loop runs thousands of frames per second.

| run (default scene, `--timeout 5`) | cpu / wall | share |
|---|---|---|
| before, debug | 26.3 s / 5.28 s | 4.98 |
| after, debug | 3.38 s / 5.00 s | 0.68 |
| after, release | 3.00 s / 5.00 s | 0.60 |

## Verified
`test/core` App: `headless_default_budget_caps_busy_jobs` submits 8 busy jobs at frame 0
and asserts process CPU under 1.5 x wall over a one second run (prints share 1.00); the
existing budget cases stay green. `bench/job` attached as `bench.json`.

## Left out
The per frame wake broadcast is still there; `ore_engine/idle_cpu_load` owns the idle
attribution and may drop it.
