# Project management tool

Tool and web page for tracking epics, tasks and subtasks stored under project/db.

## Details

The project state lives as folders under `project/db`: one per epic, one per task inside an epic, and one per subtask inside a task. Every folder has a `README.md` describing the goal and a `progress.json` with kind, status, timing and dependencies. Tasks and epics have a `deliverable` folder for test reports, screenshots, benchmarks and short notes.

`project/pm.py` walks that tree and serves a web page with a board view and a scrollable timeline. The same script creates items and moves them between statuses so every agent records work the same way. bake3 gained a `--json` option so each task can attach the unit tests it ran as a deliverable.
