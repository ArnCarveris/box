# Texture loading and PBR texture slots

A Texture component that loads an image file and PbrTextures slots that bind textures to a material.

## Details

A box.texture module gains Texture with a path and an optional target size; setting it loads the image, sends a resource command to the renderer and frees the CPU copy unless Surface.keep_source_textures is set. PbrTextures names a texture entity per slot for albedo, emissive, roughness and normal and is inherited by instances. Sampler state follows the Surface settings for anisotropy, texture quality, compression and the on disk cache. Instances carry texture handles in their material data so instanced batching still works.

Verification: unit tests cover loading, resizing, reference counting, replacing a path, unloading and the inherited slots. A visual scene renders a textured box, a textured sphere and a normal mapped surface, and a benchmark measures the extract cost of a scene with 1000 textured materials.

Introduces Texture and PbrTextures plus the texture resource commands in the frame packet.
