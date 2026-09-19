# More than 256 textures per size class

A size class is no longer one array texture. `src/render/texture.c` holds a pager of eight pages,
each an array texture of at most the layers the device reports (256), and hands a texture the
first page of its class with a free layer, opening one when none has room; a released layer
returns to its page. The layer of a handle counts across the pages, so the page is the high bits
of the packed instance slot, and `etc/shaders/include/textures.wgsl` picks one of the eight page
bindings with a switch and one shared sampler, replacing the four per class bindings.
`src/renderer/texture.c` grows a page by copying, fills every layer no image landed in with opaque
white, and rebuilds the bind group when the hash of the page layout changes. Running out of pages
logs one error and hands back handle 0, so a material keeps its color instead of aborting in wgpu.
Capacity is 2048 textures.

236 render tests pass, three new: 600 textures of one class over three pages with their packed
indices, reuse of a released layer, and the out of pages error. All 43 visual scenes match their
baselines. `svg_storm.flecs` rasterizes 1000 icons (`svg_storm.png`), mean 0.171 and max 0.418 ms
over 300 frames (`svg_storm.json`). `Textures.extract_1000_materials` is unchanged: 114.6 us
against 115.5 to 117.3 us for the same benchmark built from the parent commit, both under the load
a parallel agent put on the machine. Left out: the binding array path (raising the sampled texture
limit needs `src/renderer/device.c`), per page statistics and BC7.
