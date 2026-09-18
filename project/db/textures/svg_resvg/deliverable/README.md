# SVG rasterization with resvg

`src/texture/svg.c` rasterizes `Svg` with resvg instead of nanosvg. The bundle is
`crates/c-api` of `linebender/resvg` v0.47.0, built with cargo in release with
`--no-default-features --features=text,system-fonts,memmap-fonts`; nanosvg is gone. One
shared `resvg_options` is built at import with the system fonts and
`apps/box_app/etc/fonts/B612-Regular.ttf` (OFL 1.1) as the fallback and the default sans
serif family, so text renders headless and on the web, where the file comes out of the
embedded `etc`. The options are never mutated afterwards, which lets a worker parse with
them. resvg writes premultiplied pixels, so they go back to straight alpha and a
transparent texel takes the average color of its opaque neighbours, the defringing
nanosvg did and the mip chain needs. The `Svg` interface is unchanged; a file path now
also resolves against the engine search path.

11 `Svg` tests pass; the three new ones check a text stem, a masked half and a clipped
half. `svg_text`, `svg_mask`, `svg_clip`, `svg_gradient` and `svg_filter` show B612 text,
a gradient mask, a star clip path, linear and radial gradients and a blur with a drop
shadow, each at 128 and 1024 px; `svg_icons` was rebaselined. `Svg.rasterize_256` costs
0.65 ms against 0.39 ms for nanosvg. The wasm build links resvg for
`wasm32-unknown-emscripten` and embeds the font.
