fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let n = (*s).geo_normal;
  let pool = mix(1.0, saturate(n.y), p.b.y);
  let noise = mix(1.0, material_layer_noise(s, p.b.x), p.b.z);
  let k = saturate(p.a.w * material_layer_amount(s, BOX_LAYER_INDEX) * pool * noise);
  (*s).albedo = (*s).albedo * (1.0 - 0.5 * k);
  (*s).roughness = mix((*s).roughness, p.a.x, k);
}
