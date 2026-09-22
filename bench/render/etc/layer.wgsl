fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let amount = material_layer_amount(s, BOX_LAYER_INDEX) * p.a.x;
  (*s).roughness = mix((*s).roughness, p.a.y, amount);
}
