# glTF import

A Gltf component that loads a glTF file into entities, meshes and materials.

## Details

Setting Gltf on an entity parses the file and creates a child hierarchy with transforms, Mesh3 meshes, materials with their PBR textures and, where present, lights and cameras. It stays data driven: removing the component removes the generated children and setting another path replaces them. Large files are parsed and uploaded through the job system so a load does not spike a frame.

Verification: unit tests load a small glTF fixture checked into the test project and verify the hierarchy, mesh and material counts, texture binding and unloading. A visual scene renders the fixture, and a benchmark loads a larger model and reports the load time and how many frames it was spread over.
