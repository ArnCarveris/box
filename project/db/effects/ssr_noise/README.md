# SSR miss noise

The SSR scene shows dithering like blue dots where pixels get no reflection, so misses are not falling back to the environment smoothly.

## Details

Isolated pixels in the reflective floor show the sky color instead of the traced reflection, in a scattered pattern that looks like dithering. Likely causes, in order: the march step count or thickness letting rays pass between depth samples so neighbouring pixels alternate between hit and miss (the fix is a thickness relative to the depth slope, a coarse to fine march on the Hi-Z pyramid and a hit refinement step), a per pixel jitter on the start offset or the step length without a matching temporal or spatial resolve, the miss weight blending to the environment with a hard step instead of a fade by the confidence of the last sample, and a half resolution trace upsampled without depth aware weights. The environment fallback must be the prefiltered map at the surface roughness, mixed by the hit confidence, so a miss is indistinguishable from the neighbouring hits at grazing angles.

Verification: the `ssr` scene re-baselined with a smooth floor reflection and no isolated miss pixels (a test counts isolated sky colored pixels inside the floor region of the capture and asserts a small number), the cause described in the deliverable README, and the SSR cost re-measured.

## References

- `project/db/effects/ssr/deliverable/README.md`
- `apps/box_app/etc/shaders/` (the ssr effect shader), `src/renderer/hiz.c`
