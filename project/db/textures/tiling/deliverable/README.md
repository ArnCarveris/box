# Texture transforms and tiling

`box.material` gains `TextureTransform {scale_x, scale_y, offset_x, offset_y}` (identity
through a ctor), `TextureTiling {meters_x, meters_y, fit, allow_partial, min_tiles, shape}`
with `TilingShape` and the `TextureWrap` enum component, all inherited by instances.

`src/renderer/tiling.h` is the twin of the shader math. Extract takes the world size of an
instance (mesh bounds times its matrix column lengths), turns it into a span per projection
(box `size_x, size_y`, cylinder `pi size_x, size_y`, radial `size_x/2, pi size_x`, meters
`size_x, size_y`), divides by the tile size, rounds when `fit` is set and `allow_partial`
is not, raises to `min_tiles` and stores `tiles / span` (`tiles` for mesh) times the
transform scale. The fragment stage builds the base uv from the local position in meters
and the local normal: box picks the dominant normal axis, cylinder is `atan2(z, x) * r` by
height, radial `r` by `atan2(z, x) * r`, mesh uv otherwise; the wrap mode folds the result,
mirror with `1 - |t - 1|`. Box tiling does not blend faces, so a sphere shows three seams.

The instance grew 104 to 124 bytes (a `Float32x4` uv transform and a `Uint32` mode at
locations 10 and 11). A world without tiling keeps a `tiled` flag clear and skips the three
lookups, so `Extract.dynamic_10k` is 192 us against 195 us on the texture baseline (-1.7%);
`Tiling.extract_10k` costs 459 us for 10k tiled instances. 15 Tiling tests pass, the five
test projects and the lint are clean, the wasm build links, and `textured`, `pbr_spheres`,
`lights_city` and `baked_room` render bit identical. `tiling.png` tiles one checker at one
meter over boxes of 1, 2 and 6 meters, a cylinder and a sphere, with offset, mirror, clamp
and `fit` in front, where two boxes share a material and differ only in `TextureTiling`.
