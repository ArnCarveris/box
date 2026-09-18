# 2D shapes

The remaining 2D shapes: Circle, Border with per corner radii, Line, and Image showing a region of a texture.

## Details

The 2D set that scene_core started is completed with Circle, Border with per corner radii, Line and Image showing a region of a texture. They are scalable shapes like the primitives, so they instance through the same path and carry no geometry of their own. The UI epic builds its widgets out of them, which is why they ship before it.

Verification is unit tests per component in test/geometry and a visual scene that renders the 2D sheet. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Circle, Border, Line and Image.

## References

- design/DESIGN.md, section Content (shapes)
- design/datamodel/geometry.flecs
