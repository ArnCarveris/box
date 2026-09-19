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
Headless runs step with a fixed delta of one over `--fps` (60 by default) so captures are
deterministic, require `--frames` or `--timeout`, write their temporary
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
on the right. `svg_text`, `svg_mask`, `svg_clip`, `svg_gradient` and `svg_filter` are
the resvg feature checks, one per SVG in `apps/box_app/etc/svg/`, with text set in the
fallback font `apps/box_app/etc/fonts/B612-Regular.ttf` that the engine loads next to
the system fonts.

`test/integration/visual/tiling.flecs` is the tiling check: one checker texture
at one meter over boxes of one, two and six meters, a cylinder and a sphere, with a
second row that offsets the texture transform, mirrors and clamps the wrap mode and
tiles the same material at three sizes.

`test/integration/visual/blending.flecs` is the blend mode check: alpha blended,
additive, multiply, screen and subtractive panels overlapping each other and the
primitives behind them, drawn back to front. `milk_glass.flecs` holds a frosted
panel with opacity 0.5 and roughness 0.8 next to a clear one over the same
textured geometry, `glass_sweep.flecs` rows six panels of opacity 0.5 at
roughness 0 to 1 over checker boxes and spheres, and `back_faces.flecs` shows
three open boxes of quads, one per `BackFaceMode`, with the camera looking into
them.

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

`test/integration/visual/parent_tree.flecs` is the hierarchy check: a spinning
node whose children use the non fragmenting `Parent` storage (`@tree Parent` in
the script) carries two arms with spheres and a tumbling box, a tower mixes a
`Parent` child with a `ChildOf` subtree under it, and a `Tint` on the spinner
resolves into its children and grandchildren next to an untinted white box.

`test/integration/visual/culling.flecs` is the frustum culling check: a ring of
boxes and spheres around the camera with a row behind it, which `CullSettings`
on a view or as a singleton switches off for a bit identical comparison.

`test/integration/visual/occlusion.flecs` is the Hi-Z occlusion check: a wall in
front of 2400 boxes whose capture is bit identical to `occlusion_off.flecs`,
next to `occlusion_sweep.flecs`, which `python3 tools/occlusion_sweep.py` sweeps
past walls at 60 units per second and compares frame by frame with occlusion on
and off; `CullSettings.occlusion` switches the test off and `hiz_levels` caps the
depth pyramid.

Entities are static unless they or an ancestor have `DynamicTransform` or
`DynamicMaterial`; static entities are only re-uploaded when a component changes.
`Velocity` and `AngularVelocity` from `box.movement` mark entities dynamic.

`test/integration/visual/tint.flecs` checks tint on single objects and on ChildOf, Parent and mixed hierarchies with tints set at different levels.

The `Input` singleton of `box.input` holds the key, mouse and text state of the frame,
filled in OnLoad from the OS events the platform layer queues and from injected events,
which are indistinguishable from real input; key codes are ASCII where possible and are
exported as script constants under `keys` (`keys.W`, `keys.MouseLeft`).

`apps/box_app/etc/scenes/input_map.flecs` is the action layer map: actions are entities
under the `actions` scope with an `InputAction`, an entity with `InputMap` binds them with
`(keys.W, actions.Forward)` pairs or with `Binding` children that carry a full key mask
like `keys.Ctrl|keys.S`, and a UI button or a command fires the same action without a key.
A script task awaits an action with `await on.pressed(actions.Forward)` inside an `async`
block, next to `on.down` and `on.up`; the await returns the source that fired the action.
A terminal drives both layers with the `key.*`, `mouse.*`, `input.*` and `action.*` commands,
which queue injected events over frames; `test/integration/command/input.flecs` types, clicks
and drags through them.

`CameraController` from `box.controller` flies a camera with the `Forward`, `Back`, `Left`,
`Right`, `Up`, `Down`, `Fast` and `Look` actions the module binds to WASD, QE, shift and the
right mouse button under `box.input.map`; it writes `Position`, `Orientation` and `LookAt`,
turns with the cursor while the surface captures it, and keeps `clearance` above the bounds of
its `terrain` entity. `FirstPersonController` walks the same actions in the XZ plane with a walk
and a run speed, falls with gravity, jumps with `Jump`, lowers its eye with `Crouch` and toggles
free flight with `Fly`; `test/integration/visual/first_person.flecs` stands it in a small level.

