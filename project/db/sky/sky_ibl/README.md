# Sky lighting and fog sync

The atmosphere as the image based lighting source and as the fog color, so sky, lighting and fog stay in sync.

## Details

The sky is rendered into a small cubemap that feeds EnvironmentLight, updated when the sun direction or the atmosphere parameters change rather than every frame. Fog color and density are derived from the same LUTs so distant geometry, fog and sky agree, with a toggle for projects that author fog by hand. The ambient term used by baked and clustered lighting reads the same environment.

Verification: unit tests cover the update trigger, the derived fog values at several sun elevations and the manual override. Visual scenes render a foggy morning and a clear noon with reflective spheres to show the environment tracking the sky, and a benchmark proves the cubemap update is not a per frame cost.
