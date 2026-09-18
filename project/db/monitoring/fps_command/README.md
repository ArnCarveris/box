# fps command

An fps command that enables or disables the FPS overlay on the active view and prints the current frame rate.

## Details

`fps` prints the current frame rate and the mean and worst frame time of the last second; `fps on` and `fps off` set `FpsOverlay.enabled` on the active view (or the singleton when no view exists), so the inspector's render pane shows the overlay in its captures. Registered like every other command group under `box.command.commands`.

Verification: command tests for the three forms and the printed format, and a command integration scene whose log holds the on and off results; the inspector render pane is checked once by eye with the overlay on.

## References

- `monitoring/fps_overlay`, `src/command/`
