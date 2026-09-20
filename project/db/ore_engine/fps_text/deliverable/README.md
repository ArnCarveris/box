# FPS overlay in regular text

## Built
Panel draws background, reference lines and bars; digit rendering is gone,
the number is `box.monitor`'s own UI text.

## Reopened
`box_app`'s console UI view sorted first in `view_list`, so `fps` armed the
overlay there; the renderer skips the panel pass for any UI view
(`extract_view.c:693`). Fixed in `src/command/fps.c`: `box_fps_target` now
picks the first non-UI view of `view_list`, falling back to
`box_render_primary_view` when every view is UI.

## Verified
`test/command`: 220 passed, adds `Fps_on_targets_the_scene_view_over_a_ui_
view`. `test/monitor`: 67 passed. `fps_overlay.flecs` baseline unchanged
(0 px delta). Headless `box_app` capture over `fps`/`capture` shows number
and panel together (`box_app_overlay.png`); `fps_overlay.png` is unchanged.

## Left out
Nothing owned by this task; the fix stayed in `src/command/fps.c`.
