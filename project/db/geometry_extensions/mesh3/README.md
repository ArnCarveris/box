# Mesh3 custom meshes

A Mesh3 component that uploads application authored vertex data as a mesh like any primitive.

## Details

Mesh3 holds vertices, normals, uvs, indices and optional per vertex colors; setting it registers a mesh with the render module and writes RenderMesh so the entity joins the instanced path used by primitives. Per vertex color multiplies albedo and its alpha multiplies roughness, as the datamodel describes. Meshes are reference counted, freed when the last user goes away and re-uploaded when the component is set again.

Verification: unit tests in test/geometry cover upload, bounds computation, index validation, sharing between entities, replacing the data and freeing. A visual scene renders a script authored mesh next to primitives, and a benchmark measures the upload cost of a 100k triangle mesh.
