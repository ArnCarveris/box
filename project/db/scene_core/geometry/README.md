# Geometry module

Shape components that select cached meshes and per instance scale.

## Details

Every shape component (Box, Quad, Rect, Triangle, RightTriangle, TrianglePrism, RightTrianglePrism, NGon, Sphere, HemiSphere, IcoSphere, Cone, Cylinder, Torus, Capsule, Tube, Frustum, RoundedBox) has an OnSet observer that builds a mesh key from the topology parameters and writes `RenderMesh` with the cached handle, the instance scale and the local bounds. The mesh cache generates a mesh on first use and hands it to the render module, which uploads it with the next packet. Two spheres with the same segment count share one mesh regardless of radius.
