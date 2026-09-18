# Interaction and picking

Prefab root bounds, cheap colliders, an incremental spatial index and script driven picking with bubbling, verified on a million objects.

## Details

Interaction starts with bounds: every renderable already has a world AABB, and this epic aggregates them up to prefab and template roots so a hierarchy can be tested as one object. Colliders are derived from the shape where possible, so a sphere gets a sphere collider and a box an oriented box, with an explicit override and an opt out. A two level spatial index over roots and their colliders is updated incrementally by the same observers that maintain bounds, and it is what makes picking and controller collision independent of the object count.

Picking casts a ray from a view through a screen position, resolves the hit to the entity the author cares about and raises events through the flecs script event module so a script can await on.drag(this), with bubbling up the hierarchy until a handler consumes the event. Verification is unit tests against a brute force reference, a visual scene that highlights hovered objects, command based tests that click through the input commands, and the brief's benchmark of a pick under one millisecond in a scene of a million nested objects. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: RootBounds, the collider components with NoCollider, the spatial index settings and the picking events and their opt in for descendants.

## References

- design/DESIGN.md, section Interaction
- design/datamodel/transform.flecs (AABB)
