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

## Using the framework

An application creates a world and imports the `Box` module and nothing else:

```c
ecs_world_t *world = ecs_init();
ECS_IMPORT(world, Box);
```
