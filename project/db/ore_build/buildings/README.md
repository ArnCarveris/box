# Building lifecycle

Every building as a template with its C lifecycle: drill, wall, solar panel, factory, three turrets, drone pad, pad beacon and rocket pad.

## Details

Port buildings.c and buildings/*.flecs: the building registry, construction cost and progress,
health, the per building behaviour hooks (drill extracts from its deposit, factories craft, turrets
are armed by the combat epic, pads host drones and rockets), removal with refund, and the building
materials (wall plate, hazard, fan, vent, drone bay, solar cells).

Verification: Unit tests per building on a fixed clock, a visual scene per family, a command based test building a
small base.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/buildings.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings
