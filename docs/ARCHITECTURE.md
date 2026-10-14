# Architecture

Box is a library of flecs modules plus the `boxrun` application. Everything an
application configures is a component value; the engine reacts to changes of those
values with observers and systems. `DESIGN.md` is the brief, this page describes
what exists.

## Module layout

`include/box.h` includes every public header and `BoxImport` (`src/box.c`) imports
the modules. Applications, tests and benchmarks import `Box` and nothing else, so
component ids are registered in the same order in every world.

| Module        | Header                  | Sources          | Contents |
|---------------|-------------------------|------------------|----------|
| `box.types`     | `include/box/types.h`     | `src/types`      | Reflected math types (`vec3`, `mat4`, `rgba`) |
| `box.transform` | `include/box/transform.h` | `src/transform`  | `Position`, `Rotation`, `Scale`, `WorldTransform`, static and dynamic propagation |
| `box.geometry`  | `include/box/geometry.h`  | `src/geometry`   | Scalable 3D and 2D shapes |
| `box.camera`    | `include/box/camera.h`    | `src/camera`     | `Camera` |
| `box.surface`   | `include/box/surface.h`   | `src/surface`    | `Surface`, a window or a texture that views render to |
| `box.view`      | `include/box/view.h`      | `src/view`       | `View`, `Scene` |
| `box.effects`   | `include/box/effects.h`   | `src/effects`    | Post process effect settings |
| `box.material`  | `include/box/material.h`  | `src/material`   | PBR material components, `Tint` |
| `box.texture`   | `include/box/texture.h`   | `src/texture`    | `Texture`, `TexturePath` (not yet imported by `BoxImport`) |
| `box.light`     | `include/box/light.h`     | `src/light`      | Directional, point and spot lights |
| `box.movement`  | `include/box/movement.h`  | `src/movement`   | `Velocity`, `AngularVelocity` |
| `box.layout`    | `include/box/layout.h`    | `src/layout`     | UI layout components |
| `box.app`       | `include/box/app.h`       | `src/app`        | `App`, `AppStatus`, argument parsing, the run loop |

A module keeps private declarations in a header next to its sources
(`src/transform/transform.h`, `src/app/app.h`); test projects that need them add the
folder to their include path. Every directory under `test/` is a test project,
`test/core` covers `box.app`.

## Running a world

`apps/boxrun` creates a world, imports `Box`, calls `box_app_parse` and then
`box_app_run`. The parser only writes the `App` singleton:

| Option                     | `App` member | Notes |
|----------------------------|--------------|-------|
| `--headless`               | `headless`   | No window, fixed delta of `1/fps` |
| `--frames <n>`             | `frames`     | Stop after n frames |
| `--timeout <s\|infinite>`  | `timeout`    | Stop after s wall seconds, `infinite` is -1 |
| `--cpu-budget <pct>`       | `cpu_budget` | Percentage of one core, default 100 |
| `--fps <n>`                | `fps`        | Target frame rate, default 60, 0 uncapped |
| `--port <n>`               | `port`       | REST and command port, default 27750, 0 disables |
| `<file>`                   | `scene`      | Script loaded before the first frame |

`box_app_run` refuses a headless run without `frames` or `timeout`, and a headless
run on a machine whose one minute load average exceeds its core count unless
`App.unguarded` is set. It then loads the scene as a managed script, starts the REST
server when `port` is set and steps the world until it quits. `BOX_TEST=1` in the
environment sets `unguarded` and `unpaced` in every new world, for test runs.

The `AppFrame` system (`EcsPostFrame`) counts frames into `AppStatus.frame`, calls
`ecs_quit` when `App.frames` or `App.timeout` is reached and, in a headless run that
is not `unpaced`, sleeps for as long as the process exceeded its CPU budget since the
previous frame (process CPU time over wall time, all threads included). Because these
are systems that read `App` every frame, a test can drive them with `ecs_progress`
and a running process picks up a new value set through the REST API.

## Threads

The main thread owns the world: it runs every flecs system and observer, and no other
thread reads or writes the world. Planned in the `engine_core` epic and not yet
present:

- A render thread that owns the GPU backend and consumes render packets the main
  thread fills during extract. At most N packets are in flight and their buffers are
  reused, so a steady state frame does not allocate.
- A job system whose workers run IO and asset processing without world access; a
  job's result is applied by a system on the main thread.

The REST server of flecs accepts connections on its own threads but runs requests on
the main thread during `ecs_progress`.

## From a component change to a reconfiguration

Public components are plain data with reflection, so a script, the REST API and C
code change them the same way. A module reacts to a change with:

1. An `OnSet` observer when the change must take effect immediately. Setting
   `App.headless` creates `/tmp/box/<pid>` and writes it to `AppStatus.tmp_dir`
   (removed again when the world is deleted); setting `App.fps` while the world runs
   changes the target frame rate.
2. A system that reads the component every frame when the value steers per frame
   work, such as the stop conditions and the CPU budget of `AppFrame`, or the
   transform systems that recompute `WorldTransform`.
3. `OnAdd` and `OnRemove` observers for components whose presence matters. Inside
   them the entity is mid table-move, so structural changes go through `it->stage`.

Writing through `ecs_get_mut` raises no `OnSet`; call `ecs_modified` when observers
must see the new value. State that the engine produces lives in separate components
(`AppStatus`, `WorldTransform`), never in the component a user writes.
