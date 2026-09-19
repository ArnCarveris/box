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
