# Image orientation matches mesh UVs

Images render vertically flipped because mesh UVs put v=0 at the bottom while loaded and rasterized images start at the top row.

## Details

The mesh generators follow the convention that v=0 is the bottom of a texture, which is what modelling tools and the UV layouts in the generator tests assume, while stb_image and nanosvg produce rows from the top, so every texture appears upside down and the SVG icon scene rotated its quads to compensate. The texture module flips rows when it publishes pixels, for both image files and rasterized SVGs, and the SVG scene drops its rotation. Normal maps are affected too: the green channel encodes the up direction, so the flip fixes bump lighting that was inverted on the textured scene.

Verification: a reproducer test loads a two row image with distinct colors and checks that the row at v=0 in the published pixels is the bottom row of the file; the textured and SVG icon scenes render with upright artwork, checked by eye, and the geometry tests are untouched.

## References

- `src/texture/`, `test/integration/visual/svg_icons.flecs`, `textured.flecs`
