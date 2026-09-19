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
