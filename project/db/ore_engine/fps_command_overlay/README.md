# fps command opens the overlay

In box_app the fps command only prints the frame rate; a plain fps must switch the FPS overlay on in every project, with fps off the way to close it.

## Details

`box_fps_command` in `src/command/fps.c` creates an `FpsOverlay` on the primary view when
there is none and sets `enabled` to the value of the `on` argument, so a plain `fps` creates a
disabled overlay and only prints. Change the command so `fps` and `fps on` both enable the
overlay on the primary view and print the window, `fps off` disables it, and the panel is
visible on the next frame in any project that imports `box.command` (which imports
`box.monitor` for the command), including `box_app` opened through the backtick console. Check
that the target is the view that renders to the surface when several views exist and that the
overlay of the singleton is honoured when a world has no view, and that the overlay text from
`fps_text` follows.

Verification: `test/command` cases for the three forms asserting `FpsOverlay.enabled` on the
target, the `fps` command scene under `test/integration/command` refreshed with the overlay
visible in its capture, `test/command` and every command scene green, and `fps` in the
`box_app` console showing the panel by hand.

## References

- `src/command/fps.c`, `src/monitor/fps.c`, `test/command/src`, `test/integration/command`,
  `project/db/monitoring/fps_command/README.md`, `project/db/ore_engine/fps_text/README.md`
