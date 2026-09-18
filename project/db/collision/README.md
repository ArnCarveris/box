# Collision

Prefab root bounds, cheap colliders, an incrementally updated two level spatial index, and controllers that collide with the scene.

## Details

Collision starts with bounds: every renderable already has a world AABB, and this epic aggregates them up to prefab and template roots so a hierarchy can be tested as one object. Colliders are derived from the shape where possible, so a sphere gets a sphere collider and a box an oriented box, with an explicit override and an opt out. A two level spatial index over roots and their colliders is updated incrementally by the same observers that maintain bounds, and it is what makes picking and controller collision independent of the object count. Controllers sweep a capsule against that index with sliding resolution, a step up for small obstacles and a ground probe, so no controller walks through the scene and no system walks all objects.

Verification is unit tests that compare index queries against a brute force reference on randomised scenes, unit tests for wall sliding, step up, falling and disabling collision, and a debug visual scene that draws colliders as wireframes over the primitives scene. The brief's verification is a benchmark that navigates a scene with a million objects and must hold 60 FPS, kept as the deliverable report. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: RootBounds, the collider components with NoCollider, and the spatial index settings.

## References

- design/DESIGN.md, sections Interaction and Controllers (collision)
- design/datamodel/transform.flecs (AABB)
