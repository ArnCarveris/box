# Playback control and bursts

`ParticlePlayback` holds the state of an emitter, playing, paused or stopped, the clock it has
been playing for and a reset trigger. Stopping ends the emission and leaves the particles in the
air to finish, pausing raises a bit in the block that makes the renderer record neither compute
pass, so the pool is drawn exactly as the frame that paused it left it, and reset empties the pool,
rewinds the clock and is written back to false by the frame that acts on it. Starting a stopped
emitter rewinds it, a pause keeps its place. The clock is engine written and only runs while the
emitter plays, which is what `ParticleBurst` schedules ride on: a burst is evaluated from the clock
instead of from a countdown, so the times it went off is the clock over its period capped by its
repeat count and a frame spawns the difference between the two ends of the frame. Nothing is
carried between frames, so a burst lands on the same frame on every run. A schedule sits on the
emitter or on any of its descendants, which its query reaches over `ChildOf`, and what the bursts
ask for is added to the rate and capped by the pool with it. Every control is a component write, so
a script, the REST API and a terminal command drive it; `particle.start`, `pause`, `stop`, `reset`
and `time` are the same controls as script functions.

Verified: 112 tests (26 new), lint 0, 85 scenes match with `particles_control.png` new, the command
log of `particles_control` drives an emitter through every state and edits a schedule, a million
particles still at 0.48 ms and the extract at 262 ns per emitter with a playback and four schedules
against 216 ns with neither. Left out: a second command phase, which needs a `.cmd` file this task
does not own, and the per view budgets and indirect draw of the scale task.
