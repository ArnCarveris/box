# Splines

A reusable spline framework with arc length sampling, vertex shader extrusion for roads, and path following.

## Details

A Spline entity carries control points and is evaluated both by parameter and by arc length, with a cached length table so sampling stays cheap. A vertex shader extrusion sweeps a cross section along the spline, which builds roads, fences and cables without generating meshes. Path following moves entities along a spline for animation and gives particle emitters a shape.

Verification is unit tests per component in test/geometry, visual scenes of a road network and an animated path, and benchmarks for spline sampling and for a thousand spline meshes. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Spline, SplineMesh and FollowPath.

## References

- design/DESIGN.md, section Content (splines)
- design/datamodel/geometry.flecs
