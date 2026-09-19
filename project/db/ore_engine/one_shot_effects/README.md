# One shot effects at a world point

Play an effect template once at a world position from a shared pool and let it clean itself up, the box answer to flecsEngine_particlesBurst plus the game's hand rolled fading flash light.

## Details

Ore Else fires two dozen effects from C: a muzzle flash, a mining burst, an explosion, a weld spark.
Each is `flecsEngine_particlesBurst(world, pool, at, burst)`, a burst descriptor spawned into an
emitter pool that outlives it, scaled per call, next to an entity with a `PointLight` the game fades
and deletes itself. Box has `ParticleBurst` as a schedule on an emitter and `ParticlePlayback`, but
nothing that plays an effect once at a point and goes away, so the port would either churn entities
per hit or copy the imperative API. Give `box.particle` a one shot: an effect is an ordinary template
with its emitter, its curves, its bursts and whatever else it holds (a light, a decal), instantiated
at a position through a component or a script function, played once and removed when its last
particle dies, with a pool of instances per template reused instead of created so a hundred hits a
second cost nothing. A scale on the play so one template covers a small and a large explosion, and a
`Lifetime` (or the same removal rule) for the entities of an effect that are not particles, which is
what the game's fading flash light is.

Verification: Unit tests on a fixed clock for the play, the reuse of a pooled instance, the scale and the removal
after the last particle, a visual scene with several effects frozen mid burst, every other scene
unchanged.

## References

- project/db/particles/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/particles
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/game.c
