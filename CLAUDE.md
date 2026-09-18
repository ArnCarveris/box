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
- `ecs_os_malloc` needs `ecs_os_init()` (or a world) before use.

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

An epic attaches one report per bench project plus a baseline comparison against
the previous epic:

```sh
python3 tools/bench.py --env <name> --out project/db/<epic>/deliverable \
    --baseline project/db/<previous epic>/deliverable
```

## Visual scenes

`python3 tools/integration.py visual` compares every render byte for byte with
`test/integration/visual/baseline/<scene>.png` and fails on a difference. Do not
view screenshots of scenes you are not working on; a task only inspects its own
scene and refreshes that baseline with `--update-baseline <scene>` when the change
is intended. Every other scene must keep matching.
