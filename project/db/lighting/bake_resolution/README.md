# Bake resolution for large instances

Per vertex or lightmap texel probes for instances larger than a voxel so baked lighting can be enabled by default.

## Details

One probe per static instance is too coarse for walls, floors and other large primitives: the baked room shows flat lighting per tile and the lights city looks wrong with baking enabled, which is why `BakeSettings.enabled` defaults to off. This task adds a second probe layout for instances whose bounds exceed a settings driven size: a small grid of probes across the instance's bounds (a lightmap of texels in instance space, interpolated in the fragment shader by the local position) baked by the same compute pass and invalidated by the same rules. Small instances keep the single probe.

Verification: the baked room rendered again shows smooth gradients across the walls and floor comparable to the dynamic room, lights city with baking enabled matches its dynamic look, the stress scenes stay above 60 fps with baking on, and the default flips to enabled.

## References

- `project/db/lighting/light_baking/deliverable/README.md`
- `src/renderer/bake.c`, `bake.h`
