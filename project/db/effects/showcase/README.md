# All effects showcase scene

A visual scene with every effect enabled over content using transparency, milk glass, material layers, blend modes and PBR textures, to catch effects composing wrongly with each other.

## Details

Each effect has its own scene, which proves it works alone but not that the chain composes: ambient occlusion must not darken fog or transparent panels, reflections must not appear on fogged distance, bloom must come from tone mapped highlights rather than clipped color, and the milk glass pyramid must read the same HDR target as the chain. `test/integration/visual/showcase.flecs` enables gtao, ssr, bloom, height fog, auto exposure and tone mapping together over a scene with a textured PBR floor and props, roughness and metallic variation, emissive objects, alpha blended and additive panels, a milk glass panel, material layers on a few objects, a dynamic object, shadows and a bright sun with a colored environment, plus a second `showcase_off.flecs` with the same content and the chain disabled. Both are baselined; the deliverable README lists what each region of the image proves and any interaction found and fixed.

Verification: the two baselines, a list of checks by eye done once when the scene is created (AO absent on fog and on the transparent panels, fog over reflections, no double gamma, no banding), and the frame cost of the full chain on the scene at 1280x720 recorded in the README.

## References

- `test/integration/visual/README.md`
- the effect task reports under `project/db/effects/*/deliverable/`
