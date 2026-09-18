# Picking and script events

Ray picking that resolves to the right entity and raises bubbling script events such as on.click and on.drag.

## Details

A pick is a ray from a view's camera through a screen position, resolved through the spatial index to the nearest hit and then to the entity the author cares about, which is the prefab root unless a descendant opts in. Hits raise events through the flecs script event module so a script can await on.drag(this), and events bubble from the hit entity up the hierarchy until a handler consumes them. Hover, press, release, click and drag are derived from the input state, so the whole interaction is data driven.

Verification: unit tests cover ray construction from a view, nearest hit resolution, root versus descendant targeting, bubbling order and consumption. A command based integration test moves the mouse and clicks objects through the input commands and keeps the log, and a visual scene highlights the hovered object.
