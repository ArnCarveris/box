# Box architecture

Box is a data-driven framework: every feature is a set of flecs components plus
hooks, observers and systems that make the component values true. There are no
public native functions besides bootstrap. Scripts, the REST API and the command
terminal all work by changing component values.

## Layers

```
apps/            executables (box_app), thin: init world, import modules, run
include/box/     one public header per module, reflected types via ECS_STRUCT
src/<module>/    one directory per flecs module, C11, may include flecs.h
src/renderer/    render thread, wgpu only, MUST NOT include flecs.h
src/platform/    window, surface and OS events (GLFW native, HTML5 on wasm)
etc/             engine assets (WGSL shaders), installed as <BAKE_HOME>/etc/box
apps/box_app/etc app assets (scenes, textures, svg, fonts), both embedded on wasm
test/            bake test projects, one suite per module
```

Two hard rules keep the concerns apart:

- Nothing under `src/renderer` includes `flecs.h`. It only sees frame packets.
- Nothing outside `src/renderer` and `src/platform` includes `webgpu.h`.

## Modules

| Module | Owns |
|---|---|
| `box.types` | `vec2/3/4`, `mat4`, `rgb` and math helpers |
| `box.core` | `App` singleton, command line, process folder, frame loop, CPU budget |
| `box.surface` | `Surface` component; setting it brings up a window or offscreen target |
| `box.transform` | `Position/Rotation/Scale/WorldTransform`, static vs dynamic propagation, AABB |
| `box.geometry` | primitive shape components, mesh cache, shape registry |
| `box.material` | `Rgb`, `PbrMaterial`, `Emissive`, `Tint`, textures, material layers |
| `box.camera`, `box.light`, `box.view` | cameras, lights, views and layers |
| `box.render` | Extract system, frame packets, handoff queue to the renderer thread |
| `box.shader` | `Shader` component, WGSL preprocessor, builtin shaders under `box.shader.builtin` |
| `box.effects` | `Effect` entities, the per view post process chain and its parameters |
| `box.input` | device layer (`Input` singleton, event queues, `keys` codes) and action layer (`InputAction`, `InputMap`, `Binding`, key masks, emitted actions, `on.pressed` script events) |
| `box.controller` | `CameraController`, `FirstPersonController`, their default actions and the systems that move them |
| `box.command` | terminal sessions, HTTP endpoints, script functions for commands |
| `box.checkpoint` | script state capture and restore |
| `box.monitor` | `CpuMonitor`, `EcsStats` and `SystemMonitor` singletons, sampled only while present |
| `box.script` | scene loading, incremental template instantiation |
| `box.collision` | root bounds, per shape colliders, a bounding volume hierarchy with ray, sphere, box and frustum queries |
| `box.picking` | cursor rays through the views, hit resolution to the root or a `Pickable`, bubbling `on.click` script events |
| `box.job` | worker pool, prioritised jobs, completion on the main thread in a budget |
| `box.spline` | `Spline` with its cached arc length table, `FollowPath`, `SplineMesh` sweeps |
| `box.ui` | `Ui` roots a UI tree, its layer and the orthographic view that draws it in logical pixels |
| later | `box.particles`, `box.transition`, `box.lod`, `box.atmosphere` |

Each module has `include/box/<module>.h` with its components and a single
`Box<Module>Import` function, and `src/<module>/` with one file per concern
(`<module>.c` registers the module, other files hold systems and hooks).

## Frame

```
OnLoad      platform poll -> Input singleton; injected events
PreUpdate   script async tasks, incremental scene loading
OnUpdate    game systems, controllers
PostUpdate  dynamic transforms (static ones already updated by observers)
PreStore    Extract: build the frame packet from render-relevant components
OnStore     push packet to the renderer queue; block when 2 packets are in flight
```

The renderer thread pops packets, applies deltas to its own persistent state,
records and submits GPU work, presents, and returns the packet to the pool. On
wasm the renderer runs on the main thread and drains the queue after OnStore.

## Frame packets

A packet is not a copy of the scene. It carries:

- per-frame data: cameras, views, lights, time
- full arrays for dynamic instances (entities with `DynamicTransform` or
  `DynamicMaterial`)
- deltas for static instances: index and new data for every static entity whose
  components changed since the last packet, collected by observers
- resource commands: create/destroy mesh, texture, material, in creation order

Packets come from a pool of three and reuse their vectors, so steady state does
not allocate. Render handles (`uint32_t`) identify meshes, materials and
instances on both sides; the game side allocates them from free lists.

## Static and dynamic content

Entities without `DynamicTransform` are static: their world transform is computed
by an observer when a local component changes and uploaded once as a delta.
Entities with `DynamicTransform` are re-transformed and re-uploaded every frame
in a separate instance buffer. Materials follow the same split with
`DynamicMaterial`. Shadows and culling structures are built on the static buffer
and only patched for changed entries.

## Command interface

Agents drive a running instance through terminal sessions. A session is an entity
with a `Terminal` component that owns a folder `/tmp/box/<pid>/<session>` with a
`resources` subfolder and a `commands.log` holding every command and its result.
Setting `TerminalInput` runs a command line: the dispatcher splits it, looks the
name up as a script function under `box.command.commands`, converts the arguments
and writes the text result to `TerminalOutput`. Commands are grouped per file:
shell, search, logs, script, capture, render and input.

The HTTP layer is glue over those components. `/terminal/new`, `/terminal/close`,
`/terminal/exec` and `/resource/<path>` are served by an `HttpServer` singleton
whose port comes from `--port`; requests are dequeued in OnLoad so handlers run on
the main thread.

`CommandScript` runs a file of command lines on a session when it is set, one
command per line, skipping blank lines and `#` comments; a quoted argument that a
line leaves open continues on the next line. `Terminal.log` copies the session log
to a file outside the process folder, which is how the command integration tests
collect it.

## Checkpoints

`box.checkpoint` turns the state of an instance back into script source. The
`checkpoint` command walks every managed script, records the value of every
component of every entity the script owns in an edit set of the script addon and
writes the source that the edit set produces to
`<process folder>/checkpoints/<name>/<script>.flecs`. Only edited statements
change, so the rest of the file survives byte for byte. The `Checkpoint`
singleton points the folder elsewhere and keeps the last one and the count.

What a checkpoint leaves out is as important as what it writes. A component the
engine derives, such as `WorldTransform` or the handles in `RenderMesh`, carries
the `NoCheckpoint` tag, which keeps the statement the author wrote; the module
tags the components of the box modules when a checkpoint runs. An entity that no
statement of a script declares cannot be written back at all: entities created
by C code, by a command or inside a `for` loop have no source location, so they
are reported as `not checkpointed: <path>` instead of being dropped. Entities of
a template body are shared by every instance and are counted, not written.

## Headless and agents

Every process creates `/tmp/box/<pid>` and removes it on exit. `--headless`
requires `--frames` or `--timeout`, respects `--cpu-budget` by sleeping when it
exceeds its share, and refuses to start when the CPU is oversubscribed. Headless
rendering uses an offscreen target so `capture` and `render` commands work
without a window.

## Coding rules

See `design/coding_style.c`. In short: file header with purpose, one comment per
function and type, no other comments, asserts over defensive ifs, files under
2000 lines, functions under 200 lines, single exit point per function.
