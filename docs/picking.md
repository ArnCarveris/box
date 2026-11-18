# Picking

`box.picking` (`BoxPicking`, `include/box/picking.h`) sends the pointer events of a frame to the
entity under the cursor. Every view casts a ray from its camera into its scene, front view first, so
screen UI, world UI and 3D shapes are picked the same way. Events go out through `flecs.script.input`
(`on.enter`, `on.click`, ...), and an `ActionTrigger` drives an input `Action`.

## Components

| Name | Fields | Does |
| --- | --- | --- |
| `Interactive` | tag | Handles pointer events that hit it or its children; a press on it captures the pointer |
| `InputBlocker` | tag | Takes events that hit it or its children so they don't reach views behind (e.g. a HUD panel over the 3D scene); handles them like `Interactive` when no `Interactive` is on the path; hides presses/wheel from world actions |
| `ActionTrigger` | `action, mode, button` | Mouse button on the element drives an `Action`. Adds `Interactive`. `mode`: `ActionTriggerClick` (default), `ActionTriggerPress`, `ActionTriggerHold`; `button` default `KeyMouseLeft` |
| `Cursor` | `shape` | Pointer shape over the element (`CursorPointer`, `CursorNotAllowed`, ...); first `Cursor` on the path from target to handler wins |
| `PickRange` | `distance` | Hits on it or its children further than `distance` from the camera are no hits (the ray goes on behind); the nearest one up the path counts, 0 = no limit. E.g. a world-space screen used within reach |
| `Hovered` | tag (output) | On the entity the events go to while the cursor is over it (stays on a captured entity) |
| `Pressed` | tag (output) | On the entity a press went to while the button is held |
| `PickStatus` | singleton (output) | `target, handler, view, pressed, hit, hit_view, point, distance, cursor, captured, drag, shape` |

Script enum constants are written in full: `ActionTrigger: {action: a, mode: ActionTriggerHold}`.

## Script usage

```flecs
using box.picking

crate {
  Position: {-4.4, 0.6, -1.5}
  Box: {1.2, 1.2, 1.2}
  ActionTrigger: {action: state.actions.select}   // click taps the action
  Cursor: {CursorPointer}
}

button {
  Interactive
  Cursor: {CursorPointer}
}

// async blocks go in the root scope or a template, not in a plain entity
async {
  while true {
    await on.enter(button)
    // hovered
    await on.leave(button)
  }
}
async {
  while true {
    const e = await on.drag(button)   // e.local_x: cursor in the element's local space
  }
}

// clicks on the panel never reach the 3D scene behind it
panel {
  InputBlocker
  Rect: {1, 1}
}
```

Events seen in scripts: `on.enter`, `on.leave`, `on.move`, `on.press`, `on.release`, `on.click`,
`on.drag`, `on.scroll` (registered by box.picking; resolves when the wheel moves over the target,
`delta` is the wheel offset). For actions use `await on.pressed(action)` (box.input).

## How a pick works

1. `box.picking.Update` runs in **PreUpdate**: sends the events box.input queued this frame, picking
   against what the last frame placed, then picks again under a still cursor so hover follows moving
   things. Consecutive moves with the same buttons are coalesced into the last; presses, releases, wheel and keys are
   never dropped or reordered.
2. Views of the surface the cursor is on (`Input.surface`, else the focused surface, else the first
   without a texture) are visited by descending `View.order`, then descending id (at most 64 views).
3. Each view casts its camera ray into its scene through the spatial index (`docs/spatial.md`),
   then a narrow phase: UI elements by area (`LayoutBounds`, else `Rect`/`Circle`/`TextBounds`),
   `Polygon2D` by outline, shapes by triangles. Hits outside a `ClipChildren` ancestor don't count.
4. Nearest hit wins; on a tie UI elements beat shapes and the element drawn last wins.
5. A hit on something that shows a surface texture (`Image`, `PbrTextures`, `TextureBlend` mask)
   picks on through that surface at the hit UV (nesting depth up to 4).
6. The event bubbles from the hit up to the first `Interactive` (else `InputBlocker`) ancestor,
   the handler. No handler: the event passes to the views rendered before. Nothing handles it:
   it goes to `box.input.screen` (`PickStatus.target` 0).

## Rules and gotchas

- No `Interactive`, `InputBlocker` or `ActionTrigger` on the path: the hit passes through and the
  entity gets no handled events.
- Bare UI elements (no Rect, Circle, Line, Polygon2D, Image, Border, Text, TextBounds, ShapeMesh,
  Interactive or InputBlocker), such as a full window layout root, are transparent to the pointer.
- Never picked: `Ignore` subtrees (box.spatial; they don't block either), disabled entities, prefabs,
  entities without a `WorldTransform`.
- Capture: a press on a handler captures move/drag/release until that button is released, even
  outside the element, view or window; nothing else is entered or left. Deleting it ends the capture.
- `ActionTrigger` changes the action in the frame after the pointer event (picking runs after
  ActionUpdate). Taps queue: N clicks give N taps. A mouse `Binding` of the button still sees the
  press unless an `InputBlocker` is on the path. Disabled entities and prefabs never trigger.
- `on.scroll` waiters: the innermost waiting entity on the path wins, past the handler, so a button
  inside a scroll view does not eat the wheel.
- Tie order of 2D elements follows draw order: floating elements (and descendants) last, parents
  before children, siblings by `Position.z`, then clipped overlays, then `OrderedChildren` order or id.

## Cost

- Picking queries the spatial index every frame while a cursor is known, even without movement.
- Meshes with 64+ triangles get a triangle BVH built on a worker on first pick (linear scan until
  ready); it is dropped 600 frames after no shape uses the mesh.
- Many movers under the cursor: each mover leaf hit tests its shapes one by one.

## C API

- `box_view_cursor_ray(world, view, origin, dir)`: world ray through the cursor in a view (camera
  and cursor only, ignores hits, blockers and views on top). Use it to meet your own geometry, e.g. a
  ground plane. False when the view has no camera, the cursor is on another surface, the surface
  renders to a texture, no pointer event arrived yet, or the cursor is outside the view. Systems
  after PreUpdate get this frame's ray.
- Read `PickStatus` (`ecs_singleton_get(world, PickStatus)`) for `hit`, `point`, `distance`,
  `drag` (cursor movement since the press while captured).
