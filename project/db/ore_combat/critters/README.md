# Critters and waves

Five critter kinds with their AI and the wave director that scales forever.

## Details

Port the critter templates (mite, skitter, brute, behemoth, spitter), their movement and attack AI
against the base, and the director spawning waves from the map edges on the game clock with the
scaling rule.

Verification: Unit tests for the director schedule and the AI decisions on a fixed clock, a visual scene of a
wave.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/combat.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/critters
