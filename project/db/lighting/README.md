# Lighting

Clustered forward lighting for dynamic lights, GPU baked lighting for static lights and emissive geometry, and image based lighting.

## Details

Dynamic lights are shaded with clustered forward rendering: a froxel grid per view, a compute pass that assigns point and spot lights to clusters and a surface shader that loops over the lights of its cluster. Static lights on static geometry are baked on the GPU into per instance irradiance so their runtime cost is a lookup, and emissive surfaces contribute to that bake as the brief requires. Image based lighting comes from a constant color or a cubemap and is prefiltered per roughness, which is also the hook the sky epic plugs the atmosphere into.

Verification is unit tests for light extraction, range culling, cluster assignment, bake invalidation and prefilter triggering, visual scenes with hundreds of point lights, a baked room and a roughness and metallic sphere grid, and benchmarks for 1000 moving lights and for bake throughput. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: the cluster settings on View, BakedLighting with its per instance storage, and EnvironmentLight for IBL. Existing DirectionalLight, PointLight, SpotLight and Emissive components gain their real behaviour here.

## References

- design/DESIGN.md, section Graphics (baked lighting, clustered forward, IBL)
- design/datamodel/light.flecs
