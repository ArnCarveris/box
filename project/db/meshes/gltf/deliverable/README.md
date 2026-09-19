# glTF import

Built: `box.gltf` adds `Gltf {path, scene, scale}`, which parses a `.gltf` or `.glb`
with the cgltf bundle and builds a child tree: one entity per node with `Position`,
`Rotation` and `Scale` mirrored from glTF axes into box axes, one child per primitive
with a `Mesh3` the mesh cache shares, and `Rgb`, `Opacity`, `PbrMaterial`, `Emissive`,
`BlendMode`, `BackFaceMode` and `PbrTextures` from its material. Images become texture
entities under a `textures` child, from a file next to the glTF, a data uri or a buffer
view; `box.texture` gained `box_texture_set_memory` for those. Another path replaces
the tree, removing the component or deleting the owner takes it away.

Verified: 30 cases in `test/gltf` over two checked in fixtures, 352 tests over `gltf`,
`geometry` and `render`, 0 lint problems. `Load.mesh_100k` loads a 100k triangle model
in 1.01 ms, `Load.nodes_500` a 500 node scene in 9.05 ms. `gltf.png` renders
`crate.gltf` and the Khronos CC0 `box_vertex_colors.gltf` next to a primitive sphere;
the other baselines are untouched.

Left out: the load is synchronous, so the job system is unused; cameras, lights,
animation, skinning, morph targets, vertex colors, tangents, second uv sets, sampler
wrap modes and compression extensions are skipped.
