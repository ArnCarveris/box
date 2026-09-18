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
`--fps <n>`, `--port <n>` (command endpoints, default 27750, 0 disables the server).
Headless runs require `--frames` or `--timeout`, write their temporary
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

`apps/box_app/etc/scenes/capture_camera.flecs` adds `Capture: {path:
"/tmp/box/capture_camera.png"}` to its camera, which renders that camera to the
PNG once and removes the component again.

`test/integration/visual/lights_city.flecs` lights a grid of boxes with a few
hundred colored point lights and a handful of spot lights, which the clustered
forward path shades in one pass.

`test/integration/visual/pbr_spheres.flecs` is the image based lighting check: a
7 by 7 grid of spheres with roughness across the columns and metallic up the
rows under an `EnvironmentLight` with a blue sky and a warm ground.

`test/integration/visual/textured.flecs` is the texture check: a checkered box,
sphere and floor, a normal mapped box and a panel with a roughness ramp, bound
through `PbrTextures`. `apps/box_app/etc/textures/` holds the PNGs it loads, which
`python3 tools/gen_textures.py` regenerates.

`test/integration/visual/svg_icons.flecs` is the SVG check: the three hand written
icons in `apps/box_app/etc/svg/` rasterized by `Svg` at 64, 256 and 1024 pixels and
shown side by side on quads, so the same vector art is blocky on the left and crisp
on the right.

`test/integration/visual/tiling.flecs` is the tiling check: one checker texture
at one meter over boxes of one, two and six meters, a cylinder and a sphere, with a
second row that offsets the texture transform, mirrors and clamps the wrap mode and
tiles the same material at three sizes.

`test/integration/visual/blending.flecs` is the blend mode check: alpha blended,
additive, multiply, screen and subtractive panels overlapping each other and the
primitives behind them, drawn back to front. `milk_glass.flecs` holds a frosted
panel with opacity 0.5 and roughness 0.8 next to a clear one over the same
textured geometry, and `back_faces.flecs` shows three open boxes of quads, one
per `BackFaceMode`, with the camera looking into them.

`test/integration/visual/layers.flecs` is the material layer check: the same
textured crate and sphere six times, without layers and with dust, edge wear,
grime, wetness and all four at once. A layer is an entity under
`box.material.layers` with a `MaterialLayer` WGSL body and a `LayerParams`
holding its two scene global vectors; a material enables one with the pair
`(MaterialLayerOn, box.material.layers.dust)`. Dust takes its color and amount
from `a`, and noise frequency, roughness and up facing sharpness from `b`. Edge
wear takes the metal color and amount from `a` and the band width, roughness and
noise frequency from `b`. Grime takes its albedo multiplier and amount from `a`
and noise frequency and roughness from `b`. Wetness takes the wet roughness in
`a.x` and the amount in `a.w`, and noise frequency, pooling and noise weight from
`b`. `MaterialLayerSettings` turns every layer off at once.

`test/integration/visual/baked_room.flecs` is the baked lighting check: a tiled
room with static point lights, emissive boxes and a pillar whose shadow the bake
resolves, next to `dynamic_room.flecs` which lights the same room with the lights
marked `DynamicTransform`.

`test/integration/visual/layers_views.flecs` is the render layer check: red
objects in layer `a`, blue objects in layer `b`, a green pillar in both and a
light per layer, drawn by two views of the same camera. The main view lists
layer `a` and the second view lists layer `b` and renders into a `RenderTarget`
its camera captures. A layer is any entity an entity or a light joins with the
pair `(box.view.Layer, layer)`; `View.layers` and `View.shadow_layers` hold up to
eight layers each and show every layer when they are empty.

`test/integration/visual/culling.flecs` is the frustum culling check: a ring of
boxes and spheres around the camera with a row behind it, which `CullSettings`
on a view or as a singleton switches off for a bit identical comparison.

Entities are static unless they or an ancestor have `DynamicTransform` or
`DynamicMaterial`; static entities are only re-uploaded when a component changes.
`Velocity` and `AngularVelocity` from `box.movement` mark entities dynamic.

## Tests

```sh
bake3 run test/core --local-env -- -j 12                     # all unit tests
bake3 run test/core --local-env -- Args -j 12                # one suite
bake3 run test/core --local-env -- Args.fps                  # one case
bake3 run test/core --local-env -- -j 12 --json report.json  # with JSON report
bake3 run test/transform --local-env -- -j 12                # other projects: geometry, render, view, command
```

