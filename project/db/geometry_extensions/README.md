# Custom meshes, 2D shapes and splines

Mesh3 custom meshes, a shape registry for projects, the remaining 2D shapes, glTF import and a reusable spline framework.

## Details

Content authoring gains everything the brief lists that scene_core left out. Mesh3 uploads application authored vertex data through the same instanced path as primitives, a registration point lets a project add its own scalable shape without patching the engine, and the 2D set is completed with Circle, Border with per corner radii, Line and Image showing a region of a texture. glTF import turns a file into entities, meshes and materials so external content can be mixed with procedural geometry.

The spline framework is the second half of the epic: a Spline entity with control points and arc length sampling, a vertex shader extrusion that builds roads, fences and cables without generating meshes, and path following for animation and for particle emitter shapes. Verification is unit tests per component in test/geometry, visual scenes for the 2D sheet, a glTF fixture, a road network and an animated path, and benchmarks for mesh upload, spline sampling and a thousand spline meshes. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Mesh3, the shape registration component, Circle, Border, Line, Image, Gltf, Spline, SplineMesh and FollowPath.

## References

- design/DESIGN.md, section Content (shapes, splines)
- design/datamodel/geometry.flecs and design/datamodel/gltf.flecs
