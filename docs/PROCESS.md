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

   Write the README, the review artefacts and the report back to the coordinator
   one line per paragraph or list item, never hard-wrapped at a column width: the
   status page renders them as markdown, and a line break inside a paragraph only
   breaks it up.

9. **Review.** The implementation agent stops after `pm deliver` and never runs
   `pm done`. The coordinator puts every task through the review loop described in
   [Closing a task](#closing-a-task) and closes it once the verification passes.

10. **Close.** `python3 project/pm.py done <epic>/<task>` refuses while
    `tests.json` or `bench.json` is missing, while the review loop has not passed, or
    while a report predates the review, and prints what it wants; a task that
    changes nothing that runs per frame sets `"bench": false` in its
    `progress.json` instead. `--force` completes past a missing deliverable and says
    so, but never past the review loop: only `--skip-review "<reason>"` closes a task
    whose review has not passed, and it records the reason in `progress.json`.
    Neither does `--force` close an item whose test reports do not pass: see
    [Passing tests](#passing-tests).

## Passing tests

`pm done` (task or epic) reads every top level `deliverable/tests*.json` (`tests.json`, `tests_<project>.json`, ...) and refuses when one reports a failed case, an empty case (`empty` above 0, or a case with status `empty`), an empty run (no case passed or failed, or an empty `tests` list) or is not a test report at all. A missing `tests.json` stays a deliverable gap that `--force` passes; a failing report is not, and `--force` never overrides it.

The one way past it is `--waive-tests "<reason>"`, which closes the item and records `tests_waived` (the reason, the time and the failures) in `progress.json`, the way `--skip-review` records `review_skipped`; it is not recorded when every report passes, and reopening the item drops it. `pm check` reports every completed task or epic with a failing report as a problem and one closed with `--waive-tests` as a warning that names the reason; `pm status` and `pm review` print the reason too. A delivered task whose reports fail is in the stage **tests failing** until a passing `pm deliver`.

## Closing a task

After each task:

1. **Review.** A review agent reads the change (`git diff <range>`), the task and
   epic README, `DESIGN.md` and `CLAUDE.md`, and writes
   `deliverable/review/review.md`: a `Date: <UTC time>` line, then a `## Findings`
   section with one `### R<n> [blocker|major|minor] <title>` block per finding (File,
   Problem, Expected), or `None.` when there is nothing to fix.
2. **Respond.** An agent addresses every finding left open, by fixing the code with a
   test that proves it or by explaining with evidence why it is not a defect, and
   appends a `## Round <k>` section to `deliverable/review/response.md`: a
   `Date: <UTC time>` line and one `R<n>: fixed in <commit> — ...` or
   `R<n>: not a defect — ...` line per finding. Round 1 answers every finding, round
   k answers exactly the findings verification round k-1 left open.
3. **Verify.** A verifying agent judges only the findings of the response round, never
   new ones, and writes `deliverable/review/verification_r<k>.md`, copied to
   `verification.md`: a `Date: <UTC time>` line, one
   `R<n>: ADDRESSED|NOT ADDRESSED — <evidence>` line per finding and, as the last
   line, `Verdict: ALL ADDRESSED` or `Verdict: OPEN: R<n>, ...` listing exactly the
   findings it marks NOT ADDRESSED. An open verdict goes back to step 2 for round k+1.
4. **Deliver.** Once the verdict is `ALL ADDRESSED` (or the review had no findings),
   rerun `pm deliver`, so `tests.json` and `bench*.json` postdate the review, and
   close with `pm done`. The review folder is part of the deliverable and shows in
   the task's deliverable listing on the status page.

Like the README, review.md, response.md and the verification files are written one
line per paragraph or list item (a finding's File, Problem and Expected are one line
each), never hard-wrapped.

The status page shows a task in three tabs: **Summary** (the deliverable README
rendered as markdown, status, kind, test and bench counts and the deliverable files),
**Review** (every finding as a card with its File, Problem and Expected and, per round,
the response and the verification judgement with its evidence, plus the verdict) and
**Process** (every step with its time: created, started, delivered, review, each
response and verification round, redelivered, done). The tabs read
`/api/report?path=<epic>/<task>`, which pm builds from `progress.json`, the `Date:`
lines of the review artefacts and the report timestamps, never from git. An epic
shows its deliverable README, a review overview of its tasks and a task timeline.

Every date is UTC in ISO 8601 (`date -u +%Y-%m-%dT%H:%M:%SZ`) and is written when the
artefact is written. These dates and the `timestamp` that `pm deliver` puts in every
report are the one clock of the loop: committing, copying or touching the files
never changes it, so a task that passed `pm done` stays clean in `pm check`.

`python3 project/pm.py review <epic>/<task>` prints the state of the loop: the
findings by severity, the response rounds, the open findings, what is missing and
the next step (review, respond, verify, repair, deliver or done). `pm done` enforces
it strictly:

- every line that looks like a finding, answer, judgement, verdict or date must be
  in the exact form above; anything else, a duplicate id, a finding judged twice or a
  second verdict is a gap, never skipped;
- the rounds are replayed in order: verification round k needs response round k,
  response round k answers what round k-1 left open, every round file exists and
  `verification.md` equals the latest one, which ends in `Verdict: ALL ADDRESSED`;
- review, response round 1, verification round 1, response round 2, ... and the
  reports carry non decreasing dates.

`pm check` reports a completed gated task without a passing review as a problem, and
one closed with `--skip-review` as a warning that names the reason (`pm status` and
the state the status page reads carry it too). The gate (`REVIEW_SINCE` in `pm.py`)
covers every open task and every task started or completed on or after
2026-10-14T22:50Z; tasks started and completed before it are exempt.

## Stages

Every task has a stage, derived from its status and the artefacts `pm review` parses:

| Stage | When |
| --- | --- |
| not started | pending |
| implementing | started, no `deliverable/tests.json` yet |
| tests failing | delivered, but a `tests*.json` reports a failure, an empty test or an empty run |
| delivered | delivered, outside the review gate: `pm done` closes it |
| delivered, awaiting review | delivered under the gate, no `review/review.md` yet |
| review written, fix round 1 / fix round k | findings open, round k of `response.md` missing or incomplete |
| verification round k | response round k written, `verification_r<k>.md` missing |
| review artefacts malformed | a review artefact breaks the strict format or order (`pm review` lists the lines) |
| ready to close | the loop has passed, waiting for the redelivery after the review or for `pm done` |
| complete, complete (review skipped), discarded | closed |

An epic is **open**, **closing** once every task is complete or discarded but the epic is not done, or
**complete**, and counts its tasks per stage. The board shows the stage as a chip on every task card
and a summary per epic in the sidebar, the panels show it next to the status, and the Process tab
ends with the current stage after the last step. `/api/state` and `/api/report` carry it as `stage`.

`python3 project/pm.py stage` prints every open epic with its open tasks, `pm stage <epic>` an epic
and all its tasks, `pm stage <epic>/<task>` one task, each with what the stage waits for.

Task work happens in worktrees of the main checkout, so `pm serve` and `pm stage` also read
`project/db` from every worktree that `git worktree list` reports and show, per task, the copy with
the most recent activity (the latest of the `progress.json` times, the report timestamps and the
review `Date:` lines), labelled with the branch it came from; this checkout wins a tie. They only
read the worktrees, skip one that disappears while it is read, and serve its deliverable files
under `/worktree/<folder>/deliverable/`. A task that exists only in a worktree is not shown.

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
