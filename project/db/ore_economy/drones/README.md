# Repair drones

Drones that fly from pads to damaged buildings, repair with iron and return.

## Details

Port drones.c and buildings/drone.flecs: the pad's drone bay, the flight state machine (idle,
launch, travel, orbit, repair, return, dock), iron consumption, target choice by damage and
distance, and the drone template with its jet plume effect.

Verification: Unit tests of the state machine on a fixed clock, a visual scene of drones repairing a wall.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/drones.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings/drone.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings/drone_pad.flecs
