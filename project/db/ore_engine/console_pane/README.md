# Console as a translucent left pane

Lay the console out as a black pane at 75% opacity covering the left half of the screen, with the input line directly under the last issued command instead of pinned to the bottom.

## Details

The console in `etc/ui/console.flecs` is a fixed size rounded panel near the top of the screen
with the prompt pinned under a fixed height scrollback. Replace it with a pane that fills the
left half of the UI view and its whole height, `Rgb` black at 75% opacity, no border and no
shadow, whose scrollback grows from the top and whose prompt sits directly under the last
line of output, so the input line follows the last issued command down the pane. Size it from
the view rather than from constants, so a resize keeps it at half the width; if `Layout` has no
way to express half of the parent, add the primitive to `src/ui` with a unit test and keep the
rest in script. The console command scene and its `console.expected` must keep passing.

Verification: `test/ui` green with a case for any new layout primitive, the `console.flecs`
visual scene refreshed and its screenshot in the deliverable, the console command scene green.

## References

- `etc/ui/console.flecs`, `test/integration/visual/console.flecs`,
  `test/integration/command/console.cmd`, `project/db/ui/console/README.md`

## Reopened

The pane renders grey in `box_app`, not black at 75% opacity. Find why `Rgb {0, 0, 0}` with
`Opacity {0.75}` does not produce a 25% darkened scene (the composite path of a transparent
subtree, its clear colour, colour space or tonemap of the UI view, or the blend of the quad)
and fix it in the engine, checked in a capture of the real application, not only the visual
scene.
