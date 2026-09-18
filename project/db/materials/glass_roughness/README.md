# Milk glass roughness sweep

A roughness sweep of translucent panels and an efficiently blurred background chain so opacity and roughness visibly combine.

## Details

The milk glass scene shows one frosted and one clear panel and the effect of intermediate roughness values is not visible. This task adds a visual scene with a row of panels at opacity 0.5 and roughness 0, 0.2, 0.4, 0.6, 0.8 and 1.0 over detailed geometry, and makes the background chain the renderer samples a properly blurred one: a dual filter downsample and upsample chain (Kawase style taps) instead of plain halving, sampled with trilinear filtering between levels and a level chosen from roughness with a perceptual curve, so each step of the sweep is visibly blurrier than the last and the blur is smooth rather than blocky. The chain is built once per view per frame only when a translucent instance is visible.

Verification: the sweep scene viewed by eye shows six distinct blur levels with no blockiness, the existing milk glass and blending scenes still look right, a unit test covers the level mapping from roughness, and a headless timing of the chain per frame is recorded in the deliverable.

## References

- `project/db/materials/blending/deliverable/README.md`
- `src/renderer/frame.c`, the transparent pass and the background copy
