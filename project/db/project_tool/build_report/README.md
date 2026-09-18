# bake3 build timing report

A bake3 option that writes a JSON report of a build with the time of every step (discovery, each compiled file, linking, bundle steps, etc install) and the git SHA.

## Details

`bake3 build --build-json <file>` (also accepted by `bake3` with no command and by `run`, `test` and `bench` for the build they trigger) writes one JSON document per invocation: tool version, timestamp, host, configuration, target, environment, the git SHA and dirty flag of the workspace when it is a repository, and a tree of steps with name, kind, start and duration in seconds: workspace discovery, dependency resolution, per project the bundle fetch and build steps, each compiled source file with its object path, code generation steps such as test harness mains, linking, the etc install, and the wasm link and embed steps for the emscripten target. Totals per kind and per project are included so a page can show them without walking the tree. The report is written even when the build fails, with the failing step marked.

Verification: bake3 self tests for the report on a native and a wasm build, the failure case, the git fields with and without a repository, and the totals; the bake3 README documents the format. The installed binary is rebuilt.

## References

- bake3 `README.md`, the `--json` test report added earlier as the precedent for the shape
