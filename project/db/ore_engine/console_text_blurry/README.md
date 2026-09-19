# Blurry text in the console

The console text renders blurry; UI text must rasterize glyphs at device pixels and sample them without resampling, as flecs-engine's text does.

## Details

The console does use the engine text path (`src/ui/font.c` rasterizes glyphs with stb_truetype
into an atlas, `src/ui/text.c` builds quads from it), so the blur is in that path. Leads: the
atlas is rasterized at the logical font size (`stbtt_ScaleForPixelHeight(info, atlas->size)`)
while the UI view maps logical points to device pixels through `Ui.scale` and the surface
point height, so on a high density surface every glyph is upscaled; the atlas goes to the
renderer through `box.texture`, whose loader rescales images to a square size class with a box
filter and builds a mip chain, so a glyph can be resampled twice and sampled from a mip; and
the quads may land on fractional device pixels. Compare with flecs-engine's `text3` module
(`/Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/src/modules/text3/text3.c`),
which produces sharp text, for how it picks the raster size and samples the atlas. Rasterize
at font size times the device scale, keep the atlas at its own size with no rescale and no
mip, sample it with linear filtering at level zero, and snap glyph origins to device pixels.
Rerasterize when the scale changes.

Verification: `test/ui` cases for the raster size following the scale and for the snapped
origin, the console visual scene refreshed (its text must be visibly sharp at the baseline's
scale; show a crop in the deliverable), every other text scene refreshed with a stated
reason, `test/ui` and `test/texture` green.

## References

- `src/ui/font.c`, `src/ui/text.c`, `src/ui/view.c`, `src/texture/load.c`, `etc/ui/console.flecs`,
  `project/db/ui/text/README.md`, flecs-engine `src/modules/text3/text3.c`