`JobSettings` sizes the worker pool that runs large tasks off the main thread and
caps the milliseconds their completions may take in PreUpdate, which is how SVG
rasterization and light baking stay off the frame; `JobStats` reports the queue.
Setting an `Svg` queues one job per horizontal band of the image, nearest to the
camera first, and the texture keeps its previous image until the last band lands;
`apps/box_app/etc/scenes/svg_storm.flecs` rasterizes a wall of them at once.

`apps/box_app/etc/scenes/templates.flecs` is the incremental loading demo: a ground
that appears at once and 4096 instances of a `Pillar` template that `box.script`
expands over frames, nearest to the camera first, inside the `ScriptSettings`
budget (`templates_per_frame`, `budget_ms`, `nearest_first`, `enabled`) while
`ScriptStats` reports the queue and the worst frame.

`CpuMonitor`, `EcsStats` and `SystemMonitor` from `box.monitor` publish process
and machine CPU usage, the flecs statistics addon and the per system frame
breakdown as components; a source is only sampled while its singleton exists,
and the CPU budget of a headless run shares the CpuMonitor sampling.

## Tests

```sh
bake3 run test/core --local-env -- -j 12                     # all unit tests
bake3 run test/core --local-env -- Args -j 12                # one suite
bake3 run test/core --local-env -- Args.fps                  # one case
bake3 run test/core --local-env -- -j 12 --json report.json  # with JSON report
bake3 run test/transform --local-env -- -j 12                # other projects: geometry, render, view, shader, command, job, input, controller, script
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

Integration harnesses disable incremental template loading (`ScriptSettings: {enabled: false}`)
so captures and command logs are deterministic; `templates_per_frame` with `budget_ms: 0` gives a
deterministic incremental run when that is what a test needs. The visual harness also sets
`JobSettings: {workers: -1, budget_ms: 0}`, which runs every queued job on the main thread before
the captured frame, so a scene with SVG textures renders the same every run.

## Shaders

Every shader of the engine is a WGSL file under `apps/box_app/etc/shaders/`, loaded
by a `Shader` component that holds a path and a comma separated `NAME=value` macro
list. Setting it preprocesses the file and hands the source to the renderer, which
compiles a module and rebuilds the pipelines that use it; a file that does not
preprocess leaves the previous shader drawing and reports the file and line through
`ShaderStatus`. The builtin shaders are entities under `box.shader.builtin`, so
reloading the mesh shader is a component write:

```
box { shader { builtin { mesh {
  Shader: {path: "etc/shaders/mesh.wgsl", macros: "DEBUG_NORMALS=1"}
} } } }
```

A path is resolved against the shader search path: the working directory, then
`apps/box_app` under the working directory or the nearest ancestor of it that has
one, then the directory of the executable. That makes a checkout work from the
repository root and from a test project, an installed game work from any working
directory, and the wasm build work from the embedded `etc`.

The preprocessor understands `#include "file"` relative to the including file with
cycle detection, `#define NAME value` with identifier substitution, `#if NAME`,
`#ifdef`, `#ifndef`, `#else`, `#endif` and `#pragma once`; an undefined macro is 0
in a condition and a `#define` never replaces a macro the component or the engine
set. The constants the renderer and the shaders share, such as `SHADOW_CASCADE_MAX`
and `ENV_MIPS`, are predefined. `#include "generated/layers.wgsl"` is answered by
the material layer registry instead of from a file, which is how the mesh shader is
recomposed when a scene registers or removes a layer.

`test/integration/visual/shader_macro.flecs` is the macro check: the same scene with
`DEBUG_NORMALS=1` on the builtin mesh shader, which outputs world normals as color.

## Post process effects

