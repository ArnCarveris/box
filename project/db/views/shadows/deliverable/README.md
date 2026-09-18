# Cascaded shadows

`ShadowSettings` (cascades 4, resolution 2048, max_distance 200, split_lambda 0.7,
depth_bias 2, normal_offset 1.5, filter_size 3, fade 0.2) reads from the view first
and the singleton after. Extract splits the camera range per view, fits every cascade
with the bounding sphere of its frustum slice so its radius never changes when the
camera turns, and snaps the light matrix to texel increments on all three axes, which
keeps it bit identical while the camera moves less than a texel. `NoShadow` (new in
`box.material`), a blend mode and `shadow_layers` clear the caster bit of an instance.

Every view owns a `resolution` squared depth array with one layer per cascade and a
second array caching the static casters. `src/renderer/shadow.h` holds the cache rule:
a cascade is dirty when its matrix changed or a static delta lands in its sphere
extruded two radii towards the light, and a frame renders every cascade that never
held an image plus at most one cached one, nearest first. The working layer is a copy
of the cached one with the dynamic casters over it, copied again only once something
wrote into it. Casters are culled against the cascade frustum on the CPU out of the
mirror and by the compute pass above the threshold.

The mesh shader picks the cascade by view depth, samples it with a comparison sampler
and a `filter_size` square PCF, blends over the last tenth of a cascade, offsets along
the geometric normal, biases in depth and fades out at `max_distance`.

`static_100k` with 1 to 1000 static changes per frame: 2.22 ms mean, 4.52 ms p99, 450
fps, 300 static rebuilds over 300 frames, against 1.72 ms and 582 fps with the sun
removed; `material_100k` 2.10 ms against 1.61 ms. `Shadows.fit_cascades` 184 ns per
view, `Shadows.casters_100k` 1.19 ms, `Extract.*` within 5% of the culling baseline.
16 tests in `view.Shadows`, six projects green, lint 0, wasm builds. Limits:
`shadow_layers` restricts dynamic casters only inside what the view draws, a caster
over three cascade radii above its cascade is clipped, and the depth arrays cost
134 MB per view at the default resolution.
