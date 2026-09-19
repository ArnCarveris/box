# Spline paths

`FollowPath` moves an entity along a `Spline` by arc length. It names the curve,
a speed in world units per second, a start offset, a mode (`PathLoop`,
`PathPingPong` or `PathOnce`) and an `orient` flag that writes `Rotation` from
the tangent and the up of the curve; the engine keeps the travelled distance on
the component. A system in OnUpdate folds offset plus travelled by the mode and
samples the cached length table, so the speed is the same whatever the shape of
the curve and a fixed step is deterministic. The component adds `Position`,
`Rotation` and `DynamicTransform`, which is why a light or a camera follows a
curve the same way a mesh does.

Verified with 48 unit tests in `test/spline`, 11 of them the new `Follow` suite:
progress over time, offset, loop, ping pong with the heading reversed, stopping
at the end, orientation from the tangent, negative speed, a light follower, a
missing curve and equal speed along a curved path. `Follow.followers_10k` in
`bench/spline` moves 10k followers per frame in 0.53 ms, 0.75 ms with
orientation, in release. `test/integration/visual/spline_path.flecs` draws a
closed catmull rom loop as 72 boxes at even arc length with four objects and a
point light following it at different offsets; its baseline is new and every
other scene still matches. `tools/lint.py` reports 0 problems.

Left out: the particle emitter shape, which waits for a particles module.
