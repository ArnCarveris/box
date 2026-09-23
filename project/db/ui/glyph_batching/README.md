# Glyphs of one atlas share a mesh

Images keep the plain quad mesh and carry their texture region on the instance, so the glyphs of a text and the panels behind them draw in one call.

## Details

`src/geometry/shape2d.c` keyed every image mesh on its texture region, so each glyph of a text was a mesh of its own and the sorted pass drew it on its own. An unrotated image now takes the quad mesh every Rect uses and a rotated one a mesh keyed on the rotation alone; the resolved region, fit applied, lands in the new `uv_region` of `RenderMesh` and `box_render_fill_uv` (`src/render/tiling.c`) maps the instance uv transform into it, after any TextureTransform or TextureTiling, so the mesh shader needs no change. Glyphs of one atlas, and the panel quads between them, now share a mesh and the run merging of the sorted pass draws them at once.

Verification: geometry Shapes2D asserts the region on RenderMesh and the shared mesh, render Tiling the instance transform and its composition with TextureTransform, ui Text that every glyph of a label has the mesh of a Rect; the default scene with the stats panels open goes from 366 draw calls to 3, `bench/render` UiDraw record_1560_glyphs from 438 to 90 us, and every visual scene keeps its match state.
