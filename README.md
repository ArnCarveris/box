# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU. Scenes, assets and configuration are entities and components; the
engine reacts to component changes. See `DESIGN.md` for the full brief and
`docs/ARCHITECTURE.md` for the module layout.

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
```

Box opens files relative to the working directory or, failing that, one of its ancestors, so a
run from the repository root or from an app's folder finds the engine's assets as `etc/...`. An
app keeps its own assets in `apps/<app>/etc` and names them from the repository root, e.g.
`apps/bunnyhopper/etc/bunnyhopper.flecs`.

## Browser build

```sh
bake3 --target em --local-env apps/boxrun                 # boxrun.html, .js and .wasm
bake3 --target em --cfg release --local-env apps/boxrun   # optimized, about 1.7 MB of wasm
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
