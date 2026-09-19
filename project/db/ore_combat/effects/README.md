# Particle effects

The two dozen particle effects as box emitters with shared pools and curves.

## Details

Port particles/*.flecs (muzzle flash, jet plume, weld sparks, boom burst, drill dust, vent steam,
missile trail and the rest) with pools.flecs and curves.flecs to ParticleEmitter, ParticleCurves,
ParticleAppearance and ParticleBurst, triggered from C through the playback and burst components.

Verification: A unit test that every effect spawns on a fixed clock, a visual scene with every effect frozen mid
burst.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/particles
- project/db/particles/deliverable/README.md
