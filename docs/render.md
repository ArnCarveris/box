# Render

`box.render` turns the world into WebGPU frames: each frame the main thread extracts what changed
into a render packet, and a render thread (the main thread in the browser) hands it to the
backend. `box.shader` (same directory) preprocesses and compiles the WGSL files. What is drawn and
how is set by components of other modules: `View`/`Scene` (view.md), lights and bakes (light.md),
materials (material.md), effects (effects.md), atmosphere, textures, surfaces.

## Frame flow

| Step | Where | Notes |
|---|---|---|
| `box.render.Extract` phase | main thread, after `OnStore` | systems `box.render.extract.*` (Views, Draws, Lights, Shadows, Bake, Reflect, ...) fill the packet |
| `box.render.Submit` phase | main thread | hands the packet to the ring; `TaskPublish` (box.task) runs after it |
| backend `render` | render thread, or main thread when `Settings.main_thread` | gpu backend (`box_render_gpu_backend`) or null backend |

- A world uses the null backend until a `Surface` is set or a capture is requested
  (`box_render_use_gpu`); a world without surfaces never creates a device.
- Pipelines build in the background; a batch whose pipeline is not ready is skipped that frame.
  Headless runs and frames with a capture wait for their pipelines. Compiling reports the blocking
  task "Compiling shaders" (`InProgressTask`).
- Headless runs capture every pending light-bake and reflection probe in one frame (no budget).

## Components

| Component | Script name | Fields / meaning |
|---|---|---|
| `RenderSettings` (singleton) | `box.render.Settings` | `in_flight` packets 1..8 (default 2), `main_thread` (default true in the browser only); applied before the next frame |
| `RenderStatus` (singleton, read only) | `box.render.Status` | `submitted`, `rendered`, `in_flight`, `stalls` (frames that waited for a free packet), `threaded`, `upload_bytes` (instance bytes uploaded last frame), `vsync`, `refresh_rate`, `presented`, `present_ms`, `presents_late`/`presents_early`, `device_losses`, `shadow_tiles`, `shadow_dropped`, `shadow_cascades`, `shadow_cascades_kept` |
| `CullStatus` (on each `View`, read only) | `box.render.CullStatus` | `frame`, `instances`, `visible`, `frustum_culled`, `occlusion_culled`, `batches_skipped`, `casters_skipped`; GPU only, arrives a variable number of frames late: wait for `frame`, don't count frames |
| `Shader` | `box.shader.Shader` | `path` (WGSL file), `defines` (`NAME` or `NAME=value`, comma separated) |
| `ShaderStatus` (read only) | `box.shader.Status` | `revision`, `ok`, `error` (`file:line: message`, NULL when ok) |

Tags from other modules that change how instances upload (see Instance memory):
`DynamicTransform` (transform.h), `DynamicMaterial`, `NoShadow`, `LightFixture` (material.h).

## Minimal lit scene

```flecs
using box.transform
using box.geometry
using box.material
using box.camera
using box.view
using box.light
using box.effects
using box.surface

$ { Scene: {baked: true} }         // features of the default scene's mesh shaders

window { Surface: {width: 1280, height: 720, vsync: true} }

camera {
  Position: {0, 2.5, 6}
  Rotation: {-0.35, 0, 0}
  Camera: {fov: 60}
}
sky {
  Rotation: {0.7359, 2.3726, 0}
  DirectionalLight: {intensity: 2 * flecs.script.math.PI}
}
view {
  tonemap { Tonemap: {} }
  smaa { Smaa: {} }
  View: {
    camera: camera,
    clear: {enabled: true, color: {120, 150, 190, 255}},
    shadows: {enabled: true},
    effects: [{effect: tonemap}, {effect: smaa}]
  }
}
cube {
  Position: {0, 0.5, 0}
  Box: {1, 1, 1}
  Rgb: {220, 90, 60}
}
```

## Scene features (gotcha #1)

