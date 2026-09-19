# FPS overlay in regular text

Draw the FPS overlay numbers with the regular UI text font instead of the seven segment digits, and drop the digit rendering.

## Details

The FPS overlay in `src/renderer/overlay.c` draws its numbers with seven segment digits it
builds from rectangles. Draw them with the regular text of the UI instead: the `B612` font the
UI already uses through `Text`, with the same size and corner the `FpsOverlay` component
selects. Remove `box_fps_digit_mask`, `box_fps_segment_rect` and the segment tables and their
tests, and keep the bars and reference lines as they are. If the overlay is drawn by the
renderer before the UI text path is available, route the numbers through a UI element
`box.monitor` owns rather than duplicating glyph rendering in the overlay.

Verification: `test/monitor` green with the digit tests replaced by a case that the overlay
publishes its text, `fps_overlay.flecs` refreshed with the new look and its screenshot in the
deliverable, every other scene unchanged.

## References

- `src/renderer/overlay.c`, `src/monitor/fps.c`, `test/monitor/src/Fps.c`,
  `test/integration/visual/fps_overlay.flecs`, `project/db/monitoring/fps_overlay/README.md`
