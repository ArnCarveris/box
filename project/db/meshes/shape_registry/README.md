# Shape registry

A registration point so a project can add its own scalable shape component without patching the engine.

## Details

Expose the mesh key and mesh cache of the geometry module through a registration component: a project creates a shape entity that names its component and its generator, and the module installs the OnSet observer that builds the key and writes RenderMesh. Cached meshes are shared between shapes with the same key exactly as the builtin shapes are, so a project shape gets instancing and LOD for free. One example shape lives in the test project and documents the extension point.

Verification: unit tests register a custom shape, instantiate several with different parameters, check mesh sharing, change a parameter and unregister the shape. The example shape is also used in a visual scene so the whole path is exercised.
