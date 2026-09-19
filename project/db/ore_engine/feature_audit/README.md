# Feature audit

A table from every flecs-engine component and call Ore Else uses to its box equivalent, with a task filed for every gap.

## Details

Walk the survey (project/db/ore_engine/README.md and the flecs-engine project under
/Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else): every
component in the scripts, every flecsEngine_* call in the C sources, every script construct. For
each row name the box component or function, note a mapping rule (Position3 to Position, Rgba to Rgb
plus Opacity, RenderView effects to the effect chain, surface effects to material layers, SSAO to
GTAO, Tony to the tonemap operator, Icon to Image, ui2dRect to a UI tree, cameraScreenRay plus
rayPlaneY to box_picking_ray plus a new plane helper in box/math.h which this task adds) and mark
the gaps; file a task in this epic for every gap not already listed and keep the table as
deliverable/NOTES.md for the porting agents.

Verification: The table covers every row of the survey, the plane helper has a unit test, and pm check is clean
with the filed tasks.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc
