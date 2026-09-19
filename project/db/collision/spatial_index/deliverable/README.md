# Spatial index

## Built

A two level index in `box.collision`. The first level is a dynamic bounding
volume hierarchy over the bounds of every root and over the colliders that are
not below one, maintained incrementally by the same flush that keeps bounds and
colliders. The second level is the collider list of a root, rebuilt only when
its subtree changes. Entities that move every frame live in a second hierarchy
the flush rebuilds from a cached query. `box_collision_ray`, `_sphere`, `_aabb`
and `_frustum` answer queries without walking the scene; `CollisionSettings`
holds the leaf margin and the two switches.

## Verified

`test/collision` suite `Index`, 20 cases: insert, remove, move, roots, nested
roots, movers, the disable switch, and four randomised scenes checked against a
brute force reference. `bench/collision` suite `Index` on 50k roots with 20
children each: the ray query costs 7 us against the one millisecond budget, the
sphere query 0.5 us, the frustum query 2.9 us, the cold build 1.29 s and a frame
with 1000 movers 494 us.

## Left out

Queries read what the last update left behind; call `box_collision_update` to
see a change of the running frame.
