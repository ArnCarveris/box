# 2D shapes

- `box.geometry` adds `Circle`, `Border`, `Line` and `Image`. A circle is a disc whose
  segment count follows its radius in steps of eight, so nearby radii share the ngon mesh
  of the primitives; a line divides its endpoints and thickness by the largest of them, so
  one outline is one mesh at any size.
- `Image` adds `(With, Rect)` and bakes its uv region, `ImageFit` and rotation into the
  quad uvs. `Cover` crops and `Contain` widens the region against the pixel size of the
  texture, which it re-reads when a texture finishes uploading; the texture itself binds
  through `PbrTextures` like any other material.
- `Border` adds `(With, Rect)` and carries no geometry. It resolves its shorthands into
  the private `RenderShape2D`; extract packs the radii and widths as ratios of the smaller
  half extent, the color and the aspect into four new instance words and sets bit 15 of
  the mode word. `include/shape2d.wgsl` rebuilds the rounded rectangle from the normalized
  local position already in `Surface`, so changing a radius uploads an instance and builds
  no mesh, and a draw without the bit is untouched.
- 465 tests green (96 geometry with the new `Shapes2D` and `Coverage`, 369 render with
  `Shapes2D`), lint 0, the 59 existing visual baselines bit identical and `shapes_2d.png`
  shows the sheet. `Extract.dynamic_10k` is 224.5 us against 224.0 us and the static cases
  are 5% faster, so the instance growing 140 to 156 bytes costs nothing measurable.
- Left out: the shadow pass still casts the full rectangle of a rounded shape, and an
  opaque border has a hard edge where a blended one antialiases.
