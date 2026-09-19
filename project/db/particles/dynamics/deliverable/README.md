# Forces, randomisation and collision

`ParticleForces` gives an emitter gravity, wind, drag and turbulence. An emitter adds the
component of the world, the one of the entity its `forces` member names and the one it carries
itself, so a scene can blow one wind over every emitter while an emitter keeps a drag of its own.
The forces ride in the emitter block instead of in the particles, so a force that changes bends
the particles that are already in the air; the integrate pass is a semi implicit Euler step with
the drag solved as `exp(-drag * delta)`, which is the closed form and is stable at any timestep,
and turbulence is a field of three sines over the position, because the place of a particle is
reproducible while the slot it lands in is not. `ParticleCollision` adds a ground plane and the
colliders the extract takes from the collision index within `reach`, at most sixteen, written into
the same table as the spawn points because the pass already binds eight storage buffers; a hit
keeps `bounce` of the speed into the surface, loses `friction` of the speed along it or kills the
particle. `color_variance` makes the color the sixth property drawn per particle, next to the
direction cone, speed, lifetime, size, rotation and opacity. `src/particle/simulate.h` is the C
twin of the WGSL the tests check the integrator, the draws and the hits against.

Verified: 438 tests (42 new), lint 0, 83 scenes match with `particles_forces.png` new, a million
particles at 0.55 ms, three forces at 135 ns per emitter against 126 ns with none. Left out:
curves, appearances, playback and colliders that are not a sphere or a box.
