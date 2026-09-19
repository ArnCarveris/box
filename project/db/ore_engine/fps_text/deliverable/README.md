# FPS overlay in regular text

## Built
The renderer panel now draws only its background, two reference lines and
bars; the digit mask/segment tables and functions are gone. The frame rate is
drawn by a UI element `box.monitor` owns: a `Ui` root sized to the `Surface`
points (960x540 headless fallback) so an anchored child resolves against a
real box instead of collapsing to the view centre, with a `Text`/`Font`/
`Anchor` child pinned to the corner `FpsOverlay` selects, at the panel's
margin/padding, sized from `FpsOverlay.scale`. Its `Rgb` is the bars' class
color converted from the 0-1 floats `box_fps_class_color` returns to the
0-255 bytes `Rgb` stores. `box.monitor` imports `box.ui`; the publishing
system runs `immediate`.

## Verified
`test/monitor -- -j 8`: 67 passed. `Fps_overlay_publishes_its_text` asserts
the resolved `Bounds` lies inside the panel rectangle for its corner, not
just that `Anchor` is set, plus a non-black `Rgb`. `fps_overlay.flecs`
baseline refreshed and inspected: the number sits inside the panel, green.
Visual sweep over every scene: only `console.flecs` fails, from another
agent's in-progress `src/ui` edits, unrelated to this task.

## Left out
Multiple `FpsOverlay` instances share one text element, from the first
enabled one found each frame; only the singleton scene case was exercised.