`Scene` members (view.h) choose what the mesh shaders of a scene support; **all are false by
default**. A shape that needs a feature its scene lacks draws as if it did not have it (no error).
Members: `baked`, `reflections`, `punctual_shadows`, `cookies`, `environment`,
`aerial_perspective`, `alpha_cutout`, `blend_modes`, `transmission`, `texture_blend`, `triplanar`,
`sheen`, `clear_coat`, `anisotropy`, `diffuse_transmission`, `fresnel`, `water`, `distortion`,
`planar_reflections`, `texture_scroll`, `planet_light`, `wind` (only the batches of `WindSway`
shapes get the wind pipelines; every other batch keeps those of a scene without it).

- Default scene: `$ { Scene: {...} }`. Nested scene: `Scene` on a root entity; an entity belongs
  to its nearest ancestor with `Scene`.
- A view renders only the scene of its camera: put the camera inside the scene it should see.
- `View.lighting.baked/reflections/atmosphere/depth_prepass` can only turn off features the scene
  has; turning them off compiles no other pipelines.
- Each feature set is its own pipeline; Transmission, triplanar mapping, Sheen, ClearCoat,
  SpecularAnisotropy, DiffuseTransmission and Fresnel draw with the heavier `mesh_extended` shader.
- `AlphaCutoff` without `alpha_cutout` draws opaque; `Opacity` < 1 or a `BlendMode` without
  `blend_modes` draws opaque.

## Debug drawing (`View.debug`, `ViewDebug`)

Drawn after the view's effects, not tone mapped, with the view depth read only. Included in a
`capture` of a camera that a view renders; never in bakes or probe captures. All off = no pass.

| Field | Values / defaults |
|---|---|
| `grid` | `{enabled, height 0, cell_size 1, major_every 10, fade_distance 100, line_width 1, color, major_color}`; X axis red, Z blue |
| `wireframe` | `WireframeOff`, `WireframeOverlay` (depth tested), `WireframeOnly` (see-through, no shading/shadows/AO, frustum culling only) |
| `wireframe_color`, `wireframe_width` | default `{255, 255, 255, 200}`, 1 px; 3D meshes only |
| `overdraw` | `OverdrawOff`/`All`/`Plain`/`Cutout`: prepass fragment heat map, black..white at 8+; opaque meshes only |
| `light_probes`, `reflection_probes`, `probe_size` | probe spheres; need `LightSettings.baked` / `.reflections` on the view |
| `bvh` | `{static_tree (green), dynamic_tree (orange), ui_tree (magenta), leaves (default true), levels}`; at most 1048576 boxes |

```flecs
View: {
  camera: camera
  debug: {grid: {enabled: true, fade_distance: 80}, wireframe: WireframeOverlay}
}
View: { camera: room.camera, debug: {light_probes: true, reflection_probes: true, probe_size: 0.15} }
```

Script `View: {...}` keeps the constructor defaults for unset members; a C literal `View` starts
from zero (unlit, no culling, no debug).

## Capture, render to PNG, tests

```sh
bake3 run apps/boxrun --local-env -- --headless --frames 3 --delta-time 1/60 --out /tmp/town.png etc/scenes/town.flecs
python3 tools/integration.py visual                          # render visual scenes, compare to baselines
python3 tools/integration.py visual --update-baseline <scene>
bake3 run test/cpu/render --local-env -- -j 8    # null backend: extract, batches, transitions
bake3 run test/gpu/render --local-env -- -j 4    # needs a GPU: captures, shaders, culling, passes
```

Shell commands (REST port, console, browser): `capture [camera|surface]` (camera as its view
shows it, with effects and debug; a surface with all its views), `render <subject>` (an entity,
prefab or template alone, 512x512, plain lit, no shadows or effects). Both return a resource that
is empty until the frame is written.

C: `Surface.write_to_file` writes a PNG every frame (relative paths go to `AppStatus.tmp_dir` when
headless). `box_render_capture(world, &(box_render_capture_desc_t){.camera, .width, .height,
.path})` (src/render/render.h) captures one frame, max 8192 px per side; a camera that no view
renders is captured lit, without shadows, effects or debug. Call `box_render_sync(world)` to wait
until every submitted packet rendered and its captures are written.

