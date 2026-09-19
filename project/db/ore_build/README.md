# Ore Else building and power

Placement by click, drag paint and rectangles with reach checks, autotiling rules, every building's lifecycle and the power allocator.

## Details

Buildings sit on the grid the app owns: placement intersects the camera ray with the ground plane
and maps to cells, paints lines and rectangles with shift fill, checks reach and occupancy and shows
ghosts; the 2 by 1 adjacency rules give walls and wires their segment shapes; each building (drill,
wall, solar panel, factory, gun, missile and laser turrets, drone pad, pad beacon, rocket pad) is a
template with its C lifecycle; power is produced by solar panels and allocated all or nothing over
the mining, weapons and construction dials.

Verification: Command based playthrough tests placing and demolishing through the input commands, unit tests per
building and for the allocator, visual scenes per building family.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md

## Porting rule

Ore Else was built on an older, inferior engine, and this port is the showcase for how box does it
better. Do not carry the old engine's structure, workarounds or C where box has a declarative way:
prefer script templates, components and the command interface over C systems, keep gameplay logic
in C only where it is genuinely simulation (grids, economy, AI), and where the old game worked around
a missing feature, file the feature as a box task instead of porting the workaround. Every screen,
effect and building should read as an example of the box way.
