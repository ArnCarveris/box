# More than 256 textures per size class

A size class is one array texture capped at 256 layers and exceeding it aborts in wgpu; a class must span several arrays so a scene can hold thousands of textures.

## Details

The SVG storm scene had to stop at 240 textures because the 256 layer limit of an array texture is the whole capacity of a size class, and layer 257 aborts inside wgpu instead of failing gracefully. A class becomes a list of array textures of up to 256 layers each; the packed slot index gains array bits, the shader selects the array by index (a small fixed maximum such as 8 arrays per class, bound as an array of textures if the device allows, else a switch), allocation fills arrays in order and releases layers back, and running out of arrays logs one error and falls back to the constant color instead of aborting.

Verification: a unit test allocating 600 textures of one class and checking their packed indices and release, the SVG storm scene raised to a thousand textures with its baseline, and the texture extract benchmark unchanged.

## References

- `project/db/responsiveness/svg_jobs/deliverable/README.md`
- `src/renderer/texture.c`, `src/texture/`
