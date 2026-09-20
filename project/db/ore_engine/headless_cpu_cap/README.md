# Headless runs exceed their CPU budget

A headless box process reached about 500% CPU; the CPU budget must cap the whole process at one core by default, workers and renderer thread included, so agents cannot oversubscribe the machine.

## Details

`App.cpu_budget` defaults to 1.0 and `box_run` only calls `box_cpu_pace` when the budget is
below 1.0 (`src/core/run.c`), so by default nothing paces a headless run at all, and
`box_cpu_pace` compares process CPU time with wall time, which a process with job workers and
a renderer thread exceeds many times over. The oversubscription guard in `src/core/cpu.c` only
refuses to start when the load average already exceeds the core count, which is after the
damage. Make the budget a hard cap of the whole process: in headless mode the pacing engages
at every budget including 1.0, the measure is the CPU time of all threads (which
`box_cpu_process_time` already returns if it uses the process rusage; verify), the job pool
starts at most `ceil(budget)` workers when a budget is set, the flecs pipeline stays single
threaded, and the renderer thread is paced with the frame instead of running free. A
windowed run keeps today's behaviour unless `--cpu-budget` is passed. Print the budget and the
measured share at exit in headless mode so a report can quote it.

Verification: a `test/core` case that a headless world with a busy job keeps its process CPU
time under budget times wall time over a second, the existing CPU budget cases green, and a
headless default scene run with `--timeout 10` reporting a share of at most 1.05 in the
deliverable, with `bench/job` attached since the pool changes.

## References

- `src/core/run.c`, `src/core/cpu.c`, `src/core/core.c`, `src/job/pool.c`, `src/job/job.c`,
  `docs/ARCHITECTURE.md` (Headless and agents), `project/db/responsiveness/deliverable/README.md`
