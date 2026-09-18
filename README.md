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
bake3 --local-env                                   # build everything
bake3 run box --local-env --cfg release             # run the engine with the default scene
bake3 run box --local-env -- --headless --frames 10 # headless run, exits after 10 frames
```

Headless runs write their temporary files to `/tmp/box/<pid>`.

## Tests

```sh
bake3 run test/core --local-env -- -j 12                     # all unit tests
bake3 run test/core --local-env -- Transform -j 12           # one suite
bake3 run test/core --local-env -- Transform.parent_child    # one case
bake3 run test/core --local-env -- -j 12 --json report.json  # with JSON report
```

## WebAssembly

```sh
bake3 build box --target em --local-env --cfg release
bake3 run box --target em --local-env --cfg release   # serves the page on http://localhost:8080
```

Every completed epic ships a wasm build under its deliverable folder, see
`project/db/<epic>/deliverable/`.

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
design/     design brief and datamodel sketches (documentation only)
project/    project management tool and its database
src/        engine sources, one directory per flecs module
include/    public headers
test/       unit test projects
etc/        default scenes and assets
```
