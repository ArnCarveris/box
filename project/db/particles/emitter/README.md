# Emitter and simulation

An emitter component that spawns and integrates particles on the GPU with a configurable rate, shape and maximum count.

## Details

ParticleEmitter holds the maximum particle count, the emission rate, the spawn shape and distribution, point, sphere, box, disc, mesh surface or spline, and the initial particle state. Particles live in renderer owned buffers and are spawned and integrated in compute passes, so the frame packet only carries emitter parameters. Each particle carries position, velocity, acceleration, age, lifetime, size, color, opacity and optional rotation. The emitter entity's transform places and orients the spawn volume like any other entity.

Verification: unit tests cover the component defaults, the extracted emitter block, the spawn count over time on a fixed clock and the maximum count clamp. A visual scene captures a steady stream from each emitter shape, and a benchmark measures the simulation cost of one million particles.
