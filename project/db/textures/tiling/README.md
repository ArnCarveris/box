# Texture transforms and tiling

Texture transforms, wrap modes and world space tiling so one material covers differently sized primitives consistently.

## Details

TextureTransform scales and offsets UVs per material. TextureTiling derives UVs from world size instead of the mesh parametrisation using the box, cylinder, radial and meters projections from the datamodel, with fit, partial tiles and a minimum tile count. TextureWrap selects repeat, clamp or mirror per material and maps onto the sampler.

Verification: unit tests compute the expected UVs of each projection on scaled primitives and cover fit, allow_partial and min_tiles. A visual scene tiles one texture over boxes and cylinders of very different sizes, including a case that changes the tiling at runtime to prove it is data driven.
