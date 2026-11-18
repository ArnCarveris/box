# View

`box.view` (`BoxView`, `include/box/view.h`) holds the components that say what a frame draws:
`View` (a camera, its settings and post effects) and `Scene` (a scene root and the shader features
it enables). Rendering itself lives in `box.render`.

## View

| Field | Type | Default (ctor) |
| --- | --- | --- |
| `camera` | entity | 0: a view without a camera is skipped |
| `clear` | `ClearSettings {enabled, color}` | off |
| `lighting` | `LightSettings` | enabled, baked, reflections, depth_prepass, atmosphere, fog_volumes on; environment intensity 1 |
| `shadows` | `ShadowSettings` | **disabled**; cascades 4, resolution 2048, max_distance 200, split_lambda 0.7, depth_bias 2, normal_offset 2, filter_size 3, fade 0.2, blend 0.1, interval {1,1,1,1} |
| `culling` | `CullSettings {enabled, occlusion, min_dimension_px, bounds}` | enabled, occlusion, 1 px, bounds false |
| `effects` | `[{enabled, effect}]` | empty; entries are enabled by default, applied in list order |
| `debug` | `ViewDebug` | nothing drawn |
| `order` | i32 | 0 |
| `lod` | `LodSettings {levels, distances[4]}` | levels 0 (full detail) |

A C literal `View` skips the ctor: unlit, no culling, no debug.

```flecs
using box.camera
using box.view
using box.effects

view {
  bloom { Bloom: {intensity: 0.2, threshold: 1} }
  tonemap { Tonemap: {} }
  View: {
    camera: camera,
    clear: {enabled: true, color: {120, 150, 190, 255}},
    lighting: {enabled: true, environment: {source: sky, intensity: 0.6, background: false}},
    shadows: {enabled: true, resolution: 4096, max_distance: 120},
    effects: [{effect: bloom}, {effect: tonemap}]
  }
}

// screen space UI drawn over the 3D view (created later, same order)
hud_view { View: {camera: hud.camera, lighting: {enabled: false}, culling: {enabled: false}} }
```

### Settings details

- `LightSettings`: `enabled, baked, reflections, depth_prepass, atmosphere, fog_volumes,
  environment {source, intensity, yaw, background}, scene` (scene whose lights light the view, 0 for
  its own). `baked`, `reflections`, `atmosphere` only work for features the `Scene` enables.
  `depth_prepass` draws opaque meshes depth only first, then shades with an Equal test; `Gtao` needs it.
- `ShadowSettings`: cascades of the first directional light plus tiles of shadow casting spot lights.
  `cascades` max 4. `filter_size` 1..7 (even rounds up). `min_caster_texels` needs culling enabled.
  `interval[i]` renders cascade i every n frames (offset by index); all re-render on the first frame
  and on light direction, camera projection, resolution, cascades, max_distance, split_lambda or
  min_caster_texels change.
- `CullSettings`: occlusion tests against what the view drew first in the same frame. Cascades cull by
  frustum only. `bounds` also skips whole batches whose world box is out of the frustum.
- `LodSettings`: shapes with segment counts (round shapes, RoundedBox, Bevel, Sweep, Loft, Lathe,
  shells); level i starts at `distances[i]` and halves segments of the previous level;
  `levels` at most `BOX_LOD_LEVELS_MAX` (4). Example: `lod: {levels: 3, distances: [0, 12, 36, 0]}`.

### Debug (`ViewDebug`, drawn after effects, not tone mapped)

| Field | Notes |
| --- | --- |
| `grid` | `GridSettings {enabled, height, cell_size 1, major_every 10, fade_distance 100, line_width 1, color, major_color}`; `cell_size` 0 draws none |
| `wireframe` | `WireframeOff`, `WireframeOverlay`, `WireframeOnly` (no shading, frustum/size culling only, no shadows/AO) |
| `wireframe_color`, `wireframe_width` | default white alpha 200, 1 px |
| `overdraw` | `OverdrawOff`, `OverdrawAll`, `OverdrawPlain`, `OverdrawCutout` heat map of the depth prepass |
| `light_probes`, `reflection_probes`, `probe_size` | Probe spheres the view samples (needs `lighting.baked` / `reflections`) |
| `bvh` | `BvhDebug {static_tree, dynamic_tree, ui_tree, mover_trees, leaves (default true), levels}`: spatial index boxes, static green, dynamic orange, UI magenta, the tree of each mover in cyan, oriented by the mover, with `levels` counted from its root; at most `BOX_VIEW_DEBUG_BOXES_MAX` boxes |

```flecs
View: {camera: camera, debug: {wireframe: WireframeOverlay, wireframe_color: {0, 255, 0, 255}}}
View: {camera: camera, debug: {grid: {enabled: true}, bvh: {dynamic_tree: true}}}
```

Enum constants are written in full in scripts (`WireframeOverlay`, not `Overlay`).

## Order and surfaces

- Views render by ascending `order`, then entity id (creation order); picking and input go the other
  way, so a higher order draws on top and is hit first. Order changes apply next frame.
- Reserved orders: `BOX_VIEW_ORDER_EDITOR` 1000000, `_FPS` 1500000, `_STARTUP` 1750000, `_CONSOLE`
  2000000. Game views stay well below (normally 0).
- A view under a surface's `view_root` renders only to that surface; other views render to the
  surfaces without a `view_root` (`docs/surface.md`).
- A screenshot of a camera a view renders includes the debug drawing; probe/reflection captures and
  bakes do not.

## Scene

`Scene` on an entity makes it a scene root; entities belong to their nearest ancestor with `Scene`
(scenes nest), else the default scene, configured via the singleton (`$ { Scene: {...} }`). A view
renders only its camera's scene, lit by that scene's lights.

All fields are false by default (smallest shaders); a shape needing a disabled feature draws as if it
did not have it: `baked, reflections, punctual_shadows, cookies, environment, aerial_perspective,
alpha_cutout, blend_modes, transmission, texture_blend, triplanar, sheen, clear_coat, anisotropy,
diffuse_transmission, fresnel, water, distortion, planar_reflections, texture_scroll, planet_light,
wind`.

```flecs
$ { Scene: {baked: true, reflections: true, environment: true} }
hud { Scene: {blend_modes: true} }   // BlendMode and Opacity < 1 draw opaque without it
```

Gotcha: a 3D feature that "does nothing" (alpha cutout, transparency, reflections) is usually a
missing `Scene` flag, or the view flag (`lighting.baked`, `lighting.reflections`) is off.

## C API

- `box_view_compare(order_a, a, order_b, b)`: render order comparison (order, then id).
- `box_view_cursor_ray(world, view, origin, dir)`: cursor ray in a view, in `box.picking`
  (`docs/picking.md`).
