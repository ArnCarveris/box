# Follow camera controller

A third person camera controller that follows a target at an offset with stepped zoom levels and a glide, next to the fly and first person controllers, so a game does not write a camera system in C.

## Details

`box.controller` has `CameraController` for a fly camera and `FirstPersonController` for a character,
and nothing for the camera every third person game needs. Ore Else writes it by hand in
`OreCameraFollow`: the camera sits at a height and a distance behind the player, looks a little above
its feet, eases toward the target at a rate per second, and the wheel steps between three presets of
height, distance and look height unless the UI holds the mouse. Add `FollowCamera {target, offset,
look_offset, glide, levels, level}` to `box.controller` that writes `Position` and `LookAt` every
frame, with the stepped levels as an array a scene authors and the zoom action bound the way the
other controllers bind theirs, the same collision and clearance story as `CameraController`, and the
capture query of `ore_engine/input_layers` deciding whether the wheel reaches it.

Verification: Unit tests on a fixed clock for the offset, the glide toward a moving target, the level steps and
their clamps, a visual scene following a moving box at two levels, every other scene unchanged.

## References

- project/db/controllers/deliverable/README.md
- project/db/ore_engine/input_layers/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/player.c
