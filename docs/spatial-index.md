# Spatial index and picking

`box.spatial` maintains a bounding volume hierarchy per scene over shapes and
shapeless UI elements with `WorldTransform`. Picking, move controllers, design
mode and light-bake change regions query it. Without Position, Rotation or Scale,
an entity has no world transform and cannot be drawn or picked.

Static shapes update when changed. Shapes with `DynamicTransform` are indexed
under their mover, the nearest entity that owns the tag, as one leaf with the
bounds of all shapes under it. A mover places its leaf again in every frame it
moves and the index is queried. A `Position`, `Rotation` or `Scale` set under a
mover bounds its shapes again; a write that does not notify (`ecs_get_mut`
without `ecs_modified`) is not seen, so give an entity that moves that way its own
`DynamicTransform`. An `Animator` does this for the entities it moves. Queries
answer with the mover; picking and move controllers test the shapes under it one
by one. Picking queries every frame while a cursor is known.

## Ignore

`Ignore` (`SpatialIgnore` in C) excludes an entity and all its `ChildOf` descendants from the index.
Excluded shapes have no index item or `SpatialPose` and add no per-frame index work.

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

In C, make it automatic for a component/tag with
`ecs_add_pair(world, ecs_id(MyEnemy), EcsWith, SpatialIgnore)`.
Adding or removing the tag on an entity or ancestor takes effect in the next
`Update` system of `box.spatial`, excluding or rebuilding the subtree at its current position.

Exclusion applies to every query:

- Picking passes through the shapes. They receive no hover, press or pointer
  events, even with `Interactive` or `ActionTrigger`, and never become `PickStatus.hit`.
- Move controllers neither collide with them nor stand on them.
- `box_spatial_bounds`, design framing and light-bake change regions do not see them.
  Keep the tag off static shapes that are baked.

Use it for moving actors that nothing needs to pick, collide with or query, such
as projectiles, corpses or drones. Leave it off clickable objects, collision
surfaces and shapes that must block picks. Untagged moving shapes remain indexed
at their current positions.
