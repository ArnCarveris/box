# UI draw calls, redundant pipeline sets and shadow rebuilds

The UI draws one call per glyph, the sorted pass sets the same pipeline before every draw, and while a UI view exists both shadow passes render every frame; bring these costs down without changing what is drawn.

## Details

Measured with the stats panels (`stats pass`, `stats pipeline`, `stats bindgroup` over the command port, or `box.monitor.PassStats` and friends): on `etc/scenes/default.flecs` with a UI panel open, the sorted pass records one draw call per glyph (273 draws of 6 indices for one panel), calls SetPipeline before every draw with the pipeline already bound (279 redundant sets a frame, `src/renderer/transparent.c`), and the shadow pass renders twice every frame once a UI view exists, while a scene without UI renders no shadow pass at all because the static cascades are cached (`src/renderer/shadow.c`).

Batch consecutive glyph quads that share mesh, pipeline and bind groups into one draw, skip pipeline and bind group sets that bind what is already bound, and keep a view that casts no shadows (the UI view) from invalidating or re-rendering the shadow cache. Nothing on screen may change: every scene in test/integration/visual must keep its current match state. Verification: the stats counts before and after on the default scene with `stats renderer` open, render and ui test suites, the visual scenes, and a benchcase in bench/render with its report as deliverable/bench.json.
