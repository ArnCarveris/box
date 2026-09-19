# Rendered bounds reach the collider

## Built

`box_render_update_aabb` writes `AABB` through `ecs_get_mut` or `ecs_ensure` and
never raised the OnSet event the collision module listens to, so a shape without
a cheap collider (a cone, a cylinder, a torus, a custom mesh) got no `Collider`
at all outside tests that set the bounds by hand. The update now ends with
`ecs_modified`, so the bounds collider, the root aggregation and the index see
every bounds the renderer writes.

## Verified

`Colliders.rendered_bounds_derive_aabb` renders a cylinder headless and checks
the collider the bounds produce; it failed before the fix with no collider and
passes after it, 89 cases in `test/collision`. `collision_debug.png` now shows
the cone in its bounds box and is the refreshed baseline. `bench/render`
`Extract` in release: `dynamic_10k` 231.5 to 229.3 µs, the static cases within
noise, the event is cheap next to the instance write.

## Left out

Nothing.
