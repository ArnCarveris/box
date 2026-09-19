# Surface resolution scale

Render the frame at a fraction of the surface and upscale it to the window, the knob the game's Low quality tier turns; Surface.resolution_scale with the UI, the picking rays and the captures staying in surface points.

## Details

Ore Else's quality tiers set `Surface: {resolution_scale: qualityScale}` and the Low tier renders at
half resolution, which is what makes the game playable in a mobile browser. Box's `Surface` reports
`actual_width` and `actual_height` from the window and has no such knob, so the tier task of
`ore_release/quality` has nothing to turn. Add `resolution_scale` to `Surface`: the swap chain
target keeps the window size, every view that renders into the surface renders into a target scaled
by it and is blitted up at the end of the frame, and everything authored in points stays in points,
so a UI tree keeps its logical pixel size, `box_picking_ray` keeps taking normalized cursor
positions, `Capture` writes the window size and the visual baselines at scale 1 do not move. Report
the scaled size next to the actual one so a scene can read what it got.

Verification: A unit test that the render target follows the scale and the reported sizes are right, a headless
capture at a scale of two that matches the shape of the full resolution one, the UI and picking
scenes unchanged at scale 1.

## References

- project/db/views/deliverable/README.md
- project/db/engine_core/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/quality.flecs
