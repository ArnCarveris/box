# Collision: completion report

## What was built
- `box.collision` with `RootBounds`: prefab and template instances become roots at instantiation, observers on `AABB`, both parent storages and the world transform mark the root a change belongs to, and one flush in `OnStore` walks only changed roots; a `RigidSubtree` root transforms its cached local bounds instead of aggregating.
- `Collider` in world space with the instance scale applied, derived from the shape (`Sphere` and `IcoSphere` a ball, `Box` and `RoundedBox` an oriented box, `Capsule` a capsule, everything else its bounds), overridden by `SphereCollider`, `BoxCollider` and `CapsuleCollider`, opted out with `NoCollider`; ray, sphere and capsule tests are public in `box/collision.h`.
- A two level index: a dynamic bounding volume hierarchy over root bounds and loose colliders, a per root collider list rebuilt only when its subtree changes, a second hierarchy for entities that move every frame. `box_collision_ray`, `_sphere`, `_aabb` and `_frustum` answer queries without walking the scene; `CollisionSettings` holds the leaf margin and the switches.
- `box_collision_move` sweeps a capsule through the index in half radius steps, slides along the deepest contact, retries a blocked move over a ledge up to `step` and finds ground with a ray; `FirstPersonController` (`radius`, `step`, `foot`) and `CameraController` (`radius`) move through it, `no_collision` disables it and `terrain` stays a fallback floor.
- DEBUG_PLACEHOLDER

## Verification
- TESTS_PLACEHOLDER
- Scenes in `integration/`: `controller_collision` walks the player into walls and up steps, `collision_debug` shows the wireframes, `first_person_1m.json` records a million nested colliders under 50k template roots at 0.90 ms a frame; every earlier scene bit identical.
- Benchmarks in `bench/` against the meshes epic: a ray query over a million colliders costs 7 µs against the one millisecond budget, the sphere query 0.5 µs, the frustum query 2.9 µs, a frame with 1000 movers 494 µs and the controller sweep 8.6 µs. A hundred controllers went from 12 to 17 µs a frame, the cost of the capsule sweep each now runs.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Marking the owning root from observers and flushing once in `OnStore` is what keeps 100k idle roots at 14 µs; per entity work would have been the frame budget.
- Randomised scenes checked against a brute force reference caught more index bugs than the hand written cases.
- The renderer aborts above roughly 250k drawn instances (`culling/instance_cap` tracks it), and the bench app cannot hold a key, so the million object scene walks with `Velocity`.

## Not in this epic
- Bounds come from the `AABB` the renderer writes, so a headless world without `box.render` sees only the bounds a scene sets itself.
- Queries read what the last update left behind; `box_collision_update` sees a change of the running frame.
- Picking and script events over the index are the picking epic.
