# GTAO

Ground truth ambient occlusion applied to indirect diffuse, with spatial and temporal denoising.

## Details

GTAO traces the depth buffer in screen space with a configurable slice and step count, denoises spatially and temporally and multiplies ambient and IBL rather than direct light. The parameters live on a Gtao component so a script or a command can change them at runtime. The effect runs at half resolution by default with a depth aware upsample.

Verification: unit tests cover the component, its graph wiring and the half resolution target sizing. Visual scenes capture a corner scene with AO on and off and an intensity sweep, and a benchmark reports the pass cost at 1080p at both resolutions.
