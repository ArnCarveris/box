# Per vertex colors in the vertex format

Built: the vertex layout gained a packed RGBA8 color as its last channel, so a
vertex is ten floats wide and `BOX_VERTEX_VERSION` keys the layout the upload,
the pipelines and the mesh shader agree on. `Mesh3` colors are packed into the
stream the mesh cache uploads, the glTF importer reads `COLOR_0` into them the
way material factors are encoded, and a mesh without colors uploads white, which
the primitives write. The mesh and sorted pipelines bind the attribute, the
shadow pipeline stays position only, and the fragment stage multiplies the color
into the albedo and its alpha into the roughness.

Verified: `geometry.Mesh3`, `render.Extract` and `gltf.Mesh` gained two cases
each over the upload, the white default, the instance color beside it and a new
vertex colored fixture; 454 tests pass, the linter reports 0 problems,
`mesh3.png` is the scene, `gltf.png` was rebaselined for the Khronos vertex
colored box, every other baseline is bit identical and `Upload.mesh_100k` runs
in 0.89 ms, within noise of the 1.00 ms before.

Left out: `Mesh3.colors` holds `rgb`, so a vertex alpha is always opaque and the
roughness multiply waits on a color type with alpha in the datamodel.
