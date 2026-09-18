# Texture loading and PBR texture slots

`box.texture` adds `Texture {path, width, height}`. Setting it loads the file with
stb_image relative to the working directory, box filters it to a square size class,
builds the RGBA8 mip chain on the CPU and queues a `box_texture_cmd_t` in the packet;
the renderer frees the pixels after the upload. `Surface` gained `anisotropy`,
`texture_quality`, `keep_source_textures` and the declared-only `texture_compression`
and `texture_cache`. `PbrTextures {albedo, emissive, roughness, normal}` is inherited
by instances; extract resolves each slot to the handle of the slot entity's
`RenderTexture`, a reverse map re-extracts dependents on reload and deleting a bound
texture warns and unbinds the slots.

Size classes are 256, 512, 1024 and 2048; an image lands in the largest class not
above its size, at least 256 and at most `texture_quality`. Each class is one
`texture_2d_array` that grows by recreating and copying, with one sampler carrying the
anisotropy setting; all four share bind group 2 with the environment, because the
device limit is four bind groups. A handle is class plus layer, packed into 16 bits
per slot and two words per instance, so the instance grew 96 to 104 bytes and extract
matches the lighting baseline (0 regressions). `Textures.extract_1000_materials`
costs 83 us for 1000 materials over 20 textures, the bench scenes still run at 737 fps
and the wasm build links with the `etc` folder embedded at `/etc`.

Limitations: textures are square and stretched to their class, the size class never
shrinks its layer count, `texture_compression` and `texture_cache` do nothing, mips
are box filtered in byte space, and tangents come from screen space derivatives.
