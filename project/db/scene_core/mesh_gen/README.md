# Mesh generators

Procedural meshes with normals and UVs for every primitive shape.

## Details

`src/geometry/mesh_gen.h` declares 17 generators producing separate position, normal and uv streams, uint32 triangle indices and local bounds. Meshes are unit sized where a shape is scaled per instance, so every box shares one mesh and a sphere's radius lives in the instance scale. Ratios that change the topology (torus tube, capsule length, tube thickness and arc, frustum top radius, rounded box dimensions) are baked into the mesh key.

Smooth variants share vertices with averaged normals, flat variants duplicate vertices per face. 32 tests check counts, index bounds, unit normals, UV ranges, bounds and that triangle winding agrees with the normals.
