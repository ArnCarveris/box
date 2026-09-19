# Ore Else combat

Critters, the wave director, projectiles, turret targeting, health and damage, world anchored health bars and the particle effects.

## Details

Five critter kinds with their AI and stats spawn in waves from the map edges that scale forever;
turrets pick targets in range and fire tracers, homing missiles, laser beams and globs; health and
damage apply to buildings, critters and the player, health bars follow their owners on screen
through the world anchored UI, and two dozen particle effects (muzzle flash, weld sparks, drill
dust, vent steam, missile trails, bursts) are ported to box emitters.

Verification: Unit tests for the director, targeting, projectile hits and damage on a fixed clock, command based
tests running a wave against a turret ring, visual scenes for the critters, the projectiles and the
effects.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md
