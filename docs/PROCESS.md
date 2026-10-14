# Process

How this project is coordinated. `project/pm.py` encodes the loop; this page is the
part that does not fit in a command. Read `CLAUDE.md` for the house rules and
`README.md` for the build, test and benchmark commands.

## The loop

1. **Plan.** Break the work into epics, tasks and subtasks and write the README
   first: a title, a brief of at most two sentences, a `## Details` section of at
   most three paragraphs that names the components, the files and how the task is
   verified, and an optional `## References`. A task is one unit of work an agent
   can finish and test on its own.

   ```sh
   python3 project/pm.py new epic sky "Sky" --priority 120
   python3 project/pm.py new task sky/atmosphere "Atmosphere rendering" \
       --brief "One sentence." --deps views/view,effects/shader_component
   ```

   Dependencies are what the task needs to exist, not what it is near. An epic
   name as a dependency means the whole epic. Lower priority runs first; the
   status page writes it back with the box in the epic panel.

2. **Pick.** `python3 project/pm.py next 5` prints the ready, unstarted tasks in
   the derived order with their epic priority and dependencies. Take from the top
   unless something external argues otherwise.

3. **Start.** `python3 project/pm.py start <epic>/<task>` stamps the start time,
   which is what the board and the timeline sort on.

4. **Brief.** `python3 project/pm.py brief <epic>/<task> --files <owned paths>`
   prints the whole agent prompt: the task README, the epic README, the
   deliverable reports of every dependency, and the rules (style and linter,
   flecs pitfalls, the private environment, file ownership, visual baselines,
   test conventions, commits, the deliverable and the report format). Add
   `--env <name>` when the default (the task name) collides, `--worktree` when
   another agent is editing the same tree and `--model opus|sonnet` to record
   which model to launch; the launch line goes to stderr, the prompt to stdout.

   Fill `--files` yourself. It is the only thing the tool cannot derive and the
   only thing that keeps two agents from overwriting each other.

5. **Run.** One or two agents at a time, with **disjoint** file ownership. Two
   agents in the same directory is fine, two agents in the same file is not. When
   two tasks need the same file, sequence them instead. Give a cheap, well
   specified task to sonnet and keep opus for design work.

6. **Headless.** An agent never opens a window: `boxrun`, benches and scenes run with
   `--headless` and `--frames` or `--timeout`, are driven over the command port and are
   checked through captures. `pm brief` prints the rule; a windowed run takes over the
   keyboard and the screen of whoever is working on the machine.

7. **Verify.** Never take a report at face value: rerun the suite it claims
   (`bake3 run test/<project> --local-env=<name> -- <Suite> -j 8`), open the scene
   it changed, and run `python3 tools/lint.py`. A claim that is not reproduced is
   not done.

8. **Deliver.**

   ```sh
   python3 project/pm.py deliver sky/atmosphere --tests render:Atmosphere,core --bench render
   ```

   It writes one report per project into the task's `deliverable/`, merges them
   into `tests.json`, copies the benchmark report to `bench.json` and exits non
   zero when anything failed. Add a `deliverable/README.md` of under 20 lines and
   the screenshots of the task's own scene.

9. **Close.** `python3 project/pm.py done <epic>/<task>` refuses while
   `tests.json` or `bench.json` is missing and prints what it wants; a task that
   changes nothing that runs per frame sets `"bench": false` in its
   `progress.json` instead. `--force` completes anyway and says so.

## Closing an epic

`pm done <epic>` wants `deliverable/README.md` with the sections **What was
built**, **Verification**, **Lessons** and **Not in this epic** in that order and
under 200 lines, a merged `tests.json`, a `bench/` folder, an `integration/`
folder or screenshots, `wasm/boxrun.wasm`, and every task complete or discarded.
`tools/epic.py` produces all of it in one run:

```sh
python3 tools/epic.py <epic> --env <name> [--baseline <previous epic>]
```

Its steps, each writing into `project/db/<epic>/deliverable`, are `build_debug`
and `build_release`, which build `apps/boxrun` (and with it the library) in debug and
release and keep the bake3 `--build-json` reports as `build_debug.json` and
`build_native.json`; `tests`, one `BOX_TEST=1 bake3 run test/<project> --json` per
project under `test/` with a `project.json` into `tests_<project>.json`, merged into
`tests.json` the way `pm deliver` does; `visual` and `perf`, which run
`tools/integration.py` into `integration/` (`index.html` and `visual.json`, `perf.json`);
`bench`, which runs `tools/bench.py` over every bench project into `bench/` against the
`bench/` folder of the baseline epic, by default the last completed one;
`shaders`, which checks every shader in `etc/shaders` with Tint's WGSL rules through
`tools/wgsl_check.mjs` into `shaders.json` (native wgpu does not enforce uniformity, the browser
does); and `wasm`, which builds the web image in release with its `build_wasm.json` report and
copies `boxrun.html`, `boxrun.js` and `boxrun.wasm` into `wasm/`. `--steps a,b` and
`--skip a,b` rerun part of a pipeline after a failure.

The run prints one line per step, keeps the output of every step in
`.bake/epic/<epic>/<step>.log` and exits non zero when a build, a test or a scene
fails, then lists what `pm done` still wants. Write the README yourself and close
with `python3 project/pm.py done <epic>`. The wasm image is what gives the
epic its **Inspect** link on the status page: the page serves
`apps/box_inspector/index.html` at `/inspector` and opens it on that build, which
is how an old epic stays explorable after the code has moved on. The three build
reports are what the **Build** tab of the status page charts over epics.

## Environments

Every agent builds in its own environment, named after its task:

```sh
bake3 --local-env=<name>
bake3 run test/core --local-env=<name> -- -j 8
```

The shared environment is for nobody in particular, so two agents that use it
trade corrupted objects. While another agent is mid edit, verify in a worktree
instead of the live tree:

```sh
git worktree add ../box-<name> HEAD
mkdir -p ../box-<name>/.bake && cp -Rc .bake/bundles ../box-<name>/.bake/bundles
```

`cp -Rc` clones the fetched dependencies copy on write, so the worktree does not fetch
them again. Do not symlink them: the bundles are built in place, and two trees sharing
one folder rebuild wgpu and meshoptimizer under each other and break each other's links.
Commit from the main tree with explicit paths (`git add <path>`), never `-A`, check
`git diff --cached --stat` before committing because the index is shared with other
agents, and retry when another agent holds `index.lock`.

`pm serve` loads `pm.py` once, so restart it after any change to the tool or the
page before believing what the browser shows.

## Visual baselines

`python3 tools/integration.py visual` renders every scene under
`test/integration/visual` and compares it with `baseline/<scene>.png`; it fails
when the largest channel delta is above 24 and more than 200 pixels differ by
more than 16, so GPU speckle passes. A task looks at its own scene only and
refreshes that baseline with `--update-baseline <scene>` when the change is
intended. Every other scene must stay bit identical, which is the cheapest
regression test the project has.

## Noise

- **System load.** Benchmarks and frame timings taken while several agents build
  read 10-20% slow. Compare runs from the same session, never across days.
- **Stuck test processes.** A crashed case can leave a process spinning at 100%
  CPU; it poisons every timing afterwards. Check for one before trusting a
  regression and kill it.
- **Benchmark drift.** `tools/bench.py --baseline <dir>` prints REGRESSION and
  IMPROVEMENT per case beyond 5%. Treat a single case beyond the threshold as
  noise until a rerun on a quiet machine repeats it, and measure the pre change
  binary in the same session before blaming a change.
