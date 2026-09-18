# Playback control and bursts

Start, stop, pause and reset controls alongside continuous emission and scheduled bursts.

## Details

EmitterState holds playing, paused or stopped plus a reset trigger: stopping ends emission but lets live particles finish, while reset clears the buffers immediately. Burst entries describe a count, a time and a repeat count and run alongside the continuous rate. Every control is a component write, so a script, the REST API or a terminal command drives it without new endpoints.

Verification: unit tests cover each state transition, pausing mid simulation, the burst schedule and that reset clears the live count. A command based integration test drives an emitter through its states and keeps the log, and a visual scene captures a burst explosion.