An effect is an entity with an `Effect` component that names a `Shader`, an order, a
resolution `scale` and the inputs its pass reads, plus a parameter component the
engine copies into the uniform block the shader sees as `fx.params`:

```
using box.effects

grade {
  Effect: {shader: box.effects.shaders.color_grade, order: 100, enabled: true, color: true}
  ColorGrade: {exposure: 0.7, saturation: 1.8}
}
```

A view runs the effects its `effect_list` names, or every enabled effect of the world
in `order` when the list is empty, and `View.effects: false` turns the chain off.
Enabling or disabling an effect, changing its order or changing the list of a view
reconfigures the chain on the next frame, because the chain is rebuilt during extract.

The renderer runs the chain after the transparent pass and before the capture. A view
with a chain draws its mesh, transparent and glass passes into a pooled RGBA16F target
instead of its own 8 bit one, so highlights past white reach the chain; the gamma encode
moves to the last pass, which writes the image of the view. The passes ping pong
between pooled targets of the size their scale asks for; targets are handed back after the chain, so the passes of one view
and the views of a frame share the same textures. The declared inputs decide what the
frame produces: the depth target is bound as it is, the world normals come from a
second color attachment the mesh pass writes through its `fs_normal` stage, and the
motion vectors come from a pass that reprojects the depth with the view projection of
the previous frame into an RG16F target. An input no enabled pass asks for costs
nothing and binds a one texel dummy. `include/effect.wgsl` gives a pass its bindings,
`fx_sample`, `fx_view_depth`, `fx_world_normal` and `fx_motion_vector`.

`test/integration/visual/effect_toggle.flecs` is the effect check: the same scene with
the color grade enabled, which is visibly brighter and more saturated.

The engine registers its own effects as entities under `box.effects.stack`, all
disabled, so a scene turns one on and gives it parameters:

```
box { effects { stack { bloom {
  Effect: {enabled: true}
  Bloom: {intensity: 0.22, radius: 1, knee: 0, firefly_clamp: 8}
} } } }
```

`gtao` (order 600) traces horizon based ambient occlusion over the depth at half
resolution and darkens the indirect diffuse light of a pixel with it, `ssr` (650)
marches the depth for a reflection and replaces the indirect specular light of a
pixel with what it found, falling back to the prefiltered environment on a miss,
`fog` (700)
integrates a height fog along the view ray it rebuilds from the
depth, `bloom` (800) mixes in a pyramid of the image, `auto_exposure` (850) scales
it by the exposure its histogram adapted to and `tonemap` (900) maps the result
onto the display and dithers it. Two of them ask the frame for work through their
own inputs: `pyramid` builds the mip chain of the image the pass reads,
`exposure` runs the histogram and adaptation compute passes over it and
`occlusion` traces, denoises and accumulates the ambient occlusion of the view.
The second attachment of the mesh pass carries the world normal octahedrally,
the indirect diffuse fraction of a pixel and its indirect specular fraction
packed with the roughness, which is how a pass changes the indirect light of a
pixel and leaves the direct light alone. Targets
between two passes are RGBA16F, so what an earlier pass pushes past white reaches
the tone map. `height_fog.flecs`, `bloom.flecs`, `auto_exposure.flecs` and
`tonemap.flecs` under `test/integration/visual` show one effect each, and
`gtao.flecs` shows a room corner with the occlusion on next to `gtao_off.flecs`
and `ssr.flecs` a glossy floor reflecting colored boxes next to `ssr_off.flecs`;
`python3 tools/ssr_check.py` renders that scene and fails when the floor keeps
more than a handful of isolated pixels where a ray missed.

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
tasks and epics keep test reports, screenshots and notes in `deliverable/`. An
epic also has a `priority` in its `progress.json`, an integer that is 100 when
missing and where lower runs first.

The tool derives the execution order from that: a task is ready when every
dependency is complete (an epic as a dependency counts as complete when that
epic is), and among the ready tasks the one from the epic with the lowest
priority goes first, then the oldest one. That order sorts unstarted epics and
tasks everywhere the page lists them and is served as `next_up` in `/api/state`.

