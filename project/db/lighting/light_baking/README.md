# Baked lighting

GPU baked lighting for static lights on static geometry, including the light emitted by emissive surfaces.

## Details

Lights that are static, meaning not dynamic and not driven by the time of day, and static geometry are baked on the GPU into per instance irradiance so shading them costs a lookup instead of a light loop. Emissive surfaces are gathered into the bake, so a glowing primitive lights its surroundings. The bake runs as jobs, prioritises content near the camera, never blocks the render thread and its results are cached in renderer owned storage. Changing a static light or a static entity invalidates only the instances it affects.

Verification: unit tests cover the static classification, the invalidation set, emissive gathering and the job priority order. Visual scenes compare a baked room with the same room lit dynamically, and a benchmark measures bake throughput and proves that moving one entity re-bakes only its neighbourhood.
