# Spatial index and picking

`box.spatial` keeps a bounding volume hierarchy per scene over the world bounds of shapes and of
UI elements without a shape. Picking (`box.picking`), the move controllers, design mode and the
change regions of a light bake all read it. A static shape is placed again only when it changes.
A shape with `DynamicTransform` is refit in every frame that the index is queried, and picking
queries it in every frame that a cursor is known. So every moving shape in the index costs
`SpatialUpdate` time in every frame, whether or not any query can use it.

## SpatialIgnore

`SpatialIgnore` (a tag in `box.spatial`) keeps an entity and all of its descendants out of the
index. Such an entity has no item and no `SpatialPose`, so it costs the index nothing per
frame. The index's per frame work does not grow with the number of these shapes.

```flecs
using box.spatial

prefab Critter {
  SpatialIgnore
  body {
    Box: {1, 1, 1}
  }
  head {
    Position: {0, 1, 0}
    Box: {0.5, 0.5, 0.5}
  }
}
```

In C, a game can put it on every entity of one kind with
`ecs_add_pair(world, ecs_id(MyEnemy), EcsWith, SpatialIgnore)`.

The rule is explicit and has no exceptions: what the index does not hold, no query finds.

- Picking: the entities are never hit. They get no `Hovered`, `Pressed` or pointer events, are
  never `PickStatus.hit`, and they no longer block the pick ray, so a pick goes through them to
  what is behind. This also holds for an `Interactive` entity, or one with an `ActionTrigger`,
  under `SpatialIgnore`.
- Move controllers neither collide with them nor stand on them.
- `box_spatial_bounds`, design mode framing and the change regions of a light bake do not see
  them. Keep the tag off static shapes that are baked.

The tag can be on the entity or on any `ChildOf` ancestor. Adding or removing it (on the entity
or an ancestor) takes effect in the next `SpatialUpdate`. The subtree is then indexed again at
its current place, or taken out.

## When to use it

Put it on actors that move every frame and that nothing picks, collides with or queries, such
as the enemies, projectiles, corpses and drones of a game (Ore Else does this). Leave it off
anything the player clicks or hovers, anything a move controller must collide with, and anything
that should hide what is behind it from a pick.

Without the tag, moving shapes behave as before: they are refit while they move, they are
picked at their current place, and they block picks of what is behind them.
