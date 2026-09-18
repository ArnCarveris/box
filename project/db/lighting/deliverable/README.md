# Lighting: completion report

## What was built
- Clustered forward lighting: point and spot lights extracted with linear color and range, culled against the camera frustum on the CPU, uploaded per packet; a froxel grid (`ClusterSettings`, 16 by 9 by 24, 64 lights per cluster) built in a compute pass with logarithmic depth slices; the mesh shader loops over the lights of its cluster with windowed inverse square falloff and a squared spot cone. `box_cluster_index` is shared by the CPU, the tests and the WGSL.
- Image based lighting: `EnvironmentLight` (sky and ground colors, intensity, reserved source) prefiltered only on change into a 64 px cubemap with a five mip GGX specular chain, a 16 px irradiance map and a split sum BRDF table computed once; the old hardcoded ambient term is gone and the defaults reproduce it.
- Baked lighting: static lights and emissive static instances baked in a compute pass into order 1 spherical harmonics probes, a grid of probes per instance at one per world unit (`BakeSettings.probe_spacing`, seams shared) interpolated trilinearly in the shader, occlusion from a 128 by 32 by 128 voxel occupancy grid ray marched 32 steps, dirty probes rebaked under a per frame budget nearest the camera first, static instances skipping static lights in the cluster loop. Enabled by default.
- Churn cost: a moved instance that no static light reaches pushes no invalidation, leaves the grid alone and parks its descriptor, so the bake is free in scenes lit only by the sun.

## Verification
- 358 unit tests across test/core (27), test/transform (46), test/geometry (32), test/render (103) and test/command (150), all passing (`tests.json`, `tests_<project>.json`); the render project alone grew from 37 to 103 tests (`Lights` 18, `Environment` 8, `Bake` 40).
- Visual scenes in `integration/`: `lights_city` (324 point lights and 6 spots over a box city), `pbr_spheres` (roughness by metallic grid under a colored environment), `baked_room` and `dynamic_room` (the same room with static and dynamic lights, the pillar shadowing the wall, emissive boxes lighting their surroundings), plus `primitives`.
- Stress benchmarks with baking on: `static_100k` 1.30 ms mean and 2.73 ms p99, `material_100k` 1.28 ms mean and 2.72 ms p99 for 110k entities with 1 to 1000 static changes per frame (`integration/*_100k.json`), against 1.25 ms mean before this epic.
- Microbenchmarks in `bench/` against the inspector epic baseline: `Extract.static_1_changed` 17% slower (about 60 ns of fixed per frame light, environment and bake extraction), twelve unrelated cases 5 to 9% faster because the baseline was taken under load. New cases: extraction of 1000 moving lights 6.8 us, `Bake.churn_1000_of_100k` 77 us, `Bake.invalidate_one_of_100k` 0.04 us.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- The game thread blocks on the packet queue, so renderer thread cost shows up as main thread frame time; measuring the split with temporary timers found the invalidation scan and a whole array descriptor upload, not the GPU.
- One probe per instance is too coarse for walls and floors; a probe grid per instance with seam sharing gave results matching dynamic lighting at a small cost.
- A light can only be blocked by geometry it reaches, which turns invalidation from a scan over every instance into a range test around changed lights.
- `bake3` did not always notice edited sources; touching the file before rebuilding worked around it, worth a look in bake3.

## Not in this epic
- Layer restriction of lights and per view settings wait for the views epic; froxels assume a perspective camera.
- A moved instance that a static light reaches still refills the whole occupancy grid on the GPU; an incremental update needs per voxel counts.
- Environment cubemap textures and the atmosphere as source come with the textures and sky epics; the environment is not drawn as a background.
