# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU. Scenes, assets and configuration are entities and components; the
engine reacts to component changes. See `DESIGN.md` for the full brief and
`docs/ARCHITECTURE.md` for the module layout.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3) on the path
- The flecs repository checked out at `../../flecs` (built as a bundle)
- A Rust toolchain (wgpu-native and resvg are built with cargo)
- For wasm builds: emsdk, with `emsdk_env.sh` sourced, and the Rust target for resvg:
  `rustup target add wasm32-unknown-emscripten`

## Build and run

```sh
bake3 --local-env                                         # build the library and the app
bake3 run apps/boxrun --local-env -- --headless --frames 10  # headless run, exits after 10 frames
bake3 run apps/boxrun --local-env -- etc/scenes/default.flecs  # open the default scene in a window
```

## Browser build

```sh
bake3 --target em --local-env apps/boxrun                 # boxrun.html, .js and .wasm
bake3 --target em --cfg release --local-env apps/boxrun   # optimized, about 1.7 MB of wasm
bake3 run apps/boxrun --target em --local-env             # serve the page on localhost:8080
```

The files land in `.bake/local_env/wasm32-Emscripten/<cfg>/bin/`. The build uses emsdk's
`emdawnwebgpu` port, so it renders with the browser's own WebGPU, and embeds `etc/` at `/etc`.
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
BOX_TEST=1 bake3 run test/transform --local-env -- -j 8          # all suites of a test project
BOX_TEST=1 bake3 run test/transform --local-env -- Update -j 8   # one suite
BOX_TEST=1 bake3 run test/transform --local-env -- Update.root   # one case
```

Every directory under `test/` is a test project.

## Style

`python3 tools/lint.py` checks `include`, `src`, `apps`, `test` and `bench`
against `design/coding_style.c`; `--fix` reformats what it can.

## Project management

Work is tracked with `project/pm.py`; `docs/PROCESS.md` describes the loop.

```sh
python3 project/pm.py serve --host :: --port 8090        # status page on all interfaces
python3 project/pm.py next 5                              # ready tasks
python3 project/pm.py check                               # validate the database
```