Integration scenes live under `test/integration`: `visual/` scenes are rendered
headless to screenshots, `bench/` scenes are run by the benchmark app and
`command/` scenes run a command file on a terminal session:

```sh
python3 tools/integration.py visual            # writes test/integration/out/<scene>.png
python3 tools/integration.py bench             # writes test/integration/out/<scene>.json
python3 tools/integration.py command           # writes test/integration/out/<scene>.log
bake3 run box_bench --local-env --cfg release -- ../../test/integration/bench/static_100k.flecs --churn 1-1000
```

A command scene holds a terminal named `terminals.agent0` with a `CommandScript`
that points at the `<scene>.cmd` next to it. The runner sets `Terminal.log` from a
generated harness scene so the session log survives the process folder, strips the
path of the repository from it and compares it with the `<scene>.expected` next to
the scene, printing a diff when they differ. A `<scene>_content.flecs` is loaded
before the scene, which keeps the scripts that commands patch out of the scene
that holds the terminal. `command/render.flecs` runs `render` on an entity, a
prefab and a template; the runner copies the PNGs of a session to
`test/integration/out/<scene>_<image>.png` while the scene runs, because the
process removes its folder when it exits.

## Benchmarks

`bench/<module>` holds one bake3 bench project per engine module, mirroring the
test projects. Cases always run sequentially in one process, so `-j` has no
effect on them.

```sh
bake3 run bench/transform --local-env -- -j 1 --json report.json  # one project
bake3 run bench/render --local-env --cfg release -- Extract.dynamic_10k
bake3 run bench/geometry --local-env -- --time 2 --samples 50     # longer budget
python3 tools/bench.py --env bench                                # every project
```

`tools/bench.py` builds and runs every project under `bench/` in release and
writes `<out>/<project>.json` (`bench/out` by default). With `--baseline <dir>`
it compares each report against the same file in that directory and prints the
cases that changed by more than `--threshold` (5% by default):

```sh
python3 tools/bench.py --env bench --out bench/out \
    --baseline project/db/scene_core/deliverable
```

A benchcase is a `void <Suite>_<case>(bench_t *b)` function in
`bench/<module>/src/<Suite>.c`, listed in the `bench` section of the project's
`project.json`; bake generates the harness and stubs for cases that are missing.
Build the world before the loop, measure inside it, exclude per iteration setup
that cannot be hoisted with `bench_pause`/`bench_resume`, keep results with
`bench_keep` and declare how many items an iteration processes with
`bench_set_items`:

```c
void Extract_static_100_changed(bench_t *b)
{
    // epic/task
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    /* build the scene and the packet */
    bench_set_items(b, 100);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        /* touch 100 entities */
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    ecs_fini(world);
}
```

A report holds median, mean, standard deviation, a 95% confidence interval and
the raw samples per case. Reports attached under `project/db` show up in the
**Microbenchmarks** table of the Benchmarks tab of the status page.

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

The **Plan** tab draws the epics as a node diagram with their dependencies and a
chain of order arrows from `project/db/plan.json`; dragging an arrow head onto an
epic replans it after that epic with `POST /api/plan` (started epics stay first).

Besides the board, timeline and plan, the page has a **Tests** tab (per test
project a table of suites with the time of the latest report and a chart of time per suite
over every report, expanding into cases with their own history) and a
**Benchmarks** tab (the same breakdown for bake3 benchmark suites, expanding into
the median, confidence interval, p99 and samples per case, plus frame timing per
bench scene). Both read the aggregated `/api/tests` and `/api/benchmarks`
endpoints served by `pm.py serve`.

The server also serves `apps/box_inspector/index.html` at `/inspector`. An epic
with a `deliverable/wasm/` build gets an **Inspect** link on its card and in its
detail panel that opens the inspector on that image; the header link **Inspect
local instance** opens it on `http://localhost:27750`.

```sh
python3 project/pm.py new task <epic>/<task> "Title" --brief "One sentence." --deps <epic>/<other>
python3 project/pm.py start <epic>/<task>
python3 project/pm.py done <epic>/<task>
python3 project/pm.py check            # validates READMEs, statuses, dependencies and plan.json
```

## Layout

```
apps/box_app/   the engine executable, its scenes under etc/ and the wasm shell page
bench/          bake3 bench projects, one per engine module
design/         design brief and datamodel sketches (documentation only)
docs/           architecture notes
include/box/    public headers, one per module
project/        project management tool and its database
src/            engine sources, one directory per flecs module (see docs/ARCHITECTURE.md)
test/           unit test projects
tools/          lint, integration, benchmark and texture generation scripts
```
