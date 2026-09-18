# More than 256 textures per size class

A size class is one array texture capped at 256 layers and exceeding it aborts in wgpu; a class must span several arrays so a scene can hold thousands of textures.

## Details

The SVG storm scene had to stop at 240 textures because the 256 layer limit of an array texture is the whole capacity of a size class, and layer 257 aborts inside wgpu instead of failing gracefully. flecs-engine solved this with a pager (`src/modules/renderer/render_texture_arrays.c` in the recovered checkout): a class is a bucket holding a list of pages, each an array texture of at most the device's layer limit; `texturePager_assign` puts a texture on the first page with room and opens a page when none fits, `release` frees a layer back, a layout hash decides when a bucket's bind group must be rebuilt, empty layers are filled with a fallback so sampling never reads garbage, and per bucket statistics are recorded. Take those parts: pages per class with the layer limit queried from the device, the packed slot index gaining page bits, the shader selecting the page (a binding array of textures per class where the device allows it, else a fixed small maximum with a switch), release and reuse of layers, the layout hash driven bind group rebuild, the fallback fill, and running out of pages logging one error and falling back to the constant color instead of aborting. Its BC7 compression and on disk texture cache are the `texture_compression` and `texture_cache` surface settings declared but unimplemented in the textures epic; leave them for a later task but keep the pager shaped so they slot in.

Verification: a unit test allocating 600 textures of one class and checking their packed indices, page count and release, the SVG storm scene raised to a thousand textures with its baseline, and the texture extract benchmark unchanged.

## References

- flecs-engine `src/modules/renderer/render_texture_arrays.c`, `render_textures.h`, `texture_cache.h`
- `project/db/responsiveness/svg_jobs/deliverable/README.md`
- `src/renderer/texture.c`, `src/texture/`
