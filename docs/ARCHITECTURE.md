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
etc/             default scenes, shaders (WGSL) and assets, embedded on wasm
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
| `box.input` | device layer (`Input` singleton) and action layer (`InputMap`) |
| `box.command` | terminal sessions, HTTP endpoints, script functions for commands |
| `box.checkpoint` | script state capture and restore |
| `box.monitor` | CPU, system, GPU and ECS statistics |
| `box.script` | scene loading, incremental template instantiation |
| later | `box.ui`, `box.particles`, `box.transition`, `box.spline`, `box.lod`, `box.job`, `box.atmosphere` |

Each module has `include/box/<module>.h` with its components and a single
`box_<module>Import` function, and `src/<module>/` with one file per concern
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
