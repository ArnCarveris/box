# Camera controller ported from flecs-engine

CameraController does not fly correctly; replace its movement and look model with the one in flecs-engine's camera controller, which is the reference feel.

## Details

Port the behaviour of flecs-engine's `camera_controller.c`
(`/Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/src/modules/camera/camera_controller.c`)
into `src/controller/camera.c` and `look.c`, keeping box's `CameraController` component and its
actions rather than raw keys. The reference model: a target velocity from the move actions in
the yaw plane (forward and right from yaw only, up and down on the world axis), scaled to a
max speed of 40 units per second and five times that while Fast is held, smoothed with
`1 - exp(-k dt)` at k 6 when accelerating and 18 when stopping; a target angular velocity from
the look actions at 1.5 radians per second with yaw a third as fast while sprinting, smoothed
at k 5 and 15; while the right mouse button is held the angular velocity comes straight from
the cursor delta at 0.003 radians per point divided by dt (yaw a third while sprinting); the
wheel adds an impulse of 3 units along the look direction; pitch clamped to just under a
quarter turn with the angular velocity zeroed at the clamp; the position kept above the
terrain sample plus the clearance with a downward velocity zeroed there. Box's defaults
(`speed`, `fast`, `acceleration`, `sensitivity`) map onto these constants and keep their names,
with the new smoothing and angular terms as fields with the reference defaults. Keep the
collision radius behaviour box added. State in the deliverable what the previous model got
wrong (the report is only that it "does not work correctly", so compare the two by driving the
same input through both and describing the difference).

Verification: `test/controller` cases for the smoothing curve (velocity after n frames of a
held action matches the closed form), the sprint yaw scale, the mouse look while the button is
held and not held, the wheel impulse, the pitch clamp and the terrain clearance, the existing
cases still green, and the default scene flown by hand in `box_app`.

## References

- flecs-engine `src/modules/camera/camera_controller.c`, `src/controller/camera.c`,
  `src/controller/look.c`, `include/box/controller.h`, `project/db/controllers/deliverable/README.md`

## Also

Adding `CameraController: {}` to the camera of the default scene moved the camera, which is
why the scene has it removed at the moment. A controller added to a camera that has a
`Position` and a `LookAt` or a `Rotation` must start from that orientation: initialize yaw
and pitch from the current look direction on the frame the controller appears and leave the
position alone until an action or the mouse moves it. Cover it with a `test/controller` case
that adds the controller to a placed camera, progresses a frame with no input, and asserts
the position and the look direction are unchanged. Once that holds, put `using box.controller`
and `CameraController: {}` back in `apps/box_app/etc/scenes/default.flecs` (you own that file
for this change) and verify with a headless capture that the first frame matches the frame
without the controller.
