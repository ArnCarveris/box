# Mesh3 custom meshes

Built: `Mesh3` holds vertices, normals, uvs, indices and per vertex colors as
reflected vectors with its own lifecycle hooks. Setting it validates the
streams, fills in what they leave out (flat normals, zero uvs, a sequential
index list), hashes the data and writes `RenderMesh`, so the entity joins the
instanced path of the primitives. The hash keys a reference counted registry
beside the primitive mesh cache, so entities and prefab instances with the same
data upload once and the mesh is destroyed with its last user.
`src/render/mesh.c` now owns the upload, destroy and packet hand off of handles.

Verified: the `Mesh3` suite of `test/geometry`, 18 cases over upload, bounds,
derived and supplied normals, validation, sharing, prefab instances, replacing
and freeing; 309 tests pass over `geometry` and `render`. `Upload.mesh_100k`
uploads a 100k triangle mesh in 1.08 ms and `Upload.share_100k` shares one in
0.31 ms. `mesh3.png` is the scene; the other baselines stay bit identical.

Left out: per vertex colors are kept but not uploaded, because
`BOX_VERTEX_FLOATS` has no color channel; freed handles are retired, not reused;
the repository README does not mention the scene, which this task does not own.