## Shaders

Engine shaders are entities `box.shader.builtin.<name>` (`mesh`, `mesh_extended`, `shape2d`,
`shadow`, `cull`, `scatter`, `tonemap`, `bloom`, `smaa`, `gtao`, `fog`, `atmosphere`,
`debug_grid`, ...) with `Shader {path: "etc/shaders/<name>.wgsl"}`. Most compile only once a
batch uses them.

- Preprocessor: `#include "file"` (relative to the includer, then like a path), `#define NAME
  value` (object-like only; function macros are an error), `#undef`, `#ifdef`, `#ifndef`, `#else`,
  `#endif`, `#pragma once`.
- Paths resolve from the working directory and its ancestors, then the executable's folder up.
- Hot reload: the file watch (`FileWatch`, every 0.25 s by default) recompiles a shader when it or
  any file it includes changes. Setting `Shader` again also recompiles.
- A failed recompile keeps the old module running; the error is in `ShaderStatus` until the
  outcome of newer code arrives (can take more than a frame in the browser).
- Override an engine shader: `ecs_set(world, ecs_lookup(world, "box.shader.builtin.blit"), Shader,
  {.path = "my_blit.wgsl"})`. Nothing draws with an application `Shader` of its own; it only
  compiles and reports status.

## Instance memory

Shapes are instanced in persistent batches keyed by scene, mesh, pipeline, blend, cutout,
transmission/water, back-face mode, texture wrap, `PbrTextures` entities, `TextureBlend`,
`DynamicTransform`, `DynamicMaterial`, shadow (`NoShadow`), `LightFixture`, parametric surface,
MappingFrame and scene features. Changing any of these moves the shape to another batch, and a
batch splits after one page worth of instances.

| Store | Transform | Shading | Surface | Frame | Slots per 128 MiB page |
|---|---:|---:|---:|---:|---:|
| Plain mesh | 48 | 16 | - | - | 2,796,202 |
| Mesh with parametric surface | 48 | 16 | 384 | - | 349,525 |
| Mesh with MappingFrame material | 48 | 16 | (384) | 48 | by largest stride |
| 2D shapes and glyphs | 48 | 64 | - | - | 2,097,152 |

- Static streams upload only changed slots through the scatter pass (entry = 4-byte slot + record,
  segments 16-byte header, 256-byte aligned). `DynamicTransform` batches upload all transforms
  every frame; `DynamicMaterial` batches all shading every frame.
- Material table: 336-byte records shared by content (hashed, ref counted); a steady frame hashes
  nothing. Prefabs, `DynamicMaterial` shapes, `TextureScroll` and running material transitions
  get private records; dynamic ones re-upload every frame, scrolling ones only on change (the
  shader animates them from a per-frame clock in record 0).
- Runs and page buffers grow by 1.5x (multiple of 4, min 16 slots); a new batch starts at its
  exact count. Grown buffers keep old contents with GPU copies; pages never shrink.
- 48-byte full 3x4 transforms are required (shear from rotated children of nonuniform parents).
- Costs to watch: `RenderStatus.upload_bytes`, and `box_render_draws_stats` (batches, pages,
  `page_*_bytes`, `transform_bytes`, `shading_bytes`, `materials`, copies) in src/render/render.h.
  Measure with `bake3 run --cfg release bench/render --local-env` (Cull needs a GPU).

## Gotchas

- Mark moving shapes `DynamicTransform` and per-frame material changes `DynamicMaterial`;
  changing a static shape every frame scatters it every frame and re-hashes its material.
- Transitions resolve on the render side: during a transition the component holds the target.
- `RenderStatus`/`CullStatus` lag the main thread by the packets in flight; `box_render_sync`
  before asserting in tests. `box_render_gpu_read_buffer` needs `Settings.main_thread`.
- `RenderStatus.device_losses` counts recreated GPU devices; the browser neither counts nor
  recovers a loss (the page must).
