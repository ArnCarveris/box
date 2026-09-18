# Process encoded in the tool

pm next, pm brief, pm deliver and a strict pm done encode the coordination loop, with docs/PROCESS.md for whoever runs it next.

## Details

The loop that has run this project lives in the coordinator's prompts: pick the next ready task, start it, brief an agent with the task spec, the reports of its dependencies and the house rules, verify, produce the deliverable, close, and close the epic with the full pipeline. The tool gets it: `pm next [n]` prints the ready tasks in derived order; `pm brief <task> [--files a,b]` prints the complete agent prompt (the task README, the deliverable READMEs of its dependencies, the style and flecs rules from `CLAUDE.md`, the private build environment named after the task, file ownership from `--files`, the visual baseline rule, the commit rules, the deliverable expectations and the report format) so a coordinator only fills in ownership; `pm deliver <task> --tests <project>[:<suite>] [--bench <project>]` runs bake3 with `--json` into the task deliverable and writes `bench.json`; `pm done` refuses a task without `tests.json` (and `bench.json` unless `bench: false`) and an epic without `README.md` in the report format (sections What was built, Verification, Lessons, Not in this epic, under 200 lines), `tests.json`, the integration outputs, the bench folder and the wasm image, printing what is missing; `pm done <epic>` also refuses when a task of the epic is still in progress.

`docs/PROCESS.md` describes the loop end to end for a new coordinator: how tasks and epics are written, how to brief and how many agents to run, the private environments, verifying in a worktree while another agent edits the tree, closing with `tools/epic.py` (queued in `project_tool/epic_tool`; until it exists the manual pipeline is described), restarting the tracker server, and the known noise sources. `CLAUDE.md` points to it in two lines.

Verification: tracker tests for `next`, the generated brief, `deliver` writing the files, every refusal of `done` and the epic README section check; the current in progress tasks keep passing `check`.

## References

- `project/pm.py`, `project/test_pm.py`, `CLAUDE.md`, `README.md`
