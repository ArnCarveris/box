# Epic deliverable tool

tools/epic.py runs an epic's whole deliverable pipeline, including native and wasm build reports, unit test reports, scenes, benchmarks with baseline and the wasm image.

## Details

Closing an epic has been a hand run script: build debug and release, run every test project with a JSON report, run the visual, bench and command scenes, run the microbenchmarks against the previous epic's baseline, build the wasm image and copy everything into the epic's deliverable folder. `tools/epic.py <epic> [--baseline <epic>] [--env name]` does all of it, writes the merged `tests.json` and the per project reports, copies the integration outputs, and adds `build_native.json` and `build_wasm.json` produced with bake3's `--build-json` for the release native build and the wasm build, plus `build_debug.json` for the debug build the tests use. It prints a one screen summary and exits non zero when a test, a scene or a build fails. `CLAUDE.md` and the README point to it as the way to close an epic.

Verification: the tool is run on the current in progress epic to produce its deliverable, and a tracker test checks that the deliverable listing recognises the build reports.

## References

- `tools/integration.py`, `tools/bench.py`
- `project_tool/build_report`
