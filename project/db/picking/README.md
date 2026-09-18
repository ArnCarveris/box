# Picking

Script driven picking that resolves a screen position to the entity the author cares about and raises bubbling events, verified on a million objects.

## Details

A pick is a ray from a view's camera through a screen position, resolved through the spatial index to the nearest hit and then to the entity the author cares about, which is the prefab root unless a descendant opts in. Hits raise events through the flecs script event module so a script can await on.drag(this), and events bubble from the hit entity up the hierarchy until a handler consumes them. Hover, press, release, click and drag are derived from the input state, so the whole interaction is data driven.

Verification is unit tests that compare picks against a brute force reference, a visual scene that highlights the hovered object, command based integration tests that move the mouse and click objects through the input commands, and the brief's benchmark of a pick under one millisecond in a scene of a million nested objects. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: the picking events and their opt in for descendants.

## References

- design/DESIGN.md, section Interaction
- design/datamodel/transform.flecs (AABB)
