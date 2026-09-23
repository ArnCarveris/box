fn box_material_layer(s: ptr<function, Surface>, p: LayerParams) {
  let amount = saturate(p.a.w * material_layer_amount(s, BOX_LAYER_INDEX));
  if (amount <= 0.0) {
    return;
  }
  let breakup = saturate(p.b.w);
  let origin = material_layer_origin(s);
  let seed = material_layer_seed(s);
  let frequency = max(p.b.z, 0.0) * (*s).layer_data.scale / 6.0;
  var edge = smoothstep(4.0, 18.0, (*s).curvature);
  if ((*s).edge_bounds) {
    let axis = abs((*s).layer_normal);
    let face = max(axis.x, max(axis.y, axis.z));
    let excluded = axis >= vec3(face - 1e-3);
    let lim = select((*s).layer_extent * 0.5, vec3(1e9), excluded);
    let border = select(min((*s).local, vec3(1.0) - (*s).local) *
      (*s).layer_extent, vec3(1e9), excluded);
    let d = max(min(border.x, min(border.y, border.z)), 0.0);
    let reach = min(lim.x, min(lim.y, lim.z)) * 0.5;
    let width = max(max(p.b.x, 0.0), (*s).footprint);
    let band_max = max(min(width * (1.0 + 0.66 * breakup), reach), 1e-4);
    if (d < band_max) {
      let bite = material_noise_triplanar_value(origin * (7.4 * frequency),
        (*s).layer_normal, seed + 977u);
      let band = min(width * mix(1.0, 0.35 + 1.3 * bite, breakup), reach);
      let feather = max(band * 0.35, (*s).footprint * 0.8);
      edge = max(edge, 1.0 - smoothstep(max(band - feather, 0.0),
        max(band, 1e-4), d));
    }
  }
  if (edge <= 0.0) {
    return;
  }
  let noise = material_noise_triplanar_value(origin * (2.6 * frequency),
    (*s).layer_normal, seed + 641u);
  let field = saturate((noise - 0.5) * 1.9 + 0.5);
  let cover = amount * mix(1.0, 0.5, breakup);
  let cut = 1.0 - cover;
  let gate = smoothstep(cut - 0.1, cut + 0.1, field);
  let chipped = saturate(edge * gate) * 0.85;
  let lum = dot((*s).albedo, vec3(0.2126, 0.7152, 0.0722));
  let deep = smoothstep(0.36, 0.44, lum);
  let bare = mix(p.a.rgb, (*s).albedo * 0.22, deep);
  (*s).albedo = mix((*s).albedo, bare, chipped);
  (*s).roughness = mix((*s).roughness, mix(saturate(p.b.y), 0.85, deep),
    chipped * 0.85);
  (*s).metallic = mix((*s).metallic, mix(0.25, 0.05, deep), chipped * 0.85);
}
