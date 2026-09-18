# Blending and translucency

- `BlendMode` (nine modes, Opaque default) and `BackFaceMode` (textured, plain,
  cull default) are inherited enum components of `box.material`.
- An instance leaves the opaque batches for a per frame sorted list when its
  blend mode is not Opaque, when `Opacity` times the `ActualTint` alpha is below
  one, or when it shows back faces. Extract sorts that list back to front by
  view space depth with four radix passes; the renderer draws it after the
  opaque pass, one lazily built pipeline per blend mode and cull mode, depth
  test on and depth write off except for the two sided opaque draws.
- Milk glass: after the opaque pass the target is copied into a texture in the
  target format and its mip chain is filled by halving passes; the alpha blended
  modes sample it at `roughness * min(mips, 5)` and take `roughness^2` of their
  background from it, premultiplied over, so roughness 0 is plain alpha blending.
  The copy costs one full screen copy plus about ten small passes per frame with
  a transparent object in view.
- Texture alpha is a cutout below 0.5 in the opaque pass and a multiplier of the
  alpha in the blended modes; no loader flag decides the pass.
- 16 `Blending` cases (`tests.json`), 164 render tests and 419 in total green.
  `Blending.transparent_10k` extracts and sorts 10k instances in 462 us; nothing
  in `bench.json` regresses, `Extract.dynamic_10k` is 2% faster.
- `blending.png`, `milk_glass.png` and `back_faces.png` are the visual checks;
  the seven untextured scenes of the textures epic still render bit identical.
