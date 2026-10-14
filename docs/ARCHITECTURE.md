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
| `box.camera`    | `include/box/camera.h`    | `src/camera`     | `Camera`, reversed Z projections |
| `box.surface`   | `include/box/surface.h`   | `src/surface`    | `Surface`, `SurfaceStatus`, glfw windows, render targets |
| `box.view`      | `include/box/view.h`      | `src/view`       | `View`, `Scene` |
| `box.effects`   | `include/box/effects.h`   | `src/effects`    | Post process effect settings |
| `box.material`  | `include/box/material.h`  | `src/material`   | PBR material components, `Tint` |
| `box.texture`   | `include/box/texture.h`   | `src/texture`    | `Texture`, `TexturePath` (not yet imported by `BoxImport`) |
| `box.light`     | `include/box/light.h`     | `src/light`      | Directional, point and spot lights |
| `box.movement`  | `include/box/movement.h`  | `src/movement`   | `Velocity`, `AngularVelocity` |
| `box.layout`    | `include/box/layout.h`    | `src/layout`     | UI layout components |
| `box.input`     | `include/box/input.h`     | `src/input`      | `Input`, `Key`, synthetic input queue, `Action`, `ActionState`, `Binding` |
| `box.controllers` | `include/box/controllers.h` | `src/controllers` | `CameraController`, `FirstPersonController` and their default actions |
| `box.app`       | `include/box/app.h`       | `src/app`        | `App`, `AppStatus`, argument parsing, the run loop |
| `box.jobs`      | `src/jobs/jobs.h` (private) | `src/jobs`     | Worker pool for IO and asset work |
| `box.render`    | `include/box/render.h`    | `src/render`     | `RenderSettings`, `RenderStatus`, packet ring, render thread, backends, views, forward pass |
| `box.shader`    | `include/box/shader.h`    | `src/render`     | `Shader`, `ShaderStatus`, `ShaderSettings`, WGSL preprocessor, engine shaders in `etc/shaders` |
| `box.perf`      | `include/box/perf.h`      | `src/perf`       | `PerfRun`, `PerfStatus`, paced frames, orbit camera fitted to the shape bounds, frame time checks |

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
thread reads or writes the world. Two kinds of threads work next to it:

- The render thread (`box.render`) is the only thread that calls webgpu. It consumes
  render packets the main thread fills, see below. With `RenderSettings.main_thread`
  (the default in the browser) there is no render thread and the main thread renders
  the packet at the end of the frame instead.
- The job workers (`box.jobs`) run IO and asset processing without world access.

The REST server of flecs accepts connections on its own threads but runs requests on
the main thread during `ecs_progress`.

## Job system

`src/jobs/jobs.h` is private to box modules; there is no public job component.
`box_job_submit` queues a `box_job_desc_t`: `work` runs on a worker and must not touch
the world, `complete` runs on the main thread in the `JobsComplete` system (`EcsOnLoad`)
in submission order, and `ctx_free` releases the context afterwards. The first job
starts the pool, sized from the core count and `App.cpu_budget` (`BOX_JOBS_AUTO`, two
cores kept for the main and render thread, at most 16 workers). With zero workers jobs
run inline in `JobsInline` (`EcsPostFrame`). Deleting the world drops queued work,
waits for running work and frees every context without running completions.

## Render module

`box.render` moves a frame from the world to the renderer through a ring of render
packets (`src/render/render.h`, private). `RenderSettings.in_flight` (default 2, at
most 8) packets exist; their vectors keep their storage, so a steady frame does not
allocate. A packet holds, in apply order: resource changes (create, update or destroy
of a buffer, mesh, texture, target or shader by handle, with a description in the
packet data), uploads into buffers, the targets to render and the views with their
batches. Upload and description bytes live in one 16 byte aligned data block.

Two phases run after `EcsOnStore`: in `RenderExtract` modules call
`box_render_packet` to append to the packet of the frame (the first call acquires a
free packet and waits when all are in flight, counted in `RenderStatus.stalls`), and
`RenderSubmit` hands it to the consumer. `RenderExtractViews` adds every `View` whose
camera has a `Camera`, in creation order (entity id), with the parent of the view, the
camera transform, its inverse and the scene root of the camera (the nearest ancestor with
`Scene`, 0 for the default scene). `RenderExtractDraws` sends the instanced batches of the
shapes, see below. `box_render_sync` waits until every submitted packet rendered; deleting
the world does the same before the fini actions registered after `box.render`, such as the
removal of `AppStatus.tmp_dir`, run, so the captures of the last frames land.

A backend (`box_render_backend_t`) is four callbacks: `init`, `render` and `fini` run on
the consuming thread only, `free` on the main thread. A new ring has the null backend,
which renders nothing and records what it received; tests set their own with
`box_render_set_backend`. The first `Surface` replaces a ring that still has its
initial backend with the webgpu backend (`box_render_use_gpu`), so a world without
surfaces never creates a device and a test that set a backend keeps it.

## Surfaces and the GPU device

