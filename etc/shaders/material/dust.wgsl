fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let n = (*s).geo_normal;
  let up = pow(saturate(n.y), max(p.b.z, 0.001));
  let k = saturate(p.a.w * material_layer_amount(s, BOX_LAYER_INDEX) * up *
    material_layer_noise(s, p.b.x));
  (*s).albedo = mix((*s).albedo, p.a.rgb, k);
  (*s).roughness = mix((*s).roughness, p.b.y, k);
}
