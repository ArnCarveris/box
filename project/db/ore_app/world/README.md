# Map, deposits and scenery

The flat cell grid the game owns, ore deposits, the map boundary, props and flora.

## Details

Port game.c's map and state machine, deposits.flecs, boundary.flecs, scenery.flecs and props/*
(flora, lights, markers, ore) as templates on box shapes and materials, with the cell helpers
(oreCellAt and friends) as the module API every later epic uses; the state machine keeps title,
playing, paused and ended.

Verification: Unit tests for the grid helpers and deposit placement with a seed, a visual scene of the map with
deposits and scenery.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/game.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/deposits.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/scenery.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/props
