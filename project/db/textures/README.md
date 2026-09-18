# Textures

Texture entities for the PBR slots, SVG textures, and texture transforms with world space tiling and wrap modes.

## Details

Materials gain the texture half of what the brief lists beyond the PBR constants that scene_core already has: texture entities for albedo, emissive, roughness and normal, texture transforms and world space tiling, and wrap modes. An Svg component rasterizes an SVG file or inline SVG data into the same kind of texture, so vector art is a texture source like any image file. All of it is driven by component values, so a script or a terminal command reconfigures a texture at runtime.

Verification is unit tests per module in a new test/material extension and in test/render, visual integration scenes that render the texture and tiling matrices, and a benchmark for the extract cost of many textured materials. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Texture, Svg, PbrTextures, TextureTransform, TextureTiling and TextureWrap.

## References

- design/DESIGN.md, sections Graphics and Materials
- design/datamodel/texture.flecs
