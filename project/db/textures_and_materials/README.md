# Textures and materials

PBR textures with tiling and transforms, SVG textures, blending modes and project extensible material layers.

## Details

Materials gain everything the brief lists beyond the PBR constants that scene_core already has: texture entities for albedo, emissive, roughness and normal, texture transforms and world space tiling, wrap modes, blend modes with a sorted transparent pass, and the roughness driven translucency that produces milk glass and blurred panels. Material layers are scene global effect entities such as dust, edge wear, grime and wetness that a material enables individually, with a registration point so a project can add its own layer without patching the engine. All of it is driven by component values, so a script or a terminal command reconfigures a material at runtime.

Verification is unit tests per module in a new test/material extension and in test/render, visual integration scenes that render texture, tiling, blending and layer matrices, and benchmarks for the extract cost of many textured materials and for the shading cost of enabled layers. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Texture, Svg, PbrTextures, TextureTransform, TextureTiling, TextureWrap, BlendMode, BackFaceMode and the MaterialLayer registration with Dust, EdgeWear, Grime and Wetness.

## References

- design/DESIGN.md, sections Graphics and Materials
- design/datamodel/material.flecs and design/datamodel/texture.flecs
