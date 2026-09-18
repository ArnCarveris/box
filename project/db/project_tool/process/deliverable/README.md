# Process encoded in the tool

`pm next [n]` prints the ready unstarted tasks in the derived order with their epic
priority and dependencies. `pm brief <epic>/<task> [--files] [--env] [--model]
[--worktree]` assembles the whole agent prompt from the task README, the epic README,
the deliverable reports of every dependency task and epic, and nine rule blocks kept in
one place in `pm.py` (environment, ownership, style and linter, flecs pitfalls, tests,
visual baselines, commits, deliverable, economy) plus the report format.

`pm deliver <task> --tests <project>[:<suite>],... [--bench <project>,...]` runs bake3
with `--json` into the task deliverable, merges the reports into `tests.json` the way
the epic pipeline does, copies the `tools/bench.py` reports to `bench.json` and exits
non zero when a test fails or a run writes nothing. `pm done` refuses a task without
`tests.json` and `bench.json` (unless `"bench": false`), and an epic without the four
section report under 200 lines, the merged tests, `bench/`, the integration outputs,
the wasm image or with an open task; `--force` overrides with a warning. `pm check`
repeats it as warnings for tasks and problems for epics completed after this task was
created, so the 12 historical epics still pass. `docs/PROCESS.md` (144 lines) writes the
loop down for the next coordinator; 58 tracker tests green and `pm check` clean.
