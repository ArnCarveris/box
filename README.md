# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU. Scenes, assets and configuration are entities and components; the
engine reacts to component changes. See `DESIGN.md` for the full brief.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3) on the path
- The flecs repository checked out at `../../flecs`, built once with bake3 (`bake3 ../../flecs`)
- A Rust toolchain (wgpu-native and resvg are built with cargo)
- For wasm builds: emsdk, with `emsdk_env.sh` sourced, and the Rust target for resvg:
  `rustup target add wasm32-unknown-emscripten`

## Flecs version

Box uses the flecs project from the bake environment, which points at the checkout in
`../../flecs` after it has been built once with bake3. The `use` entry for flecs in
`project.json` lists the defines box needs (`FLECS_SCRIPT_MATH`, `FLECS_CREATE_MEMBER_ENTITIES`),
so bake builds a static flecs library with those defines into box's own build directory. That
library is rebuilt from the flecs sources like any other dependency, so box always builds against
the current state of `../../flecs`.

## Build and run

```sh
bake3 --local-env                                         # build the library and the app
bake3 run apps/boxrun --local-env -- --headless --frames 10  # headless run, exits after 10 frames
bake3 run apps/boxrun --local-env -- etc/scenes/default.flecs  # open the default scene in a window
bake3 run apps/boxrun --local-env -- --surface 1280x720 test/integration/visual/bake_bounces.flecs
                                                          # a window for a scene without a Surface
bake3 run apps/bunnyhopper --local-env                    # play the bunny hopper
bake3 run apps/oreelse --local-env                        # play Ore Else (apps/oreelse/README.md)
bake3 run apps/yeon --local-env                           # play Yeon (apps/yeon/README.md)
```

Box opens files relative to the working directory or, failing that, one of its ancestors, so a
run from the repository root or from an app's folder finds the engine's assets as `etc/...`. An
app keeps its own assets in `apps/<app>/etc` and names them from the repository root, e.g.
`apps/bunnyhopper/etc/bunnyhopper.flecs`.

Box watches the files it loaded, shaders with their includes, textures, fonts and environment
images, and loads one again when it changes on disk; the previous content stays in use until the
new load is ready. The `FileWatch` singleton of box.app sets how often the files are checked:
every 0.25 s by default, `{interval: 0}` every frame and `{interval: -1}` never.

Parameterized surfaces and surface-relative placement are described in
[Geometry surfaces](docs/geometry-surfaces.md).

Relational 3D modeling with Fit, Align, Array, and Distribute is described in
[Spatial layout](docs/spatial-layout.md).

Use `--design` to inspect a scene with top, bottom, left, right, front and back orthographic
cameras and two perspective cameras:

```sh
bake3 run apps/boxrun --local-env -- --design etc/scenes/default.flecs
bake3 run apps/boxrun --local-env -- --headless --frames 3 --design --out model etc/scenes/default.flecs
bake3 run apps/boxrun --local-env -- --camera camera etc/scenes/default.flecs
```

Design mode uses the scene of the first view on the main surface, or the first shape's
scene when no view exists. It fits the cameras and shadow distances to that scene's
static and dynamic shape bounds each frame, with a small margin. Top and bottom occupy
the first row on the left, left and right the second row, and front and back the third
row. The right half stacks perspective above perspective (back), which orbits the camera
180 degrees around the scene at the same elevation. Every panel has a black border and
a tiny label in its upper-left corner. Orthographic labels include the model's dimensions
in world units along the two visible axes, updated each frame from the scene bounds.
Each view has a white directional light angled from its camera and a dim neutral
ambient fill. Post effects, environment lighting, atmosphere, baked
lighting and reflections are disabled for the design views.

`--out <path>` saves the main surface to a PNG, adding `.png` when no extension is
given. Relative paths are resolved from the working directory, including headless runs.
It overrides the scene's main `Surface.write_to_file` and works with `--camera` too.

With `--design --out model` (or when the main `Surface.write_to_file` is `model.png`),
the overview is saved as `model.png` and
the individual views are saved as `model-top.png`, `model-bottom.png`, `model-left.png`,
`model-right.png`, `model-front.png`, `model-back.png`, `model-perspective.png` and
`model-perspective-back.png`. Each individual image has half the main
surface's width; its height matches the tile's aspect ratio after reserving the label
header. Relative paths set directly through `Surface.write_to_file` use the usual
headless temporary directory.

`--camera <entity.path>` renders an existing camera, retaining its view settings when
available, or the main view's settings otherwise. The options are mutually exclusive;
the equivalent App fields are `design: true` and `camera: "entity.path"`, applied by
`box_app_run` after loading the scene. Both options create a surface if needed, using
`--surface` dimensions when supplied. `View.lighting.scene` can select a separate scene
for direct and ambient lighting; design mode uses it to light each camera independently.

Views render in ascending `View.order` (default 0) and are picked and receive input in
descending order, so a view with a higher order draws over and is hit before the views
below it. Views with the same order keep their creation (entity id) order, and a changed
order applies from the next frame. box's own overlays use orders above those of a game:
`BOX_VIEW_ORDER_EDITOR` (1000000) for the editor and `BOX_VIEW_ORDER_CONSOLE` (2000000)
for the in game console, which stays on top of every other view.

## Browser build

```sh
bake3 --target em --local-env apps/boxrun                 # boxrun.html, .js and .wasm
bake3 --target em --cfg release --local-env apps/boxrun   # optimized, about 7.2 MB of wasm
bake3 run apps/boxrun --target em --local-env             # serve the page on localhost:8080
```

The files land in `.bake/local_env/wasm32-Emscripten/<cfg>/bin/`. The build uses emsdk's
`emdawnwebgpu` port, so it renders with the browser's own WebGPU, and embeds `etc/` at `/etc`
(an app also embeds its `apps/<app>/etc` at `/apps/<app>/etc`).
Command line arguments go after `#` in the page URL, e.g. `boxrun.html#etc/scenes/x.flecs`.
In the browser the main thread renders each packet at the end of its frame, jobs run inline,
no command port is served and `Surface.write_to_file` captures are skipped.

The browser enforces the WGSL uniformity rules that native wgpu skips. Check every engine
shader with Tint (the validator is installed once into `~/.cache/box/wgsl`):

```sh
node tools/wgsl_check.mjs --install
node tools/wgsl_check.mjs                                 # etc/shaders, or pass files and dirs
```

## Tests

```sh
BOX_TEST=1 bake3 run test/cpu/transform --local-env -- -j 8          # all suites of a test project
BOX_TEST=1 bake3 run test/cpu/transform --local-env -- Update -j 8   # one suite
BOX_TEST=1 bake3 run test/cpu/transform --local-env -- Update.root   # one case
```

Every directory under `test/cpu` and `test/gpu` is a test project. Suites in `test/cpu` never
render with the GPU; a suite with any test case that does (a `Surface`, a capture, the `capture` or
`render` command, or the GPU backend's API) is in `test/gpu`. GPU projects have a `_gpu` id suffix.
