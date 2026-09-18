# SVG textures

`box.texture` adds `Svg {file, data, width, height}`. Setting it parses the SVG with
nanosvg (`nsvgParseFromFile`, or `nsvgParse` on a copy of `data` because it rewrites the
string), rasterizes it with nanosvgrast and hands the RGBA8 pixels to the path `Texture`
uses, so the entity gets a `RenderTexture` and binds to a `PbrTextures` slot like any
image. A zero width or height follows the other one at the aspect ratio of the SVG, both
zero keep the SVG size, and a requested aspect that differs fits the art undistorted.
Changing file, data or size re-rasterizes into the same handle while the size class holds
and swaps it otherwise; removing the component destroys it. The first of `Texture` and
`Svg` on an entity owns its texture, the second is rejected and logged once.

nanosvg un-premultiplies and defringes, so the pixels go up as straight alpha unchanged.
The shader samples only `.rgb` of the albedo slot, so transparency reads as black and the
icons in `apps/box_app/etc/svg/` are drawn full bleed. `svg_icons.flecs` renders gear,
warning and badge at 64, 256 and 1024 px (`svg_icons.png`): the left column is blocky, the
right one clean. Its quads are rotated 180 degrees about Z because the geometry module
puts uv v = 0 at the bottom, which flips every texture. `Svg.rasterize_256` costs 0.39 ms
per icon; the extract regressions in `bench.json` come from the parallel tiling task.