The **Plan** tab draws one node per task, colored by status and grouped into a
horizontal band per epic in priority order, with the columns left to right by
dependency depth and the dependency edges between tasks, stronger when they
cross epics. Hovering a task lights up what it waits for and what waits for it;
the filter bar shows one epic or only unfinished tasks, and the diagram pans and
zooms. The priority box in an epic's detail panel writes the new value with
`POST /api/priority` and reorders the page.

Besides the board, timeline and plan, the **Tests** and **Benchmarks** tabs open
with a summary panel of totals and sparklines and then one sortable, searchable
project to suite to case tree, where an open row carries its own chart: time per
suite and per case for tests, median per suite and per case with its confidence
interval for bake3 benchmarks, plus a scene section with frame timing. Both read
the aggregated `/api/tests` and `/api/benchmarks` endpoints served by `pm.py
serve`, which summarize every level of that tree.

The **Build** tab reads the `build*.json` reports of `tools/epic.py` from
`/api/builds` in the same layout: tiles per kind (debug, release, wasm), files, link
time and commit, then a report to project to step tree, slowest files first.

The server also serves `apps/box_inspector/index.html` at `/inspector`. An epic
with a `deliverable/wasm/` build gets an **Inspect** link on its card and in its
detail panel that opens the inspector on that image; the header link **Inspect
local instance** opens it on `http://localhost:27750`.

```sh
python3 project/pm.py new task <epic>/<task> "Title" --brief "One sentence." --deps <epic>/<other>
python3 project/pm.py new epic <epic> "Title" --priority 65   # lower runs first, 100 by default
python3 project/pm.py next 5           # the ready unstarted tasks in the derived order
python3 project/pm.py start <epic>/<task>
python3 project/pm.py brief <epic>/<task> --files src/sky/sky.c,include/box/sky.h
python3 project/pm.py deliver <epic>/<task> --tests render:Atmosphere,core --bench render
python3 project/pm.py done <epic>/<task>
python3 project/pm.py check            # READMEs, statuses, dependencies, priorities and dependency cycles
```

`brief` prints the whole agent prompt for a task: its README, the README of its
epic, the deliverable reports of its dependencies and the house rules, with the
private environment named after the task (`--env`), the files the agent owns
(`--files`) and a `--worktree` note when another agent is editing the tree.

`deliver` runs `bake3 run test/<project> -- [<suite>] -j 8 --json` once per
project into the task's `deliverable/`, merges the reports into `tests.json`,
copies the `tools/bench.py` report to `bench.json` and exits non zero when a test
fails. `done` then refuses a task without `tests.json` and `bench.json` (unless
its `progress.json` says `"bench": false`) and an epic without its report in the
four section format, the merged tests, the benchmarks, the integration outputs
and the wasm image, printing every missing item; `--force` overrides with a
warning. `docs/PROCESS.md` describes the loop these commands encode.

`python3 tools/epic.py <epic> --env <name>` produces everything `done <epic>`
wants except the README: it builds `box_app` in debug, release and wasm and keeps
the bake3 `--build-json` reports as `build_debug.json`, `build_native.json` and
`build_wasm.json`, runs every project under `test/` into `tests_<project>.json`
and the merged `tests.json`, runs the visual, bench and command scenes into
`integration/`, runs `tools/bench.py` into `bench/` against the `bench/` folder of
the previous epic (`--baseline <epic>` picks another) and copies the web image
into `wasm/`. It prints a line per step, exits non zero when a build, a test or a
scene fails and ends with what is still missing; `--steps a,b` and `--skip a,b`
rerun part of the pipeline.

## Layout

```
apps/box_app/   the engine executable, its scenes under etc/ and the wasm shell page
bench/          bake3 bench projects, one per engine module
design/         design brief and datamodel sketches (documentation only)
docs/           architecture notes and the coordination process
include/box/    public headers, one per module
project/        project management tool and its database
src/            engine sources, one directory per flecs module (see docs/ARCHITECTURE.md)
test/           unit test projects
tools/          lint, integration, benchmark and texture generation scripts
```
