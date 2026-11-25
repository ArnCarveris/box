# Spatial

`box.spatial` (`BoxSpatial`, `include/box/spatial.h`) does two things: a per scene spatial index
(BVHs over the world bounds of shapes and shapeless UI elements, used by picking, move controllers,
design framing and bake change regions) and a declarative layout solver (`Fit`, `Align`, `Array`,
`Distribute`). C names carry a `Spatial` prefix that script names drop (`SpatialIgnore` is `Ignore`,
`SpatialAlign` is `Align`, `SpatialDiagnostic` is `Diagnostic`, `SpatialAnchor` is `Anchor`).

## Systems

| System | Phase | Does |
| --- | --- | --- |
| `box.spatial.Resolve` | PostUpdate | Solves layout rules whose inputs changed (before transforms) |
| `box.spatial.Update` | OnStore | Brings the index up to date (after transforms, which run in PreStore) |

## Index

- One index per `Scene` (nearest ancestor with `Scene`, else the default scene, `0`). Three trees
  per scene: static (exact leaves), dynamic (fat leaves, one per mover), UI (`LayoutBounds`, else
  `TextBounds`, flat in the XY plane of the `WorldTransform`).
- Indexed: entities with a `ShapeMesh`, `LayoutBounds` or `TextBounds` (these add the private
  `SpatialProxy`) **and** a `WorldTransform`. No Position/Rotation/Scale means no `WorldTransform`:
  not drawn, not indexed, not picked.
- Not indexed: prefabs, `Ignore` subtrees. Disabled entities stay indexed but every query skips them
  at once (no wait for the next update).
- Bounds = unit mesh bounds times the shape's instance scale, placed by `WorldTransform`.
- Static shapes are re-bounded only when their `WorldTransform` changes (observers + static
  transform callbacks), so an unchanged frame costs nothing.

### Movers (`DynamicTransform`)

- A mover is an entity that owns `DynamicTransform` (or has it via a prefab, not via the
  `DynamicTransformSpread` inheritance its descendants get). Every shape with `DynamicTransform`
  is indexed under its nearest mover (itself or an ancestor).
- A mover is **one** dynamic leaf over the bounds of all shapes under it, held in the mover's frame.
  When the mover moves, only that leaf moves; 1 leaf however many shapes it holds.
- Fat leaves reach ahead along the last displacement (margin 0.1 + 10% of extent, predicted 4
  updates ahead), so a mover is reinserted only after it leaves its fat box.
- A `Position`, `Rotation` or `Scale` **set** (OnSet) on an entity under a mover re-bounds the
  mover's shapes. A write without an event (`ecs_get_mut` with no `ecs_modified`) is not seen: give
  an entity that moves that way its own `DynamicTransform`.
- `Animator` adds `DynamicTransform` to entities whose Position/Rotation/Scale it animates;
  `Velocity` and `AngularVelocity` imply it.
- Queries answer with the mover entity. Picking and move controllers then test the shapes under it
  (`box_spatial_ray` with `.shapes = true`, `box_spatial_expand_box`). A mover with 8 or more
  shapes keeps a BVH over their bounds in its frame, built by the first query after a refit (a
  refit of a built tree moves the changed shapes, or drops the tree when many changed or shapes
  were compacted). Rays walk it nearest first through the mover's world transform, so a filter that
  shortens the reach skips the rest; each shape reached is still tested by its world bounds. Moving
  the mover costs the tree nothing. Smaller movers test their shapes one by one.
- Movers, moved leaves and dynamic UI elements are placed lazily by the first query of a frame, or
  eagerly by `Update` when the previous frame queried. Picking queries every frame while a cursor
  is known.

## Ignore

`Ignore` (`SpatialIgnore`) keeps an entity and all its `ChildOf` descendants out of the index: no
index item, no `SpatialPose`, zero per frame index work.

```flecs
using box.spatial

prefab Critter {
  Ignore
  body {
    Position: {0, 0, 0}
    Box: {1, 1, 1}
  }
}
```

C, automatic for every entity with a component: `ecs_add_pair(world, ecs_id(MyEnemy), EcsWith, SpatialIgnore)`.

- Adding/removing it (on the entity or an ancestor) takes effect in the next `box.spatial.Update`,
  which excludes or rebuilds the subtree at its current position.
- No query sees ignored shapes: picks pass through them (no hover, press or pointer events even with
  `Interactive` or `ActionTrigger`, never `PickStatus.hit`, they don't block what's behind), move
  controllers neither collide with nor stand on them, `box_spatial_bounds`, design framing and light
  bake change regions skip them. Keep it off static shapes that are baked.
- Use for actors nothing needs to pick, collide with or query (projectiles, corpses, drones,
  tooltips). Leave it off clickable objects, collision surfaces and pick blockers.

## C queries (`src/spatial/spatial.h`, for box modules)

| Function | Does |
| --- | --- |
| `box_spatial_ray(world, &ray, &hit)` | Nearest hit; `box_spatial_ray_t {scene, origin, dir, max_distance, filter, ctx, ui, shapes}` |
| `box_spatial_ray_all(world, &ray, &vec)` | All hits, nearest first (`box_spatial_hit_t {entity, distance}`) |
| `box_spatial_overlap_box(world, scene, min, max, &vec)` | Entities whose bounds overlap a box |
| `box_spatial_overlap_sphere(world, scene, c, r, &vec)` | Same for a sphere |
| `box_spatial_expand_box(world, e, min, max, &vec)` | Shapes under a mover that overlap the box (or `e` itself) |
| `box_spatial_bounds(world, e, &scene, min, max)` | Bounds the index holds; false if not indexed |
| `box_spatial_static_bounds(world, scene, min, max)` | Box around all static shapes of a scene |
| `box_spatial_tree_walk(world, scene, tree, visit, ctx)` | Walk `BOX_SPATIAL_STATIC` / `_DYNAMIC` / `_UI` |
| `box_spatial_update(world)` / `box_spatial_rebuild(world)` | Update now / rebuild static trees |
| `box_spatial_stats(world, &s)` | `scenes, entities, movers, updated, rebound, moved, visited` |
| `box_spatial_layout_stats(world, &s)` | `nodes, solved, synced` |

