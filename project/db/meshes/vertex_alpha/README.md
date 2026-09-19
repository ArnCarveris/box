# Vertex color alpha

An rgba type in box.types and Mesh3 colors with alpha so per vertex alpha scales roughness as the datamodel says, through Mesh3 and glTF.

## Details

The vertex format carries a packed RGBA8 color and the shader multiplies its alpha into roughness, but `Mesh3.colors` is a vector of `rgb`, so the alpha is always one. Add `rgba` to `box.types` with reflection, switch `Mesh3.colors` to it (scripts write four components), pack the alpha into the upload, have the glTF importer keep `COLOR_0` alpha when present, and keep white with full alpha as the default.

Verification: a geometry test that a vertex alpha of 128 halves the packed alpha, a render check that roughness follows it, the glTF fixture with a four component color, the `mesh3` scene extended with a vertex alpha gradient and re-baselined; other scenes bit identical.

## References

- `project/db/meshes/vertex_colors/deliverable/README.md`
- `include/box/types.h`, `include/box/geometry.h`, `src/geometry/mesh3.c`, `src/gltf/`
