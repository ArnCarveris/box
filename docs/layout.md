# Layout

Clay-style flex layout for UI trees in logical pixels: rows, columns, fit/fixed/grow sizing,
padding, alignment, scrolling, floats and clipping. Writes `Position`, `LayoutBounds` and the
`Rect` of laid-out elements. Module `box.layout` (`BoxLayoutImport`). Script names drop the
`Layout` prefix: `Layout`, `Item`, `Bounds`, `Fill`, `Scroll`, `Content`, `Float`.
(3D placement such as `Fit`/`Align`/`Array` is `box.spatial`, not this module.)

## Components

| Component (script name) | Key fields | What it does |
|---|---|---|
| `Layout` | `direction` (`None`/`Row`/`Column`), `padding {top, bottom, left, right}`, `spacing`, `h` (`Left`/`Center`/`Right`), `v` (`Top`/`Middle`/`Bottom`) | Arranges children. Implies `LayoutBounds` and `OrderedChildren` |
| `LayoutItem` (`Item`) | `width`, `height` (`Sizing`), `h`, `v`, `x`, `y` | How an element sizes itself; anchor/offset only used in a `None` parent. Implies `LayoutBounds` |
| `Sizing` | `mode` (`Fit`/`Fixed`/`Grow`), `value` (size or grow weight, 1), `min`, `max` (0 = none), `content_min` (true) | One axis of an `Item` |
| `LayoutBounds` (`Bounds`, written) | `width`, `height` | Computed size |
| `LayoutFill` (`Fill`) | `surface` | Element takes the surface size in points (sets a Fixed `Item`). Implies `LayoutItem` |
| `LayoutScroll` (`Scroll`) | `x`, `y` (offset, +y shows content further down), `horizontal`, `vertical` | Shifts children (CSS `overflow: auto`). Implies `LayoutContent` |
| `LayoutContent` (`Content`, written) | `width`, `height`, `max_x`, `max_y`, `x`, `y` (applied, clamped) | Content size and scroll limits |
| `LayoutFloat` (`Float`) | `anchor` (0 = parent), `h`, `v`, `anchor_h`, `anchor_v`, `x`, `y`, `flip`, `clip`, `hide_clipped` | Out-of-flow popup placed against an anchor |
| `ClipChildren` (tag) | | Clips descendants' Rect, Circle, Line, Polygon2D, Border, Image, Text to the element's area |

## Flecs script

```flecs
using box.transform
using box.camera
using box.surface
using box.view
using box.geometry
using box.material
using box.layout
using box.text

window { Surface: {width: 1280, height: 720} }

// Screen space: ortho camera at ortho_scale 1, one logical pixel = one point
hud {
  Scene: {blend_modes: true}
  camera { Position: {0, 0, 100}; Camera: {ortho: true, ortho_scale: 1} }

  screen {
    Fill: {surface: window}
    Layout: {}

    panel {
      Layout: {direction: Column, spacing: 10, padding: {14, 14, 16, 16}}
      Item: {h: Left, v: Middle, x: 16, width: {mode: Fixed, value: 300}}
      Rect: {1, 1}                  // placeholder, the layout writes the size
      Rgb: {16, 20, 28}
      Border: {width: 1, radius: 10, color: {90, 110, 150, 255}}

      row {
        Layout: {direction: Row, spacing: 8, v: Middle}
        Item: {width: {mode: Grow}}
        name { Text: {value: "Wolves", size: 16} }
        gap { Item: {width: {mode: Grow}} }   // pushes the next child to the right
        amount { Text: {value: "3 / 5", size: 16} }
      }
    }
  }

  view { View: {camera: camera, lighting: {enabled: false}, culling: {enabled: false}} }
}
```

World-space UI: put the tree under an entity with `Scale: {0.008, 0.008, 0.008}` (a root keeps
its own `Position`).

Scroll view and popup (from `test/assets/ui/scroll.flecs`, `dropdown.flecs`):

```flecs
view {
  Layout: {direction: Column}
  Item: {height: {mode: Fixed, value: 200}}
  ClipChildren
  Scroll: {y: 40, vertical: true}          // read Content.max_y to clamp a script offset
}
list {
  Float: {h: Left, v: Top, anchor_h: Left, anchor_v: Bottom, y: -2, flip: true, hide_clipped: true}
}
```

## Rules

- Units are logical pixels, y up. `Position` written for a child is its center in the parent's
  centered frame; `z` is kept.
- Sizing: `Fit` = content + padding (or the shape/text size); `Fixed` = `value`; `Grow` = weighted
  share of the parent's free space on the main axis, stretch across it (and in `None`). Fitting
  elements shrink down to their content minimum when space is short (as Clay compresses).
- Content minimum = widest word of a wrapping text, else the fit size. `content_min` keeps a grow
  share above it; weight 0 takes no share.
- Elements without `Layout` on the parent are roots: sized with their children but never placed
  (keep their own `Position`). A root inside a UI tree by mistake logs a warning once.
- `DirectionNone`: children with `Item` are anchored by `h/v` plus `x/y`; children without `Item`
  keep their `Position`.
- `Rect` is an output for elements with a `Layout` or a non-`Fit` axis (`Rect: {1, 1}` is a
  placeholder). For a leaf with Fit on both axes, the `Rect` is the input size. A `Circle` leaf
  measures as its diameter.
- `Text` leaves measure via `box.text` and wrap at the resolved width (`TextWrapWords`).
- Child order follows `OrderedChildren` (implied by `Layout`), i.e. declaration order. Prefab
  children are skipped.
- Scroll offsets are clamped to `0..max` when applied but never written back: the script owns
  `Scroll.x/y`. A scrolled axis does not narrow children. Changing only the offset moves children
  without re-measuring.
- Floats draw and pick above non-floating elements, ignore their parent's clips (unless `clip`),
  follow the anchor in the same frame and need a parent with `Layout`. `flip` keeps them inside
  the root/fill surface. On a root a float is ignored with a warning.
- `ClipChildren` area = `LayoutBounds` (else `Rect`) inside the `Border` width, rounded by its
  radius. Nested clips intersect; picking misses outside. 3D shapes are not clipped. Tab skips
  focusables fully clipped away unless the clip has a `LayoutScroll`.

## Systems and C API

| Name | Phase | Does |
|---|---|---|
| `box.layout.FillUpdate` | `EcsPostUpdate` | Copies surface sizes into `Fill` items |
| `box.layout.Update` | `EcsPostUpdate` (immediate) | Lays out trees whose input changed, then places floats |

`box_layout_update(world)`: lay out changed trees now, for input changed after PostUpdate (e.g.
`box.projection`). Call outside deferred mode. World transforms of moved children follow next
frame unless computed by the caller.

## Cost and gotchas

- Only trees marked dirty (Layout/Item/Rect/Circle/Scroll/Float add/set/remove, child Position
  change in a parent with `Layout`, reparenting) are re-solved; an idle UI costs nothing.
- `Bounds`/`Content` only change when the layout runs (PostUpdate), after the input events of a
  frame; a script reacting to an event reads the values of the previous layout.
- `Bounds` collides with `box.text.Bounds`: write `layout.Bounds` when both modules are in `using`.
- `padding` order is `{top, bottom, left, right}`, not CSS order.

## Tests

```sh
bake3 run test/cpu/ui --local-env -- Layout     # also LayoutFill, LayoutFloat, LayoutScroll, Wrap
```
