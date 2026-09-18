# Epic deliverable tool

`tools/epic.py <epic> [--baseline <epic>] [--env name] [--steps a,b] [--skip a,b]` runs eight
steps into `project/db/<epic>/deliverable`: `build_debug` and `build_release` (box_app with
bake3 `--build-json` into `build_debug.json` and `build_native.json`), `tests` (every project
under `test/` into `tests_<project>.json`, merged into `tests.json`), `visual`, `bench_scenes`
and `command` (`tools/integration.py` into `integration/`), `bench` (`tools/bench.py` into
`bench/` against the last completed epic, resolved automatically) and `wasm` (`build_wasm.json`
plus the three box_app files into `wasm/`). It prints a line per step, exits non zero on a
failed build, test or scene and ends with what `pm done <epic>` still wants.

## Verified

- Run on `monitoring` in env `epic_tool`: 8 steps, 0 failed, 2.3 min. 880 unit tests over 12
  projects, 39 visual, 4 bench and 6 command scenes, 58 benchmark cases over 9 projects against
  the `controllers` baseline, and build reports of 114, 114 and 113 compiled files. The six
  `render` regressions it printed come from another agent's in flight work, not from this task.
- `project/test_pm.py --json`: 58 cases green (`tests.json`). No C changed, so lint is untouched.
- Left out: the tracker test that the deliverable listing recognises the build reports belongs
  to the Build page task, which owns `project/pm.py` and `project/test_pm.py`.
