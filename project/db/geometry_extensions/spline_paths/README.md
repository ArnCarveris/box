# Spline paths

Splines as motion paths for entities and as emitter shapes for particles.

## Details

FollowPath moves an entity along a spline by arc length with a speed, a loop or ping pong mode and optional orientation from the tangent, driven in OnUpdate and marking the follower dynamic. The same sampling is exposed as a particle emitter shape so particles can spawn along a curve. Paths work for cameras and lights as well, which makes scripted fly throughs a component write.

Verification: unit tests cover progress over time, looping, ping pong, orientation from the tangent and stopping at the end. A visual scene animates objects along a path, and a benchmark runs 10k followers to report the per frame cost.
