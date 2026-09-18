# Materials

Blend modes with a sorted transparent pass, roughness driven translucency, and project extensible material layers.

## Details

Materials gain the shading behaviour the brief lists on top of the texture slots: blend modes with a sorted transparent pass, back face handling, and the roughness driven translucency that produces milk glass and blurred panels. Material layers are scene global effect entities such as dust, edge wear, grime and wetness that a material enables individually, with a registration point so a project can add its own layer without patching the engine. All of it is driven by component values, so a script or a terminal command reconfigures a material at runtime.

Verification is unit tests per module in the test/material extension and in test/render, visual integration scenes that render the blending and layer matrices, and a benchmark for the shading cost of enabled layers. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: BlendMode, BackFaceMode and the MaterialLayer registration with Dust, EdgeWear, Grime and Wetness.

## References

- design/DESIGN.md, sections Graphics and Materials
- design/datamodel/material.flecs