`box.surface` runs on the main thread. Setting a `Surface` makes the world render with
the GPU; in `RenderExtract` the `SurfaceExtract` system gives a new surface a target
handle and, unless the run is headless, the surface has a `texture` or `BOX_TEST` is
set, a glfw window with a `CAMetalLayer` (`src/surface/window.c`; glfw only runs on
the main thread, `SurfacePoll` in `EcsOnLoad` polls events and quits when a window
closes). Every frame the surface is measured: width and height in points (default
1280x720; a window reports its framebuffer in pixels, offscreen a point is a pixel)
and the render size, the pixel size divided by `resolution_scale`. A changed
description goes into the packet as a `BOX_RENDER_TARGET` create or update and into
`SurfaceStatus`; removing the surface sends a destroy and closes its window once that
packet rendered. Each frame adds a target entry with the clear color of the first
clearing view (under `view_root` when set, opaque black otherwise) and, when
`write_to_file` is set, the PNG path; a relative path lands in `AppStatus.tmp_dir`.

The webgpu backend (`src/render/gpu.c`, `target.c`, `capture.c`, compiled with
`BOX_RENDER_WGPU`) creates the instance, adapter and device in its `init` on the
render thread and keeps targets by handle. A window target configures a webgpu
surface on the layer (vsync picks Fifo, otherwise Immediate when offered), an
offscreen target owns an `RGBA8UnormSrgb` texture. When the render size differs from
the image, views render into a scene texture that a blit scales up to the image. Per
frame the backend clears, blits, presents and, for a capture, copies the image into a
readback buffer, waits for it and writes the PNG next to the target path before
renaming it over. `box_render_gpu_stats` returns the counters of the backend for tests.
Colors are display encoded `rgba`; the targets are sRGB, so a clear color comes back
unchanged in a capture.

A one-off capture (`box_render_capture`, `src/render/captures.c`) renders a camera entity, or
explicit camera settings, transform and scene, into a PNG without a surface. The request is
queued on the main thread; `RenderExtractCaptures` turns it into an offscreen target created
by the packet of the frame, a view assigned to that target (`box_render_view_t.target`) and a
target entry with the path, and destroys the target in the next packet. A target marked
`exclusive` renders only its assigned views and other targets skip assigned views, so a
capture never shows up on a surface. The PNG is written on the render thread when that
packet renders; `box_render_sync` waits for it.

The `capture` and `render` commands of `box.command` (`src/command/cmd_render.c`) use it and
reply at once with the image name in the session `resources` folder and a `(pending)` marker,
because a reply cannot wait for a frame. `capture` renders the camera of the first view of the
first surface, or each named camera, at the surface size. `render` puts the subject in a
private `Scene` under the session: a prefab is instantiated, a template added, an entity
copied with its reflected components and children. In `EcsOnStore` of the same frame the
camera is fitted to the world bounds of the subject meshes, and the scene is deleted the
next frame.

## Cameras, views and the forward pass

`box_camera_projection` builds the projection of a `Camera` for a surface size: reversed Z,
depth 1 at the near plane and 0 at the far plane. A perspective camera without `far_` has
an infinite far plane; an orthographic camera shows `ortho_height` units, or the surface
height in points divided by `ortho_scale`. The aspect ratio follows the surface unless
`aspect_ratio` is set. The renderer builds the projection per target, so one view drawn
into surfaces of different sizes keeps its proportions.

`RenderExtractDraws` (`src/render/draw.c`) runs only when the packet has views. A mesh of
the mesh cache (`src/geometry/mesh_cache.h`) is sent once per backend as a `BOX_RENDER_MESH`
create with its vertices and indices; the renderer keeps a cache reference to it, so its
handle is not reused while the backend has it, and destroys it once only the renderer
references it. Shapes live in persistent batches (`src/render/draw.h`, `batch.c`) keyed by
scene root, mesh, pipeline and whether they are dynamic; a private `DrawSlot` holds the batch
and slot of an entity, and a removed slot is filled with the last one so the instances stay
contiguous. An instance is a `box_render_instance_t` (model matrix with the instance scale,
inverse transpose normal matrix, linear `Rgb` color). Observers on `ShapeMesh`, `Rgb`,
`WorldTransform`, `Position`/`Rotation`/`Scale`, `ChildOf`/`Parent`, `Scene` and the dynamic
tags queue changed entities or subtree roots, so an extract only visits what changed. A
static batch owns a vertex and storage instance buffer: a new or grown buffer gets one full
upload, later changes go into a shared change buffer that the engine compute shader
`etc/shaders/scatter.wgsl` scatters into their slots, and an unchanged frame uploads nothing.
`DynamicTransform` and `DynamicMaterial` (self or up) shapes go into dynamic batches that are
filled from cached queries and uploaded every frame. A batch draws all its instances with one
instanced draw call; a scatter binds the whole instance buffer, which limits a batch to 1M
instances.

The webgpu backend (`src/render/gpu_draw.c`) renders, after clearing a target, every view
of the target in packet order into its color texture and a `Depth32Float` texture at the
render size. A view that clears replaces the color with its clear color, any other view
draws over what is there; depth is cleared to 0 for every view and tested with greater. A
view draws the batches of its own scene with the engine shader `etc/shaders/mesh.wgsl` (a
sun and sky ambient), which is sent once a batch uses it. A target renders the views whose
parent is its `view_root`, or every view when that is 0; `box.surface` does not set
`box_render_target_t.view_root` yet, so for now every surface renders every view.

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