- `filter` is the narrow phase: return false to reject, may push `*distance` further.
- `ui = true` also hits shapeless UI elements; overlap queries never see them.
- Debug draw the trees: `View: {debug: {bvh: {static_tree: true, dynamic_tree: true, ui_tree: true, mover_trees: true}}}`
  (static green, dynamic orange, UI magenta, the local tree of each mover cyan and oriented by
  the mover; see `docs/view.md`).

## Layout rules

Run it: `bake3 run apps/boxrun --local-env -- --design --surface 1280x720 etc/scenes/spatial_layout.flecs`

```flecs
using box.geometry
using box.spatial

hull {
  Box: {8, 3, 18}
  docking_port { AttachmentFrame: {position: {4, 0, 0}, rotation: {0, 1.5707963, 0}} }
}
armor {
  Box: {1, 0.2, 1}
  Fit: {target: hull, axes: XZ, padding: {0.4, 0, 0.8}, mode: Stretch}   // -> {7.2, 0.2, 16.4}
  Align: {target: hull, self: {y: Min}, other: {y: Max}}
}
dock {
  Box: {1, 1, 0.5}
  Align: {target: hull, self: Back, other: {frame: hull.docking_port}, rotation: Match}
}
vents {
  Array: {axis: Z, count: 8, spacing: 0.7}
  rib { Box: {0.7, 0.3, 0.15} }
}
sensors {
  Distribute: {axis: X, span: 6, mode: SpaceBetween}
  left { Box: {0.5, 0.5, 0.5} }
  right { Box: {0.5, 0.5, 0.5} }
}
```

| Component | Fields | Notes |
| --- | --- | --- |
| `Fit` | `target, axes, padding, mode` | Resizes geometry (not `Scale`) to the target's bounds. `axes` 0 = `XYZ`. `Stretch` (default), `Contain` (smallest ratio), `Cover` (largest); uniform ratio applies to selected axes only. Padding removed on both sides. Fitted entity needs geometry; target may be a group |
| `Align` | `target, self, other, rotation, offset, rotation_offset` | Anchor `{x, y, z}` each `Center` (default) / `Min` / `Max`; or `face` (`Front` +Z, `Back` -Z, `Top` +Y, `Bottom` -Y, `Right` +X, `Left` -X, also usable bare: `self: Back`); or `frame` (child with `AttachmentFrame`). `rotation`: `Keep` (default) or `Match` (faces point at each other). `offset` in world units in the target anchor frame (X tangent, Y up, Z outward) |
| `Array` | `axis, count, spacing, rotation_step, scale_step` | Every direct child is a prototype, copied `count` times (0..100000; 0 hides prototypes). Spacing is center to center; index 0 at the authored position. Step i adds `i * rotation_step`, scales by `1 + i * scale_step` (must stay positive) |
| `Distribute` | `axis, span, mode, gap` | Children (enabled, non-prefab, not `AttachmentFrame`) in ascending entity id order, span centered on the container. `SpaceBetween` (default), `SpaceAround`, `SpaceEvenly`, `FixedGap` (uses `gap`; span 0 = unconstrained) |
| `AttachmentFrame` | `position, rotation` | Named frame in the owner's geometry coordinates; contributes no bounds |
| `IntrinsicBounds` / `ResolvedBounds` | `min, max` | Geometry local bounds before / after layout (output) |
| `ResolvedBox` | `x, y, z` | Computed Box size after Fit (output) |
| `ArrayInstance` | `owner, prototype, index` | On generated instances (output) |
| `Diagnostic` | `code, related` | Error: `InvalidTarget`, `MissingBounds`, `ZeroBounds`, `Cycle`, `Impossible`, `InvalidAnchor`, `ConflictingLayout` |

### Layout rules and gotchas

- Axes enum: `X Y Z XY XZ YZ XYZ`. `Array` and `Distribute` need a single axis.
- Authored `Position`/`Rotation`/`Scale`/geometry stay intact: placed entities get `WorldTransform`
  plus an owned `ManualTransform`; Fit writes the resolved size to the mesh. Edit authored
  components, never the computed ones. Removing a rule restores the authored result.
- An entity with an app owned `ManualTransform` or `SurfaceAttachment` cannot be placed by layout.
- One placement driver per child: Align cannot override a position from `Distribute` or `Array`
  (`ConflictingLayout`). Fit combines with anything. A whole Array/Distribute group can be aligned.
- Reference bounds: an entity with geometry uses its own shape (children excluded); without geometry
  it aggregates its children. A child fitting its own group's resolved bounds is a `Cycle`.
- `self.frame` must belong to the aligned entity, `other.frame` to the target. Bounding anchors need
  geometry bounds; frames work without geometry. Rotations in radians, Y-X-Z Euler order.
- Array makes prototypes prefabs while it exists; surviving indices keep their entity ids when
  count/spacing change. Names starting with `__array_` and `__prototype_` are reserved.
- Span smaller than the children need is `Impossible`. Failed entities fall back to authored output;
  diagnostics log on change and clear when fixed; independent layouts keep solving.
- Incremental: only changed inputs and their dependents solve again; unchanged frames do no layout
  work. Dependencies on `DynamicTransform` entities solve every frame.
- C: `box_spatial_layout_update(world)` solves immediately (false if a rule failed); use
  `ecs_progress` to also update transforms and the index.
