fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let amount = p.a.w * material_layer_amount(s, BOX_LAYER_INDEX);
  if (amount <= 0.0) {
    return;
  }
  let n = (*s).geo_normal;
  if (n.y <= 0.0) {
    return;
  }
  let up = pow(n.y, max(p.b.z, 0.001));
  let broad = material_layer_noise(s, p.b.x * 0.45);
  let fine = material_layer_noise(s, p.b.x * 1.7);
  let coverage = 0.25 + 0.75 * mix(broad, fine, 0.25);
  let k = saturate(amount * up * coverage);
  (*s).albedo = mix((*s).albedo, p.a.rgb, k);
  (*s).roughness = mix((*s).roughness, p.b.y, k);
  (*s).metallic = mix((*s).metallic, 0.0, k);
}
