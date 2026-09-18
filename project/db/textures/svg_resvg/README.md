# SVG rasterization with resvg

Replace nanosvg with resvg so text, masks, clip paths, gradients and filters render, with a visual scene per feature.

## Details

nanosvg covers paths and simple fills only. resvg's C API renders the full static SVG feature set: text with system and embedded fonts, masks, clip paths, patterns, gradients, filters and nested images. flecs-engine bundled it as a cargo bundle of `crates/c-api` from resvg v0.47.0 with the `text`, `system-fonts` and `memmap-fonts` features, loaded system fonts plus an embedded fallback font for headless and wasm, and trimmed the wasm build with an asyncify exclusion list; this task does the same for box. The `Svg` component keeps its interface: file or inline data, a requested size, re-rasterization on change, the same texture handle kind.

Verification: the existing `Svg` tests keep passing on resvg; new unit tests rasterize fixtures with text, a mask and a clip path and check sampled pixels; one visual scene per feature under `test/integration/visual/svg_text.flecs`, `svg_mask.flecs`, `svg_clip.flecs`, `svg_gradient.flecs` and `svg_filter.flecs`, each baselined, plus the existing icon scene; the wasm build compiles and embeds the font. The rasterize benchmark records the cost against nanosvg.

## References

- flecs-engine `project.json` (resvg bundle), `src/modules/texture/svg.c` (options, fonts, size handling)
- `src/texture/svg.c`, `test/render/src/Svg.c`
