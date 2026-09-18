# HDR scene target

Render the scene into an RGBA16F target whenever a view runs effects so tone mapping and bloom see unclipped color.

## Details

The mesh, transparent and glass passes write the view's 8 bit surface or render target, so values above one are clipped before the chain runs and the tone mapper and bloom only see what the chain itself produced. When a view has an enabled effect chain the scene passes render into a pooled RGBA16F target instead, every intermediate stays 16 bit, and the last pass writes the 8 bit view target; the milk glass background pyramid and captures read whichever target the view uses. Views without a chain keep the direct 8 bit path and stay bit identical. The environment gamma flag on the view follows the target so linear values are only encoded once at the end.

Verification: effect tests that a view with a chain allocates a 16 bit scene target and one without does not; the tonemap and auto exposure scenes re-baselined with a bright sun showing highlight roll off instead of clipping; every other scene bit identical; the chain cost re-measured.

## References

- `project/db/effects/exposure_tonemap/deliverable/README.md`
- `src/renderer/frame.c`, `effects.c`
