# Controller collision

## Built

`box_collision_move` reads the colliders of the cells its capsule can reach from
the index once, walks the motion in steps of half a radius and pushes the capsule
out of the deepest contact after every step, which slides it along a wall. A move
a wall stopped is retried over a ledge of at most the step height and the ground
is a ray down the same colliders. `FirstPersonController` walks with it (`radius`
0.35, `step` 0.45, the eye as the capsule, `foot` as new state) and
`CameraController` keeps a sphere of `radius` 0.5 out of geometry; `no_collision`
switches it off per controller and `terrain` stays the fallback floor.

## Verified

`test/controller` 35 cases, 8 new: wall stop, wall slide, step up, standing on
geometry, falling off it and disabling collision on both controllers;
`test/collision` 80 cases still pass, linter clean. `walk_through_1m_colliders`
costs 8.6 us a frame on 50k roots of 20 colliders, and `first_person_1m.flecs`,
a million nested colliders under 50k template roots, walks the controller at
0.90 ms and 1110 fps. `controller_collision.png` is the baselined capture.

## Left out

The renderer aborts above roughly 250k drawn instances, so the million objects of
the bench scene are collider boxes under 50k drawn towers, and it walks the
player with a `Velocity` because the benchmark app cannot hold a key down.
