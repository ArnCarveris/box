# Spline meshes

Extruding a cross section along a spline in the vertex shader, so roads need no generated meshes.

## Details

A SplineMesh component binds a spline, a cross section profile and a segment count; the renderer uploads the control points and the profile as buffers and the vertex shader places a template strip along the curve, so editing the spline is a buffer update rather than a mesh rebuild. Bounds for culling are computed from the spline. The same mechanism covers fences, cables and pipes, which is a happy coincidence worth keeping in mind.

Verification: unit tests cover bounds, segment counts, profile and control point upload and runtime edits. Visual scenes render a road network and a case that edits the spline while running, and a benchmark compares a thousand spline meshes with the equivalent generated meshes.
