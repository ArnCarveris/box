# Particles

A GPU particle system with configurable emitters, forces, lifetime curves, appearances and playback controls, verified at ten million particles.

## Details

Particles are simulated in compute passes over renderer owned buffers; the game side only sends emitter parameters, so the packet stays small and the ECS does not hold per particle state. Every property the brief lists is covered: position, velocity, acceleration, age and lifetime, size, color, opacity and optional rotation, with emission rate, maximum count, emitter shape and spawn distribution, randomisation of direction, speed, size, color and lifetime, forces such as gravity, wind and drag, changes over lifetime, appearances from circles to sprite sheets, blending for smoke, fire, sparks and magic, continuous emission with bursts and start, stop, pause and reset controls.

Verification is unit tests over the deterministic simulation with a fixed seed and a fixed clock, visual scenes for each emitter shape and each classic effect, command based tests that drive playback, and the brief's benchmark of ten million particles at 60 FPS in release. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: ParticleEmitter, EmitterShape, the force components, the over lifetime curves, ParticleAppearance, Burst and EmitterState.

## References

- design/DESIGN.md, section Particles
- design/datamodel/particles.flecs, which this epic fills in
