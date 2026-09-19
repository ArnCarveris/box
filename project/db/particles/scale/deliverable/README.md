# Ten million particles

A particle is 52 bytes: its place with its age and its velocity with its lifetime keep full
precision, its color with its opacity is two half float words and its rotation with its size is
one, and the dead list and the compacted alive list take a word each. The integrate pass is
dispatched over the slots a pool has used rather than over its ceiling, the slots of a frame are
taken off the dead list in one pass of one thread, and an added pool is drawn indirectly over the
list the simulation compacts, so neither the simulation nor the draw touches a slot that holds
nothing; a blended pool is drawn over its slots in order, because the order two sprites blend in
has to be the same from frame to frame. A quad is four vertices as a strip instead of six, which
is where most of the frame went. `ParticleSettings` caps what the pools hold and what a frame
spawns, shared out in proportion to the asks so a scene over budget thins every stream, and culls
an emitter whose bounding sphere no view sees, drops one past `cull_distance` and steps one past
`step_distance` every other frame with both frames in one step. `ParticleStats` reports the
emitters, the culled, the particles and the bytes. Taking the slots in order made the whole system
reproducible: every particle scene now renders the same image on every run.

Verified: 136 tests (24 new), lint 0, 85 scenes match, every command and bench scene passes, ten
million particles over ten emitters at 10.5 ms mean and 17.1 ms p99 in release, which is 95 fps
against the 60 the brief asks for, 520 MB of pools, the new fill rate scene at 10.7 ms with 11200
screen filling soft sprites, a million particles still at 0.49 ms and the extract at 240 ns per
emitter against 216 ns before the budgets. Left out: packing the place and the velocity, which
would move the baselines of the scenes this task does not own.
