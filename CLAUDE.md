# Box project notes

Read `README.md` for build and run commands and `docs/ARCHITECTURE.md` for the
module layout. The design brief is `DESIGN.md`.

## Style

`design/coding_style.c` is the reference. `tools/lint.py` enforces it:

```sh
python3 tools/lint.py          # report violations in include, src, apps and test
python3 tools/lint.py --fix    # reformat signatures and blank lines, then report
```

Run the linter before every commit; it must report 0 problems. `test/transform` predates
the linter and is skipped. Rules it checks:
file header with `@file` and `@brief`, a doc comment with `@param` and `@return`
on every static function and header prototype, one parameter per line and the
opening brace on its own line, no comments inside function bodies (tests may
start with one `// epic/task` reference line), a blank line before `return` and
after every block, no `return` inside a nested block (use `goto done`), lines
under 100 characters, functions under 200 lines, files under 2000 lines, and a
doc comment on every type and member.

## Component values

- Zero values are literal: a component value applies exactly as specified, so an all-zero struct
  or a zero field never stands for a default (an all-zero CullSettings disables culling, a zero
  speed does not move). Defaults live in constructors (`ecs_set_hooks` ctor, composed with
  `box_owned_hooks`), which script `Foo: {...}` and `ecs_add` start from; a C literal starts from
  zero. Zero may only mean a documented mode that is its literal meaning, such as "0 for none".

## Flecs rules that bit us

- Import `Box` and nothing else, in applications, tests and benchmarks alike. Component
  ids are process globals every world forces, so a world that imports modules in another
  order collides with the ids of the first. A test whose subject is a minimal world may
  import a subset, because every test case runs in its own process.
- This flecs has no implicit defer mode. Inside an OnAdd or OnRemove observer
  the entity is mid table-move: structural changes to it (add, set of a missing
  component, remove) must go through `it->stage`, never `it->world`. Reads use
  `it->world`. OnSet observers may set components directly.
- `ecs_ensure` in a system on a stage returns a temporary copy; use
  `ecs_get_mut` for existing components and keep registries in heap structs.
- A struct registered with ECS_META_COMPONENT gets only a default ctor: flecs expects the
  application to supply dtor, copy and move. Every component with `char*` or `ecs_vec`
  members installs them with `box_owned_hooks` (box.types).
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
- `PointLight` and `SpotLight` default to intensity 1 through their ctor, but a C literal
  `ecs_set(e, PointLight, {.range = 5})` writes 0 into the intensity, which means off. Set
  the intensity in C; script `PointLight: {range: 5}` keeps the default.
- `View` constructs with `lighting.enabled = true` and culling enabled, but a C literal
  `ecs_set(e, View, {.camera = c})` writes 0 into `lighting` and `culling`, so the view renders
  unlit (base color and emission only) and draws every instance. Set `.lighting.enabled = true`
  and `.culling` in C; script `View: {camera: c}` keeps the defaults.

## Tests

Tests verify the requirements of DESIGN.md and the task spec through observable behaviour. They
never assert internals: struct sizes, strides, buffer, shader or resource counts, slot indices,
memory layout or private bookkeeping. An agent-written test that does, or that no requirement
motivates, is changed or deleted. Tests in `test/transform` were written by the user.

## Headless only

Never open a window. Run `boxrun`, benches and scenes with `--headless` and `--frames` or
`--timeout`, drive them over the command port and look at captures; a windowed run takes over
the keyboard and screen of the person working on this machine.

A headless `box_run` refuses to start when the load average exceeds the core count ("cpu
oversubscribed") and paces itself to one core; that protects the machine from app, bench and
scene runs, not from tests. Run test projects with `BOX_TEST=1` in front of the command
(`BOX_TEST=1 bake3 run test/core --local-env=<name> -- -j 8`, same for `pm.py deliver`), which
sets `App.unguarded` and `App.unpaced` in every new world; a test about the guard or the budget
sets those fields itself. Never export `BOX_TEST` in a shell that runs `boxrun`, benches or
`tools/integration.py`.

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

Every task goes through the review loop before it closes: a review agent writes
`deliverable/review/review.md` (findings `### R<n> [blocker|major|minor] title`, or `None.`),
an agent addresses each open finding and answers it under `## Round <k>` in `response.md`, and
a verifier, who may not raise new issues, writes `verification_r<k>.md` and `verification.md`
ending in `Verdict: ALL ADDRESSED` or `Verdict: OPEN: R<n>, ...`; an open verdict goes back
to the response step. Each artefact (and each response round) carries a `Date: <UTC time>`
line, and pm parses them strictly. Implementation agents run `pm deliver` but never `pm done`;
the coordinator runs the loop, delivers again and closes the task. `pm done` refuses a task
without a passing review or with reports older than it, even with `--force`; only
`--skip-review "<reason>"` closes it unreviewed, recorded in `progress.json`. `pm check`
reports a completed unreviewed task as a problem (a skipped one as a warning), and `pm review
<epic>/<task>` prints the state and the next step. Tasks started and completed before the
gate (`REVIEW_SINCE` in `pm.py`) are exempt; see docs/PROCESS.md.

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

It builds `boxrun` in debug, release and wasm and keeps the bake3 `--build-json`
reports as `build_debug.json`, `build_native.json` and `build_wasm.json`, runs
every project under `test/` into `tests_<project>.json` plus the merged
`tests.json`, runs the visual and perf scenes into `integration/`, runs
one benchmark report per project into `bench/` against the `bench/` folder of the
previous epic, checks every shader in `etc/shaders` with the browser's WGSL rules
(`shaders.json`, Tint through node; native wgpu does not enforce uniformity) and copies the web
image into `wasm/`, all under
`project/db/<epic>/deliverable`. It prints a line per step (the output of each step goes to
`.bake/epic/<epic>/<step>.log`), exits non zero when a build, a test or a scene fails and ends
with what `pm done <epic>` still wants; `--steps` and `--skip` rerun part of it. Write
`deliverable/README.md` by hand.

## Visual scenes

`python3 tools/integration.py visual` compares every render with
`test/integration/visual/baseline/<scene>.png` and fails when the difference is
strong and widespread (largest channel delta above 24 and more than 200 pixels
off by more than 16); GPU speckle passes. Do not view screenshots of scenes you
are not working on. A task inspects only its own scene and refreshes that
baseline with `--update-baseline <scene>` when the change is intended; every
other scene must keep matching. See `test/integration/visual/README.md`.
