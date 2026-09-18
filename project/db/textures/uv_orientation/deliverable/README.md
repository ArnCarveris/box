# Image orientation matches mesh UVs

`box_texture_image` flips the rows of level 0 before it builds the mip chain, so the first published
row is the bottom row of the source and uv v = 0 is the bottom of the artwork, the convention the mesh
generators follow. Image files and rasterized SVGs both go through it and its prototype says so.

`Textures.row_zero_is_the_bottom_of_the_file` loads a two row PNG, red over blue, and
`Svg.row_zero_is_the_bottom_of_the_image` rasterizes an SVG with the same halves; both assert that the
published row 0 is blue. `svg_icons.flecs` dropped the 180 degree rotation of its quads and its icons
are upright; `textured.png` has the checker and the roughness ramp upright and the bumps lit from above
like the sphere, so `bumps_normal.png` was not regenerated.

Those bumps are still lit from the left while the sun is on the right: `perturb` builds its frame from
`dpdy`, whose y points down in WebGPU, which negates both tangents. That predates this task.
