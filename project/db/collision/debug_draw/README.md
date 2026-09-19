# Collider debug drawing

A CollisionDebug setting that draws collider and root bounds wireframes through the renderer, with a visual scene.

## Details

The colliders task derives shapes and the index keeps bounds, but nothing shows them. `CollisionSettings.debug` (none, colliders, bounds, both) makes extract emit line segments for every collider and root bounds in view into a per frame line list the renderer draws after the scene with depth test on, colored by kind, at a cost that is zero when off. The same line list is the debug drawing primitive later epics can reuse.

Verification: a visual scene with primitives and a prefab tree showing sphere, box, capsule and bounds wireframes, baselined; an extract test for the line count; every other scene unchanged.

## References

- `project/db/collision/colliders/deliverable/README.md`, `src/collision/`, `src/renderer/`
