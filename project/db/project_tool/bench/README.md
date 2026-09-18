# Native benchmark projects

bake3 bench projects for the engine modules, first benchmark suites, and benchmark reports as part of every task deliverable.

## Details

bake3 gained native benchmark projects (a `bench` section in `project.json`, generated harness main, `bake_bench.h` with `bench_iter`, `bench_keep`, `bench_pause` and `bench_resume`, and `--json` reports with median, confidence interval, outliers and raw samples). Box gets one benchmark project per engine module under `bench/<module>` mirroring the `test/<module>` layout, starting with `bench/transform` (static and dynamic propagation over subtrees), `bench/geometry` (mesh generation per shape and cache lookups), `bench/render` (extract of static deltas and dynamic instances at 10k and 100k entities) and `bench/command` (dispatch and path resolution).

Benchmarks become part of the development process: `CLAUDE.md` and `README.md` state that a task touching a hot path adds or updates a benchcase and attaches its report as `deliverable/bench.json`, and `project/pm.py check` warns for a completed task that changed `src` without one; the epic deliverable holds a `bench.json` per benchmark project and a baseline comparison against the previous epic. The status page benchmark endpoint reads the bake3 report format next to the scene benchmarks.

## References

- bake3 `README.md`, section "Benchmarks"
- `test/*/project.json` for the mirrored layout
