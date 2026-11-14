# Projection

`box.projection` (`BoxProjection`, `include/box/projection.h`) places an entity of one scene (a
screen space UI scene) at the projection of an entity of another scene (3D) as a view's camera sees
it: nameplates, health bars, markers. It is the only cross scene reference. C names have a
`Projected` prefix that script names drop.

## Components

| Script | C | Fields | Notes |
| --- | --- | --- | --- |
| `Anchor` | `ProjectedAnchor` | `target, view, offset` | Every frame `Position.x/y` = surface points of the projected target position (+ `offset` world units), origin at the surface center, y up; `z` kept. Adds `Position` |
| `box.projection.Box` | `ProjectedBox` | `min, max, offset, fit_width, fit_height, min_width` | Project a box around the target instead: entity goes to the top center of the projected rectangle plus `offset` (vec2, points). Adds `Bounds` |
| `Bounds` | `ProjectedBounds` | `left, right, top, bottom` | Output: projected rectangle in surface points, not clamped; written without OnSet; kept while hidden |
| `Hidden` | `ProjectedHidden` | tag | Added while not visible |

- `ProjectedBox.min/max` are world units along world axes from target position + anchor offset;
  target rotation and scale are ignored.
- `fit_width` / `fit_height` set the `LayoutItem` width/height to `Fixed` at the rectangle size; the
  tree is laid out again the same frame (Grow children split it, transforms final).
- `min_width`: rectangle narrower than this (points) is hidden; 0 = any.
- Elements are centered on `Position`: a bar of height h sits 6 points above the box with
  `offset: {0, 6 + h / 2}`.

## Usage

The UI scene needs an ortho camera at `ortho_scale: 1` so one point is one world unit. Write
`box.projection.Box` / `box.geometry.Box` in full when both modules are imported.

```flecs
using box.transform
using box.geometry
using box.camera
using box.view
using box.layout
using box.projection

world_camera {
  Position: {0, 4, 10}
  Rotation: {-0.35, 0, 0}
  Camera: {fov: 50}
}
world_view { View: {camera: world_camera} }

house {
  Position: {-3, 1, 3}
  box.geometry.Box: {2, 2, 2}
}

ui {
  Scene: {}
  camera {
    Position: {0, 0, 100}
    Camera: {ortho: true, ortho_scale: 1}
  }

  house_bar {
    Anchor: {target: house, view: world_view}
    box.projection.Box: {min: {-1, -1, -1}, max: {1, 1, 1}, offset: {0, 10}, fit_width: true}
    Layout: {direction: Row, padding: {1, 1, 1, 1}}
    Item: {width: {mode: Fixed}, height: {mode: Fixed, value: 8}}
    Rect: {1, 8}
  }
  view { View: {camera: camera, lighting: {enabled: false}, culling: {enabled: false}} }
}
```

From C: `ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});`

## Hidden

`Hidden` is added when the target is behind the camera, past its far plane, off view, or the target,
view or camera is missing; for a `Box` when all of it is (corners behind the near plane are clipped,
edge crossings projected), the rectangle misses the surface or is narrower than `min_width`. A hidden
entity keeps its last `Position` and its subtree's world transforms collapse to a point (not drawn).

## Systems and timing

| System | Phase | Does |
| --- | --- | --- |
| `box.projection.Update` | OnStore | After transforms, before render extract: writes `Position` and `Bounds` without OnSet, writes `WorldTransform` of the anchor and its subtree (OnSet only when it changed, which draws and the spatial index follow) |
| `box.projection.Relayout` | OnStore, immediate | Lays out trees whose `LayoutItem` was resized by `fit_*`, rewrites their subtree transforms |

- Position is final for the frame; the static transform system does not recompute the anchor.
- Views use the surface size in points (`box_surface_view_size`) of the view they project through.

## C API

- `box_projection_anchored(world, e)`: e or a `ChildOf` ancestor has `ProjectedAnchor`. Anchored
  subtrees are excluded from bakes, like `DynamicTransform` shapes and `NoBake`.
