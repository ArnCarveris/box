# Placement

Click, drag paint and rectangle placement with ghosts, reach and occupancy checks.

## Details

Port placement.c: the camera ray from box_picking_ray intersected with the ground plane, cells from
world XZ, single placement, drag painting lines, rectangles with shift fill, ghost previews with a
valid or blocked tint, reach and occupancy rules, and demolish painting.

Verification: Unit tests for the cell mapping and the paint shapes, a command based test placing a wall line and a
rectangle through mouse commands, a visual scene with ghosts.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/placement.c
- project/db/picking/deliverable/README.md
