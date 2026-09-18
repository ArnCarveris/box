# Job system

`box.job` runs large tasks off the main thread. `JobSettings` holds `workers` (0 is the
core count minus one, negative none), `budget_ms` (2, 0 for no limit),
`prioritize_near`, `complete_on_shutdown` and the `eye`; `JobStats` reports the queue,
the completions of a frame, the drops and the worst completion in ms.

`src/job/job_internal.h` is the API for other modules: `box_job_submit` (a run callback
for a worker, an optional complete callback for the main thread, a context, a priority,
a world position, a group), `box_job_cancel`, `box_job_supersede` (drops the queued jobs
of a group first), `box_job_flush` and `box_job_set_eye`. The queue is a binary heap on
priority, then the squared distance to the eye at submit time. `BoxJobUpdate` runs
completions in PreUpdate until the budget is gone, always at least one; the rest waits
for the next frame. Cancel drops a queued job and the completion of a running one; at
teardown workers are joined, queued jobs dropped and completions run only with
`complete_on_shutdown`. Without workers (wasm has no pthreads, `workers` negative on
native) that same drain runs the jobs on the main thread inside the budget.

20 tests pass, also under `--cfg sanitize`; 100k empty jobs submit and run in 97 ms
(`Job.submit_100k`, 944k jobs/s, 15 workers on 16 cores) and a frame that drains a 10k
completion backlog costs 2.01 ms, worst 2.03, against its 2 ms budget
(`Job.frame_drain`). SVG submits per texture with `box_job_supersede`, the bake per
probe with its position; both still need `src/render` to call `box_job_set_eye`, which
this task left alone.
