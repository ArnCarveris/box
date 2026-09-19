# Emitter and simulation

`box.particle` adds `ParticleEmitter`: maximum alive, rate, spawn shape with its parameters, the
ranges a particle starts between, a constant acceleration, a color and a seed, with defaults from a
constructor so `ParticleEmitter: {rate: 400}` is already a stream. No particle reaches the ECS.
Extract writes one `box_particle_emitter_t` per emitter with the world transform that places the
spawn volume; the mesh and spline shapes add a table of 64 points sampled from the `Mesh3` or
`Spline` of the source entity, resampled when the shape, source or seed changes. The spawn count
comes from an accumulator on the fixed clock and is capped by the pool. The renderer owns a pool
per emitter: one buffer per channel (position with the age, velocity with
the lifetime, acceleration with the rotation, color with the opacity, size), a dead list and
counters. An integrate and a spawn compute pass run over them once per frame and a point sprite
pass draws every pool over each view after its sorted draws. The dead list enforces the maximum and
every random draw is a hash of the seed and the index of the particle in the stream, so a fixed
timestep gives the same particles on every run. Two shader entities under `box.particle.shaders`
carry the WGSL, so a world without an emitter compiles nothing.

Verified: 571 tests (21 new), lint 0, 82 scenes match with `particles_shapes.png` new, a million
particles at 0.50 ms. Left out: forces, curves, sprites, playback and indirect dispatch.
