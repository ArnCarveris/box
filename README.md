# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU. Scenes, assets and configuration are entities and components; the
engine reacts to component changes. See `design/DESIGN.md` for the full brief.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3) on the path
- The flecs repository checked out at `../../flecs`
- A Rust toolchain (wgpu-native is built with cargo)
- For wasm builds: emsdk at `~/GitHub/emsdk` or `$EMSDK`

## Build and run

```sh
bake3 --local-env                                          # build the library, app and tests
bake3 run box_app --local-env --cfg release                # open a window with the default scene
bake3 run box_app --local-env -- etc/scenes/capture.flecs  # run a scene (paths are relative to apps/box_app)
bake3 run box_app --local-env -- --headless --frames 10    # headless run, exits after 10 frames
```

Options: `--headless`, `--frames <n>`, `--timeout <s|infinite>`, `--cpu-budget <0..1>`,
`--fps <n>`. Headless runs require `--frames` or `--timeout`, write their temporary
files to `/tmp/box/<pid>` and refuse to start when the machine's load exceeds its
core count. A scene that sets `Surface.write_to_file` writes each frame as PNG:

```
using box.surface
$ { Surface: {width: 640, height: 360, write_to_file: "/tmp/frame.png"} }
```

## Scenes

Scenes are flecs scripts. `apps/box_app/etc/scenes/primitives.flecs` shows every
primitive shape with materials, a camera and a directional light:

```
using box.transform
using box.geometry
using box.material
using box.camera

camera { Position: {0, 4, -12}  LookAt: {0, 0.5, 0}  Camera: {fov: 55} }
sphere { Position: {-3, 1, 0}  Sphere: {segments: 32, smooth: true, radius: 1}  Rgb: {80, 160, 240} }
```

Entities are static unless they or an ancestor have `DynamicTransform` or
`DynamicMaterial`; static entities are only re-uploaded when a component changes.
`Velocity3` and `AngularVelocity3` from `box.movement` mark entities dynamic.

## Tests

```sh
bake3 run test/core --local-env -- -j 12                     # all unit tests
bake3 run test/core --local-env -- Args -j 12                # one suite
bake3 run test/core --local-env -- Args.fps                  # one case
bake3 run test/core --local-env -- -j 12 --json report.json  # with JSON report
bake3 run test/transform --local-env -- -j 12                # other test projects: geometry, render
```

Integration scenes live under `test/integration`: `visual/` scenes are rendered
headless to screenshots and `bench/` scenes are run by the benchmark app:

```sh
python3 tools/integration.py visual            # writes test/integration/out/<scene>.png
python3 tools/integration.py bench             # writes test/integration/out/<scene>.json
bake3 run box_bench --local-env --cfg release -- ../../test/integration/bench/static_100k.flecs --churn 1-1000
```

## WebAssembly

```sh
bake3 build box_app --target em --local-env --cfg release
bake3 run box_app --target em --local-env --cfg release   # serves the page on http://localhost:8080
```

The page accepts engine arguments in the URL hash, e.g. `box_app.html#--frames 600`.
Every completed epic ships a wasm build under `project/db/<epic>/deliverable/wasm/`;
serve that folder with any static file server and open `box_app.html` in a
WebGPU-capable browser.

## Coding style

`design/coding_style.c` is the reference and `tools/lint.py` enforces it. Run
`python3 tools/lint.py` before committing (`--fix` reformats signatures and blank
lines). See `CLAUDE.md` for the full rule list.

## Project status page

```sh
python3 project/pm.py serve            # http://127.0.0.1:8090
python3 project/pm.py status           # print the tree in the terminal
```

Work is tracked as epics, tasks and subtasks under `project/db`. Each item has
a `README.md` with its goal and a `progress.json` with its status and timing;
tasks and epics keep test reports, screenshots and notes in `deliverable/`.

```sh
python3 project/pm.py new task <epic>/<task> "Title" --brief "One sentence." --deps <epic>/<other>
python3 project/pm.py start <epic>/<task>
python3 project/pm.py done <epic>/<task>
python3 project/pm.py check            # validates READMEs, statuses and dependencies
```

## Layout

```
apps/box_app/   the engine executable, its scenes under etc/ and the wasm shell page
design/         design brief and datamodel sketches (documentation only)
docs/           architecture notes
include/box/    public headers, one per module
project/        project management tool and its database
src/            engine sources, one directory per flecs module (see docs/ARCHITECTURE.md)
test/           unit test projects
```
