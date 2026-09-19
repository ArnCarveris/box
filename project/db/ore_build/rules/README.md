# Autotiling rules

The 2 by 1 adjacency rules that give walls and wires their segment shapes.

## Details

Port rules.c and buildings/rules.flecs: a rule component naming the shapes for each neighbour
pattern, evaluated when a cell or its neighbours change, so walls become straight, corner, tee and
cross segments.

Verification: Unit tests for every neighbour pattern and for updates on placement and demolition, a visual scene
of a wall ring.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/rules.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings/rules.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings/wall_segment.flecs
