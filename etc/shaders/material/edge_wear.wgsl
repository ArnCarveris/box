fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let d = min((*s).local, vec3(1.0) - (*s).local);
  let lo = min(min(d.x, d.y), d.z);
  let hi = max(max(d.x, d.y), d.z);
  let mid = d.x + d.y + d.z - lo - hi;
  let edge = 1.0 - smoothstep(0.0, max(p.b.x, 0.0001), mid);
  let k = saturate(edge * p.a.w * material_layer_amount(s, BOX_LAYER_INDEX) *
    material_layer_noise(s, p.b.z));
  (*s).albedo = mix((*s).albedo, p.a.rgb, k);
  (*s).metallic = mix((*s).metallic, 1.0, k);
  (*s).roughness = mix((*s).roughness, p.b.y, k);
}
