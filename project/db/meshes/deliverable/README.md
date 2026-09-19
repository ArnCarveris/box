# Custom meshes: completion report

## What was built
- `Mesh3`: script authored vertices, normals, uvs, indices and per vertex colors with alpha, validated and completed on set (flat normals, zero uvs, sequential indices), hashed into a reference counted registry beside the primitive cache so identical data and prefab instances upload once, joining the instanced path through `RenderMesh`. Mesh upload, destroy and packet hand off moved into `src/render/mesh.c`.
- The shape registry: a `Shape` registration names a component, a key function and a build function; the module installs the OnSet observer that splits a value into a cache key and an instance scale and shares meshes by key exactly like the built in shapes, so a project shape gets instancing for free. A small `Star` ships as the example.
- glTF import through cgltf: a `Gltf {path, scene, scale}` component instantiates the node tree as child entities with transforms converted to box axes, one child per primitive with a cached `Mesh3`, materials mapped to `Rgb`, `Opacity`, `PbrMaterial`, `Emissive`, `BlendMode`, `BackFaceMode` and `PbrTextures` with file, data uri and glb images loaded through the texture module, reload replacing the tree and removal unloading it.
- The vertex format gained a packed RGBA8 color, versioned with a static assert; primitives write white, Mesh3 and glTF colors multiply the albedo and their alpha scales roughness, and `rgba` joined `box.types`.

## Verification
- 1127 unit tests across thirteen projects, all passing; `test/gltf` is new with 30 tests and `test/geometry` grew to 63.
- Scenes in `integration/`: `mesh3` (shared script authored meshes and a vertex alpha gradient), `custom_shape` (three stars and a built in NGon), `gltf` (an authored crate and the Khronos vertex colored box); every earlier scene bit identical.
- Benchmarks: a 100k triangle mesh uploads in 0.9 ms, a 500 node glTF loads in 9 ms; microbenchmarks in `bench/` against the sky epic: the same run as the weather epic since both closed on one tree state; the mesh upload and glTF cases are new.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- A C registered component with vectors needs its own ctor, dtor, copy and move hooks; flecs' runtime type hooks only cover script registered types.
- Keying the mesh registry on a hash of the data is what makes prefab instances and identical script meshes share an upload without any bookkeeping in script.
- The importer had to mirror one axis and flip v to land in the engine's handedness and uv convention; keeping the winding is what keeps front faces front.

## Not in this epic
- glTF cameras, lights, animation, skinning, morph targets, tangents, second uv sets and samplers are skipped, and the load is synchronous.
- Splines and 2D shapes are their own epics.
