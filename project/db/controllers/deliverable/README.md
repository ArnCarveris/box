# Controllers: completion report

## What was built
- `box.controller` with `CameraController`: fly movement with speed, a fast multiplier, acceleration and mouse sensitivity, mouse look while the cursor is captured or the `Look` action is held, and clearance above a terrain entity's bounds. It writes `Orientation` and `Position` like any system and creates the actions `Forward`, `Back`, `Left`, `Right`, `Up`, `Down`, `Fast` and `Look` bound to W, S, A, D, E, Q, Shift and the right mouse button only when no binding exists, so scenes rebind by editing the map.
- `FirstPersonController`: walk and run speeds bound to actions, gravity with a ground check against the terrain or a flat ground height, a jump impulse, crouch and eye heights, and the F toggle into free flight that reuses the camera controller movement.
- Both controllers take all input through actions, matching the brief and the flecs-engine controller behaviour they were modelled on.

## Verification
- 842 unit tests across twelve projects, all passing; `test/controller` holds 27; `test/controller` holds 27 tests over injected input driving position and orientation, the fast modifier, sensitivity, clearance, jump arcs under the fixed step, crouching, the fly toggle and speed caps.
- The `first_person` visual scene is baselined; every other scene matches its baseline (`integration/`).
- Benchmarks in `bench/` against the culling epic: the controller cases are new (`Controller.update_camera`, `Controller.update_first_person`), everything else as in the input epic report.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Writing `Orientation` rather than `Rotation` avoids gimbal lock for yaw and pitch and lets `LookAt` stay a one shot setup that the controller then owns.
- A fixed headless step makes jump arcs and movement tests exact frame by frame.

## Not in this epic
- Collision against scene objects is the `collision` epic's `controller_collision` task; the ground check uses the terrain bounds until then.
- Speed and feel were matched to flecs-engine's defaults, not tuned by hand in a window.
