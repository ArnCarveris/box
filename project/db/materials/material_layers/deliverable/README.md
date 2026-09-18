# Material layers

- A layer is an entity with `MaterialLayer {wgsl, order}`, the body of
  `fn layer(s: ptr<function, Surface>, p: LayerParams)`, and `LayerParams {a, b}` with
  its scene global vectors. A material enables one with the inherited pair
  `(MaterialLayerOn, layer)`; the built ins live under `box.material.layers`, a project
  registers its own the same way and `MaterialLayerSettings` turns them off per view.
- Extract sorts the registrations by `order`, sends the whole list as `box_layer_cmd_t`
  when it changes, fills the 16 by two vec4 parameter array every frame and packs the
  enabled bits into the sixteen spare bits of the instance mode word, so the instance
  did not grow. A registry flag set by the first pair keeps a layerless scene out of it.
- The renderer recomposes the mesh WGSL from a head, one `layer_i` per registration, a
  generated `apply_layers` and the fragment stage, recreates the shader module and every
  mesh pipeline, and keeps the previous ones while logging once when it does not
  compile. A body works with `box_noise` and `Surface`: albedo, alpha, emissive,
  roughness, metallic, normal, geo_normal, world_pos and the bounds position `local`.
- Dust: `a` color and amount, `b` frequency, roughness, up facing sharpness. Edge wear:
  `a` metal color and amount, `b` band width, roughness, frequency. Grime: `a` albedo
  multiplier and amount, `b` frequency and roughness. Wetness: `a.x` wet roughness,
  `a.w` amount, `b` frequency, pooling, noise weight.
- 17 `Layers` cases, 181 render tests and 436 in total green.
  `Layers.extract_10k_with_layers` is 510 us against an unchanged 195 us `dynamic_10k`,
  nothing in `bench.json` moves beyond noise and `static_100k` runs at 1.47 ms and 682
  fps. `layers.png` shows the crate and sphere plain, dusty, worn, grimy, wet and all
  four; the twelve existing scenes are bit identical.
