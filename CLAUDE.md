# Box project notes

Read `README.md` for build and run commands and `docs/ARCHITECTURE.md` for the
module layout. The design brief is `design/DESIGN.md`.

## Style

`design/coding_style.c` is the reference. `tools/lint.py` enforces it:

```sh
python3 tools/lint.py          # report violations in include, src, apps and test
python3 tools/lint.py --fix    # reformat signatures and blank lines, then report
```

Run the linter before every commit; it must report 0 problems. Rules it checks:
file header with `@file` and `@brief`, a doc comment with `@param` and `@return`
on every static function and header prototype, one parameter per line and the
opening brace on its own line, no comments inside function bodies (tests may
start with one `// epic/task` reference line), a blank line before `return` and
after every block, no `return` inside a nested block (use `goto done`), lines
under 100 characters, functions under 200 lines, files under 2000 lines, and a
doc comment on every type and member.

## Flecs rules that bit us

- This flecs has no implicit defer mode. Inside an OnAdd or OnRemove observer
  the entity is mid table-move: structural changes to it (add, set of a missing
  component, remove) must go through `it->stage`, never `it->world`. Reads use
  `it->world`. OnSet observers may set components directly.
- `ecs_ensure` in a system on a stage returns a temporary copy; use
  `ecs_get_mut` for existing components and keep registries in heap structs.
- Components without a ctor are not zero initialized. Register
  `.ctor = flecs_default_ctor` for every private component with flags.
- During deletion the OnRemove destination table is the empty root table, not
  NULL; test `ecs_table_get_type(it->other_table)->count == 0`.
- Never mutate entities while iterating `ecs_children`; collect them first.
- A system that adds a component to an entity must do it through `it->stage`.
- `ecs_os_malloc` needs `ecs_os_init()` (or a world) before use.
- A template async block with two sequential awaits restarts on a `mut`
  reassignment; use one async block per await.
- Writing a component through `ecs_get_mut` or `ecs_ensure` raises no OnSet;
  call `ecs_modified` when observers must see the value.

## Build environments

Use a private environment when working next to other agents:

```sh
bake3 --local-env=<name>
bake3 run test/core --local-env=<name> -- -j 8
```

## Tracking work

Work is tracked with `project/pm.py` (see README). A task is complete only when
its `deliverable/tests.json` exists, produced with `--json` from the affected
test suites.

`docs/PROCESS.md` describes the coordination loop that `pm next`, `pm brief`,
`pm deliver` and `pm done` encode; brief every agent with `python3 project/pm.py
brief <epic>/<task> --files <owned paths>` instead of writing a prompt by hand.

A task that touches a hot path (transform propagation, mesh generation, extract,
command dispatch) adds or updates a benchcase under `bench/<module>` and attaches
the report of the affected bench project as `deliverable/bench.json`:

```sh
bake3 run bench/transform --local-env=<name> --cfg release -- \
    --json project/db/<epic>/<task>/deliverable/bench.json
```

`project/pm.py check` warns for a completed task outside the `project_tool` epic
that has no `deliverable/bench*.json`; the warning does not fail the check, so
skip it when the task changed nothing that runs per frame.

An epic is closed with `tools/epic.py`, which runs the whole deliverable pipeline:

```sh
python3 tools/epic.py <epic> --env <name> [--baseline <previous epic>]
```

It builds `box_app` in debug, release and wasm and keeps the bake3 `--build-json`
reports as `build_debug.json`, `build_native.json` and `build_wasm.json`, runs
every project under `test/` into `tests_<project>.json` plus the merged
`tests.json`, runs the visual, bench and command scenes into `integration/`, runs
one benchmark report per project into `bench/` against the `bench/` folder of the
previous epic and copies the web image into `wasm/`, all under
`project/db/<epic>/deliverable`. It prints a line per step, exits non zero when a
build, a test or a scene fails and ends with what `pm done <epic>` still wants;
`--steps` and `--skip` rerun part of it. Write `deliverable/README.md` by hand.

## Visual scenes

`python3 tools/integration.py visual` compares every render with
`test/integration/visual/baseline/<scene>.png` and fails when the difference is
strong and widespread (largest channel delta above 24 and more than 200 pixels
off by more than 16); GPU speckle passes. Do not view screenshots of scenes you
are not working on. A task inspects only its own scene and refreshes that
baseline with `--update-baseline <scene>` when the change is intended; every
other scene must keep matching. See `test/integration/visual/README.md`.
