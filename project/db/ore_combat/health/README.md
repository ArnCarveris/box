# Health and damage

Health, damage, ranges and attack intervals shared by buildings, critters and the player.

## Details

Port the shared combat components from combat.c (health, damage, range, attack interval, death) with
the events other systems listen to, and the player death ending the game.

Verification: Unit tests for damage, death and the interval.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/combat.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/include/ore_else/combat.h
