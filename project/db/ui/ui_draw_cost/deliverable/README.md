# ui/ui_draw_cost

Built:
- Sorted pass (`src/renderer/transparent.c`) tracks what it has bound: pipeline, mesh bind group and mesh buffers are only set when they differ; a group composite no longer resets the environment group after every draw.
- Shadow cache (`src/renderer/shadow.c`): an update of a static slot whose layer run the view does not cast (the UI panel on the UI layer) no longer invalidates the cached cascades.

Verified on apps/box_app default scene with `stats renderer` open (before -> after):
- blended pipeline sets 231 -> 1 (draws 231 -> 230), frame pipeline sets 237 -> 5.
- shadow passes 2 -> 0 per frame, render passes 5 -> 3; scene alone unchanged (1 draw, no shadow pass).
- tests: ui 279 and render 424 pass, new suite ui.DrawCost (2 cases, both fail on the old code).
- visual: 90 pass; the 17 failing scenes fail with identical numbers before and after.
- bench.json: UiDraw.changing_panel_next_to_2000_casters 696 -> 477 us; record_1560_glyphs within noise.

Left out: one draw per glyph. Each glyph Image bakes its atlas region into the mesh key
(src/geometry/shape2d.c), so every character is a different mesh. Batching needs the region moved
into the instance uv transform (src/render/tiling.c) with the image mesh keyed on rotation only.
