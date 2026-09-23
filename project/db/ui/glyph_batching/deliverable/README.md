# glyph_batching

## Built
- Unrotated images take the quad mesh of every Rect; rotated ones a mesh keyed on rotation
  only. The resolved region lands in `RenderMesh.uv_region` and `box_render_fill_uv` maps the
  instance uv transform into it, after TextureTransform and TextureTiling.

## Verified
- Default scene with the stats panels open: 366 draw calls to 3; the sorted pass draws every
  glyph and panel quad in one call. Blended pipeline sets stay at 1, shadow passes at 0.
- `bench/render` UiDraw record_1560_glyphs: 438 us to 90 us (`bench.json`).
- geometry Shapes2D, render Tiling, ui Text and ui DrawCost in `tests.json`; the full geometry
  (119), render (426) and ui (280) projects pass; lint 0 problems.
- All 90 passing visual scenes still match; the 17 scenes failing before still fail, with
  showcase and showcase_off differing by one pixel from their earlier failure.

## Changed tests
- Shapes2D image_uvs_show_the_region, image_cover_crops_the_region,
  image_contain_widens_the_region and image_fit_follows_the_texture_aspect assert the region
  on RenderMesh and the instance instead of on the mesh vertices, which no longer hold it.
- DrawCost sorted_pass_sets_its_pipeline_once puts a circle between two labels so the pass
  still records several draws.
