# Projectiles and turrets

Tracers, homing missiles, laser beams and globs, and the turrets that target and fire them.

## Details

Port projectiles/*.flecs and the turret targeting of combat.c: target choice in range, fire rate
under power, tracers with hit scan, missiles homing with a trail, laser beams with continuous
damage, spitter globs, and the pistol of the player.

Verification: Unit tests for targeting and each projectile, a command based test of a turret ring against a wave,
a visual scene per projectile.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/combat.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/projectiles
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings/gun_turret.flecs
