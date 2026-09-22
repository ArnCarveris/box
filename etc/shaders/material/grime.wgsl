fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let n = (*s).geo_normal;
  let low = saturate(-n.y + (1.0 - (*s).local.y));
  let k = saturate(p.a.w * material_layer_amount(s, BOX_LAYER_INDEX) * low *
    material_layer_noise(s, p.b.x));
  (*s).albedo = mix((*s).albedo, (*s).albedo * p.a.rgb, k);
  (*s).roughness = mix((*s).roughness, p.b.y, k);
}
