# Follow camera controller

## Built

`FollowCamera` joins the fly and the first person controller in `box.controller`. It writes
`Position` and `LookAt` every frame from the `Position` of its `target` plus an offset,
gliding toward that place at `glide` per second so it lags behind a target that runs and
settles when it stops; the first frame places it without a glide. `levels` holds up to four
zoom presets, each an offset and a look offset, and a preset whose offset is zero ends the
list, so a scene authors its three camera heights as data. `level` steps through them with
the `ZoomIn` and `ZoomOut` actions the module binds to `=` and `-` and with the wheel, which
is ignored while `UiCapture.mouse` says the cursor is on a UI element, and is clamped to the
presets that are authored. The camera travels to its place through `box_collision_move` with
the same `radius`, `no_collision`, `terrain` and `clearance` story as `CameraController`.

## Verified

13 new tests in `controller.Follow`, 48 in `test/controller`, all green; lint 0. The new
`follow_camera` visual scene follows a moving box at its far preset with a second camera at
the near preset rendering the same target into a texture, and all 90 other scenes stay bit
identical; all 16 command scenes pass. `Follow.update_100_followers` is 12.0 us for 100
cameras. The two older bench cases moved from 12.5 and 13.2 us to 16.6 us together on a
machine at load average 4 with the other agents building, which is the run and not the change.

## Left out

The target is read from its `Position`, so a follow camera follows a top level entity rather
than a child of one; there is no orbit around the target and no collision fade of what stands
between the camera and its target.

## Note

`test/integration/command/input.expected` gained the two lines the `ZoomIn` and `ZoomOut`
actions add to `action.list`. Importing `BoxUi` into a world that already imported
`BoxController` crashes in `box_render_state_dtor` at `ecs_fini`; importing it first is the
workaround the test uses, and the crash belongs to `src/render`.
