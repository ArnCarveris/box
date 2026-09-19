# Assets and materials

The SVG assets, the terrain generator and the material scripts carried over to box.

## Details

Copy the 52 SVGs and tools/gen_terrain.py into apps/ore_else/etc, verify every SVG rasterizes
through resvg (text, masks, gradients, filters) and file textures tasks for any that do not, rewrite
materials.flecs and etc/materials/* against PbrMaterial, PbrTextures, TextureTransform and the
material layers of box, and regenerate the terrain triplet with the tool.

Verification: A visual scene of every material on spheres and boxes, and a test that every asset loads without a
warning.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/assets
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/materials
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/tools/gen_terrain.py
- project/db/textures/deliverable/README.md
