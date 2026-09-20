# console_text_blurry

## What was built

The UI font atlas now rasterizes at the device scale of the views instead of at
the logical em size. `UiFonts` carries the factor, `box_ui_sync` derives it from
the framebuffer height of the surface over the logical height of the view and
lays the tree out again when it changes, and an atlas that was rasterized for
another factor is rasterized again under a new epoch. Glyph metrics stay in
logical pixels, the quad of a glyph is its bitmap divided by the factor and its
top left corner is snapped to a device pixel, so a glyph covers whole pixels and
samples one texel per pixel.

## What was verified

`test/ui` 187 passed, with four new cases for the raster size, the
rerasterization, the snapped origin and the quad size. `tools/lint.py` 0
problems. `console`, `ui_text`, `ui_shadow` and `fps_overlay` baselines refreshed
(every glyph moved by up to half a pixel); every other visual scene still
matches. `console_text.png` is the scrollback of the console scene at 5x.

## What was left out

The box.texture path needed no change: an atlas edge is already a texture size
class, so it is published without a rescale and sampled at level zero at 1:1.
There is no `test/texture` project in the tree.

## Reopened: still blurry on a high density display

The console text is inside `panel`, which has an Opacity and children, so it renders through
a group: a view of its own into a RenderTarget that a quad composites. `box_ui_group_scale`
sized that target from `Surface.height`, the window size in points, while the font atlas and
the main UI view use the framebuffer pixels, so on a retina window the pane's subtree was
drawn at 1x and composited up 2x by the group quad's linear sample. Only text inside a
translucent element was affected, which is why the fps overlay and the `ui_text` scene were
sharp. The group now takes `box_ui_view_device` like the view, its camera shows exactly the
logical span its whole pixels cover and its quad's top left corner is snapped to a device
pixel, so a target texel lands on one frame pixel.

`Surface.device_scale` is new: a window writes it, a headless run reads it, so a high density
framebuffer can be reproduced without a window. `console_text_2x_before.png` and
`console_text_2x_after.png` are crops of the console scene captured by `box_app` headless at
`device_scale: 2` (1920x1080 framebuffer for a 960x540 window), shown at 2x, before and after.
flecs-engine's text3 was not run for a side by side: it needs a window on this machine.

Verified: `test/ui` Group `target_follows_the_device_scale` and
`target_edges_land_on_device_pixels` (both failed before, target at 200x100 for a 2x
surface), `test/core` Surface `headless_device_scale_doubles_the_framebuffer`.
