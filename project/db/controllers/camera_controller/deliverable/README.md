# Camera controller

`box.controller` adds `CameraController` with `speed`, `fast`, `acceleration`, `sensitivity`,
`terrain`, `clearance` and the `yaw`, `pitch` and `velocity` it writes every frame; a member that
is zero or less takes its default (10 units per second, 4x fast, 8 per second of acceleration and
0.003 radians per cursor point). The module creates the actions `Forward`, `Back`, `Left`, `Right`,
`Up`, `Down`, `Fast` and `Look` under the `actions` scope and binds them to W, S, A, D, E, Q,
either shift and the right mouse button with named `Binding` children of `box.input.map`, but only
when the action has no binding yet, so a scene rebinds by editing `box.input.map.<Action>`.

`BoxCameraControllerUpdate` runs in OnUpdate: it turns the camera with the cursor movement of the
frame while `Surface.cursor_captured` is set or the `Look` action is held, eases the velocity
towards the direction the movement actions ask for, integrates `Position` itself and clamps the
camera to `clearance` above the top of its terrain's `AABB`. It writes `Orientation`, which wins
over `Rotation`, so it never fights the `LookAt` system; when the entity has a `LookAt` (a `Camera`
adds one) the controller points it at what it looks at, which keeps both consistent. Pitch follows
the `Orientation` convention, so it is positive looking down and stops short of straight down.

15 tests pass in `test/controller` suite `Camera` and `Camera.update_100_controllers` costs 12 us.
