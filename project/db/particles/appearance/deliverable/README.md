# Curves, appearances and blending

`ParticleCurves` changes a particle over its life: four keys at the start, a third in, two
thirds in and at the end, for the size and opacity that multiply what it was spawned with, the
rotation that adds turns and the gradient that multiplies its color, with a constructor of ones
so an emitter that names no curve is left alone. The curves are read by the sprite pass from the
age and the lifetime the channels already hold, so they cost no per particle state and leave the
spawn values intact. `ParticleAppearance` picks a soft circle, a sprite of a texture, which
`columns` and `rows` split into a sheet that `fps` plays or that plays once over the life, or a
billboard of the mesh of an entity, which a second pipeline draws from the vertex buffer of the
scene. The texture comes from a texture entity, an `Image` or the albedo of a `PbrTextures`.
Every pool is drawn with the blended or the additive pipeline of its target format after the
glass pass, blended first and back to front by the distance of its emitter, and a `softness`
fades a particle out against the depth of the view, which the pass now takes as a read only
attachment so it can sample it. `etc/shaders/include/particle.wgsl` holds the emitter block both
shaders read and `src/particle/simulate.h` is the C twin the tests check the curves, the frame
of a sheet and the soft fade against.

Verified: 461 tests (23 new), lint 0, 84 scenes match with `particles_appearance.png` new, a
million particles at 0.49 ms, curves and an appearance at 140 ns per emitter against 129 ns
without. Left out: the screen filling smoke fill rate scene, which needs a file under
`test/integration/bench` that this task does not own, and playback.
