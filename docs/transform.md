# Transform

`box.transform` (`BoxTransform`, `include/box/transform.h`) owns the local pose components and keeps
`WorldTransform = parent * T * R * S` up to date, on change for static entities and every frame for
dynamic ones.

## Components

| Name | Fields | Notes |
| --- | --- | --- |
| `Position` | `x, y, z` | Relative to the parent. Adds `WorldTransform` |
| `Rotation` | `x, y, z` | Euler radians: x pitch, y yaw, z roll. Matrix is `Ry * Rx * Rz` (`glm_euler_yxz`); pitch + yaw never roll, so cameras keep a level horizon. Adds `Position` and `WorldTransform` |
| `Scale` | `x, y, z` | Relative to the parent, ctor sets `{1, 1, 1}`. Adds `Position` and `WorldTransform` |
| `WorldTransform` | `m` (mat4) | Local to world, computed by the engine. Adds the private `WorldTransformVersion` |
| `DynamicTransform` | tag | Recompute every frame instead of on change; spreads to descendants |
| `ManualTransform` | tag | The engine does not write `WorldTransform`; the application does |
| `DynamicTransformSpread` | prefab | Holds `DynamicTransform`; descendants of dynamic entities get `(IsA, DynamicTransformSpread)` |

```flecs
using box.transform

ship {
  Position: {0, 2, 0}
  Rotation: {0, 0.4, 0}
  Scale: {2, 2, 2}
  DynamicTransform        // ship and its children recompute every frame
  turret { Position: {0, 1, 0} }
}
```

## Systems and observers

| Entity | Phase | Does |
| --- | --- | --- |
| `box.transform.dynamic.Update` | PreStore | All `DynamicTransform` entities (not `ManualTransform`), breadth first by depth, `ChildOf` and `Parent` hierarchies |
| `box.transform.static.Update` | PreStore | Dirty static entities and their subtrees, once per entity per frame |
| `dynamic.Spread`, `dynamic.SpreadToParented` | observers | Add `(IsA, DynamicTransformSpread)` to children / `Parent` children of dynamic entities |
| `On*` (mostly in `static`) | observers | Mark entities dirty on Position/Rotation/Scale set or remove, ChildOf/Parent change, enable, `DynamicTransform` add/remove; `OnManual` follows a set `WorldTransform` under `ManualTransform` |

## Rules and gotchas

- No `Position`/`Rotation`/`Scale` means no `WorldTransform`: the entity is not drawn, not indexed by
  `box.spatial`, not picked. A parent without a pose is skipped: children use the nearest ancestor
  with a `WorldTransform`.
- Static entities update only on events. A write through `ecs_get_mut` without `ecs_modified` is not
  seen. Use `ecs_set`, or `ecs_ensure` + `ecs_modified`, or make the entity dynamic.
- Changes made after PreStore reach `WorldTransform` in the next frame.
- `DynamicTransform` spreads: descendants inherit it via `(IsA, DynamicTransformSpread)`. A child
  that owns the tag (added by you, or implied by `Velocity` / `AngularVelocity`) stays dynamic when
  the parent stops being dynamic; inherited ones go back to static.
- `Animator` adds `DynamicTransform` to the entities whose Position/Rotation/Scale it animates.
- `ManualTransform`: static descendants follow when the app writes `WorldTransform` with `ecs_set`
  or pointer + `ecs_modified`; a silent write leaves them at the old transform. `box.spatial` layout
  uses an owned `ManualTransform` for placed entities; don't add your own to those.
- Disabled entities are not updated; when enabled again they recompute from current values.
- For spatial movers and index cost of `DynamicTransform`, see `docs/spatial.md`.

## Cost

- Static: zero work in frames where nothing changed; one subtree walk per changed root.
- Dynamic: every dynamic entity every frame, by cached queries grouped by hierarchy depth. Keep big
  static scenery out of dynamic subtrees.

## C API

| Function | Does |
| --- | --- |
| `box_transform_local(p, r, s, out)` | `T * R * S` exactly as the systems compose it (r, s may be NULL) |
| `box_transform_world_scale(&wt)` | Uniform scale: cube root of abs det of the 3x3; ~1 snaps to 1, 0 for flat, 1 if not finite |
| `box_transform_dynamic(world, table)` | Whether a table owns or inherits `DynamicTransform` |
| `box_transform_on_moved(world, cb, ctx, free)` | Callback with entities whose static transform changed (roots and written descendants, not `ManualTransform`, never dynamic ones); a later change arrives next frame |
| `box_transform_on_dynamic(world, cb, ctx, free)` | Observer callback for gaining/losing `DynamicTransform`, owned or inherited; one shared observer, cheaper than one per module |
