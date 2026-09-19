# Shape registry

Built: `Shape` is a registration component that names a shape component, a key
function and a build function. Setting it installs an OnSet observer for that
component; the observer splits every value into a cache key and an instance
scale, asks the mesh cache for the mesh of that key and writes `RenderMesh`, so
a project shape shares meshes and joins the instanced draw like a builtin.
`box_shape_key_t` and the mesh builder are the public surface; removing or
deleting the registration takes its observer away and leaves its meshes alone.

Verified: the `Shape` suite of `test/geometry`, 13 cases over registering from
outside the engine with a `Fan` shape, upload, sharing one key, changing a
parameter, changing only the scale, prefab instances, unregistering, deleting
and re-registering; 322 tests pass over `geometry` and `render`, lint is clean.
`custom_shape.png` shows the scene; the other baselines stay bit identical.

Left out: the scene needs a shape the engine registers, because box_app links
only `box`, so the flat `Star` of `src/geometry/shape_example.c` ships as the
worked example beside the test's own shape. The repository README does not
mention the registry or the scene, which this task does not own.
