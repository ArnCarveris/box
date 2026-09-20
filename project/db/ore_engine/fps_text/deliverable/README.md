# FPS overlay in regular text

## Built
Panel draws background, reference lines and bars; digit rendering is gone,
the number is `box.monitor`'s own UI text.

## Reopened twice
First: the console UI view sorted first in `view_list`, so `fps` armed the
overlay on it and the renderer skips the panel for any UI view.
Second: with the console open and no scene `View` entity, `view_list` holds
only that UI view, so the old fallback still picked it; but the renderer then
draws a synthesized scene view (no entity) that reads the singleton directly,
never that view. `box_fps_target` (`src/command/fps.c`) now targets the first
non-UI view of `view_list`, or the `FpsOverlay` singleton when every view is
UI or none exist, matching what the renderer actually reads.

## Verified
`test/command`: 221 passed, adds `Fps_on_targets_the_scene_view_over_a_ui_
view` and `..._the_singleton_when_every_view_is_ui`. `test/monitor`: 67
passed. `fps_overlay.flecs` baseline unchanged. New regression scene
`test/integration/command/console_fps.flecs` + `.cmd` + `.expected` types
`fps` into the open in-game console; full command sweep passes. Headless
`box_app`, console opened and `fps` typed over the HTTP terminal, shows
number and panel together (`box_app_overlay.png`).

## Left out
Nothing owned by this task; the fix stayed in `src/command/fps.c`.
