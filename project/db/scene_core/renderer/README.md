# Instanced renderer

Mesh and instance buffers, per mesh instanced draws and basic lit shading.

## Details

The renderer keeps a mesh table indexed by handle with vertex, index and static instance buffers, grows static buffers on the GPU with a buffer copy, patches changed slots with `wgpuQueueWriteBuffer` and uploads the dynamic instances into one shared buffer per frame. Each mesh with instances is one instanced indexed draw for its static range and one per dynamic batch.

The WGSL shader reconstructs the world matrix from three instance rows, shades with a directional light, a Blinn Phong specular term scaled by roughness and metallic, hemispherical ambient and emissive, and gamma encodes when the target is not an sRGB format. Front faces are clockwise because the world is left handed with Z forward. `primitives.png` is the headless render of the demo scene.
