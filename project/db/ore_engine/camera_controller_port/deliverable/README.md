# Camera controller ported from flecs-engine

## Built
`src/controller/camera.c` now runs the flecs-engine model: a target velocity in the yaw
plane at 40 units/s (x5 while Fast), eased with `1 - exp(-k dt)` at k 6 when a move action
is held and 18 once it stops; an angular velocity that chases the new TurnLeft/Right/Up/Down
actions (arrow keys) at 1.5 rad/s, k 5 and 15, and that the Look button sets straight from
the cursor delta (yaw a third while sprinting) and lets coast when released; a wheel impulse
of 3 units along the view; the pitch clamp zeroes the angular velocity. `CameraController`
gains `deceleration`, `angular_speed`, `angular_acceleration`, `angular_deceleration` and
`angular_velocity`; the old names keep their meaning with the new defaults.

## What the old model got wrong
Driving the same input through both: the old controller reached 10 units/s (not 40), eased
at one rate (8) for starting and stopping, so it drifted on after a key was released; the
mouse wrote yaw and pitch directly, so a turn stopped dead the frame the button came up; it
had no keyboard turn, no wheel and no sprint yaw scale. Worse, adding the controller to a
placed camera snapped it to yaw 0, pitch 0: a script sets Position and LookAt after it adds
the component, so an OnAdd observer saw zeros. Two OnSet observers now aim the controller
from Position+LookAt (or Rotation without LookAt) whenever the camera is placed.

## Verified
`test/controller`: 58 passed, 10 new Camera cases (smoothing curve closed form, turn action
curve, sprint yaw scale for mouse and keys, wheel impulse, coast after release, clamp zeroes
the angular velocity, authored LookAt, authored Rotation, scripted LookAt). One old case,
`the_fast_modifier_multiplies_the_speed`, compared the default speed with an explicit 10 x 3
and only held while the default was 10; it now sets speed 10 for both runs.
`default_with_controller.png` and `default_without_controller.png` are captures of the
default scene through the command port in `box_app`, max channel delta 1;
`default_flown_back.png` is the same run after `key.press S 30`, the camera eased back along
its view (flown over the port, never in a window).
The controller is per frame work with no bench project; `bench: false`.
