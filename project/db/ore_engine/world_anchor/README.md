# World anchored UI elements

A UI element that follows the screen projection of a world entity or position through a view, for health bars and labels.

## Details

Ore Else draws health bars as immediate mode rectangles projected by hand. Add WorldAnchor {entity,
offset, view} to box.ui: the layout pass projects the world position through the view's camera into
the logical pixels of the UI view every frame the target moved, hides the element when it is behind
the camera or outside the view, and keeps Layout working on its children.

Verification: Unit tests for the projection, the hide cases and a moving target, a visual scene with bars over
moving boxes, every other scene unchanged.

## References

- project/db/ui/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/health_bars.c
