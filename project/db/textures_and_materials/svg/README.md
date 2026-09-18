# SVG textures

An Svg component that rasterizes an SVG file or inline SVG data into a texture.

## Details

Svg holds a file path or inline data plus a target size and produces the same texture handle as Texture, so anything that takes a texture entity takes an SVG. A small vendored rasterizer does the work and re-rasterizes when the size or the data changes. Rasterization is synchronous here; responsiveness/svg_jobs moves it onto the job system afterwards.

Verification: unit tests rasterize a known SVG fixture and check the size and a set of sampled pixels, cover inline data, resizing and unloading. A visual scene renders SVG icons on quads at several sizes to show resolution independence.
