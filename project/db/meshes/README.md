# Custom meshes

Mesh3 custom meshes, a shape registry so a project can add its own shape, and glTF import.

## Details

Content authoring gains the mesh half of what the brief lists that scene_core left out. Mesh3 uploads application authored vertex data through the same instanced path as the primitives, so a custom mesh behaves like any other renderable. A registration point lets a project add its own scalable shape component without patching the engine. glTF import turns a file into entities, meshes and materials, so external content can be mixed with procedural geometry.

Verification is unit tests per component in test/geometry, a visual scene built from a glTF fixture, and a benchmark for mesh upload. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Mesh3, the shape registration component and Gltf.

## References

- design/DESIGN.md, section Content (shapes)
- design/datamodel/geometry.flecs and design/datamodel/gltf.flecs
