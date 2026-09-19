# Per vertex colors in the vertex format

Widen the vertex format with a color so Mesh3 and glTF per vertex colors multiply albedo and their alpha multiplies roughness as the datamodel says.

## Details

`Mesh3` validates and stores per vertex colors but the vertex format carries position, normal and uv only, so the colors never reach the GPU. Add a packed RGBA8 color attribute to the vertex layout (primitives write white), pass it through the mesh, sorted and shadow pipelines' vertex stages, multiply it into the albedo and its alpha into roughness in the fragment shader, and keep the mesh cache key and the packet format versioned so old images fail loudly rather than misread buffers. The generators stay unchanged apart from the white default.

Verification: a geometry test that a colored Mesh3 uploads the colors, a render test that the instance shading uses them, the `mesh3` scene extended with a vertex colored mesh and re-baselined, every other scene bit identical, and the mesh upload benchmark within noise.

## References

- `project/db/meshes/mesh3/deliverable/README.md`
- `src/renderer/packet.h`, `scene.c`, `etc/shaders/mesh.wgsl`
