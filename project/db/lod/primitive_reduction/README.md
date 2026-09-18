# Primitive reduction

Automatic tessellation reduction for parametric shapes based on their projected size.

## Details

Spheres, cylinders, cones, tori, capsules, tubes, rounded boxes and spline meshes select a segment count from their projected error instead of the authored one, which stays the upper bound. Reduced variants reuse the existing mesh cache so instances at the same level share a mesh. Because the radius lives in the instance scale, changing level is a mesh handle change rather than a rebuild.

Verification: unit tests check the selected segment count against the metric for each shape, that meshes are shared at the same level and that the authored value caps the result. A visual test captures a field of spheres at several distances, and a benchmark reports the triangle count before and after.
