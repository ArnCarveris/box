fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let n = (*s).geo_normal;
  let low = saturate(-n.y + (1.0 - (*s).local.y));
  let broad = material_layer_noise(s, p.b.x * 0.5);
  let streaks = material_layer_noise_scaled(s, vec3(p.b.x * 2.0, p.b.x * 0.12, p.b.x * 2.0));
  let patches = smoothstep(0.22, 0.72, mix(broad, streaks, saturate(p.b.w)));
  let coverage = mix(low, 0.4 + 0.6 * low, saturate(p.b.z));
  let k = saturate(p.a.w * material_layer_amount(s, BOX_LAYER_INDEX) * coverage * patches);
  (*s).albedo = mix((*s).albedo, (*s).albedo * p.a.rgb, k);
  (*s).roughness = mix((*s).roughness, p.b.y, k);
  (*s).metallic = mix((*s).metallic, 0.0, k);
}
