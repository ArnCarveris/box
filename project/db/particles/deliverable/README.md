# Particles: completion report

## What was built
- `box.particle`: `ParticleEmitter` (maximum alive, rate, spawn shape with its parameters for point, sphere, box, disc, mesh surface and spline, start ranges, acceleration, color, seed) extracted as one emitter block per frame with the world transform placing the spawn volume; no particle reaches the ECS. The renderer owns a pool per emitter, one buffer per channel with a dead list and a compacted alive list, and runs an integrate and a spawn compute pass plus a sprite pass per view; every random draw is a hash of the seed and the stream index and slots are taken in order, so a fixed timestep renders the same image on every run.
- `ParticleForces` (gravity, wind, drag, turbulence, summed from the world, a named entity and the emitter), per particle randomisation of direction cone, speed, lifetime, size, rotation, opacity and color, and `ParticleCollision` against a ground plane and up to sixteen colliders pulled from the collision index, with bounce, friction or death.
- `ParticleCurves` (four key size, opacity, rotation and color over life, read in the draw pass from age and lifetime) and `ParticleAppearance` (soft circle, sprite from a texture entity, `Image` or `PbrTextures` with sheet frames, or a mesh billboard), alpha and additive pipelines after the glass pass sorted back to front by emitter, and a depth soft fade.
- `ParticlePlayback` (playing, paused, stopped, engine written clock, reset) and `ParticleBurst` schedules evaluated from the clock so a burst lands on the same frame every run; `particle.start`, `pause`, `stop`, `reset` and `time` as script functions.
- Scale: 52 bytes per particle with half float color, opacity, rotation and size, dispatch over used slots only, an indirect compacted draw, four vertex strip quads, `ParticleSettings` budgets shared in proportion to the asks, frustum and distance culling and half rate stepping past a distance, and `ParticleStats`.
- RAIN_PLACEHOLDER

## Verification
- TESTS_PLACEHOLDER
- Scenes in `integration/`: `particles_shapes` (a stream from each spawn shape), `particles_forces` (a fountain under gravity and wind with bounces), `particles_appearance` (one emitter per appearance and blend), `particles_control` (a burst frozen mid flight, with its command log driving every state), RAIN_SCENES_PLACEHOLDER; every earlier scene bit identical. `particles_10m.json` records ten million particles over ten emitters at 10.5 ms mean and 17.1 ms p99 in release, 95 fps against the 60 the brief asks for, and `particles_fill.json` 11200 screen filling soft sprites at 10.7 ms.
- Benchmarks in `bench/` against the LOD epic: a million particles simulate and draw in 0.49 ms, the extract costs 240 ns per emitter with budgets, forces, curves, playback and four schedules, and the C twin of the shaders in `src/particle/simulate.h` is what the integrator, curve and collision tests check.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Hashing randomness from the seed and stream index and taking slots in order is what made a GPU particle system reproducible enough for baselines; before that, particle baselines only held at low sprite opacity.
- The sim pass binds the eight storage buffer ceiling, so colliders and spawn points share one table and later channels pack.
- Four vertex strip quads instead of six vertices was where most of the ten million particle frame went.

## Not in this epic
- Colliders other than spheres and boxes, packing position and velocity (which would move existing baselines), and a second phase of the control command scene.
- NOT_PLACEHOLDER
