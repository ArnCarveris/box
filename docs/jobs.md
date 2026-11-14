# Jobs

`box.jobs` (`src/jobs`) is the worker pool box modules use for long work off the main thread:
file IO and decoding (textures, fonts and glyph fields, environment images, sounds, music) and
picking BVH builds. Internal: the header is `src/jobs/jobs.h` (modules include
`"../jobs/jobs.h"`), not part of `include/box`. `BoxJobs` is imported by `Box` and by the
modules that use it.

## API

```c
typedef struct box_job_desc_t {
    box_job_work_t work;         /* void (*)(void *ctx): worker thread, required */
    box_job_complete_t complete; /* void (*)(ecs_world_t*, void *ctx): main thread, optional */
    box_job_free_t ctx_free;     /* void (*)(void *ctx): main thread, optional */
    void *ctx;
} box_job_desc_t;
```

| Function | Use |
|----------|-----|
| `int box_job_submit(world, &desc)` | Queue a job (world or stage). First job starts the workers. Nonzero = not queued (box.jobs not imported, no `work`) |
| `int box_jobs_set_workers(world, n)` | Before the first job: n threads, 0 = run inline, `BOX_JOBS_AUTO` (-1, default). Nonzero once workers started |
| `int32_t box_jobs_pending(world)` | Jobs whose completion hasn't run yet |
| `int32_t box_jobs_running(world)` | Jobs a worker took whose work isn't marked done yet |
| `int32_t box_jobs_worker_count(cores, cpu_budget)` | Pool size formula (below) |

Pattern (adapted from `src/text/font.c`; `module_working` stands for the module counter):

```c
module_working(sync, 1);
if (box_job_submit(world, &(box_job_desc_t){
    .work = load_work, .complete = load_complete, .ctx_free = load_free, .ctx = job
})) {
    module_working(sync, -1);
    load_free(job);
}
```

## Rules

- `work` runs on a worker and must not touch the world. Pass everything it needs in `ctx`; apply
  results in `complete`.
- `complete` runs on the main thread in `box.jobs.Complete` (`EcsOnLoad`, immediate) of a later
  frame, then `ctx_free`.
- Completions run in submission order and stop at the first job whose work hasn't finished: one
  slow job holds back the completions of every job submitted after it.
- When submit fails, `ctx_free` is not called: the caller frees `ctx` and undoes its counters.
- World deletion: queued work is dropped, running work finishes, `complete` never runs, `ctx_free`
  runs for every job.
- With 0 workers (`box_jobs_set_workers(world, 0)`, always in the browser) the queued work runs in
  `box.jobs.Inline` (`EcsPostFrame`), its completion in the next frame's `OnLoad`.

## Pool size (`BOX_JOBS_AUTO`)

`cores - BOX_JOBS_RESERVED_CORES` (3, for main, render and audio threads), clamped to
`1..BOX_JOBS_MAX_WORKERS` (16). In a headless, paced run it is also capped at
`cpu_budget / 100 - 1`, so the default budget of 100% gets 1 worker. Browser: always 0.

## Headless determinism

A headless frame (without `--async-loads`) waits until its module's own counter and
`box_jobs_running(world)` reach 0, since work that signals its module before returning is not yet
visible to `box.jobs.Complete`. Texture, text and sound modules do this with a timeout, after
which they warn with what is still loading.
