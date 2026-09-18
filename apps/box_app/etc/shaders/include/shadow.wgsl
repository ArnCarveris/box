#pragma once

@group(0) @binding(2) var shadow_map: texture_depth_2d_array;
@group(0) @binding(3) var shadow_samp: sampler_comparison;

fn shadow_lookup(index: u32, world: vec3<f32>, gn: vec3<f32>) -> f32 {
  let p = u.shadow_matrix[index] * vec4(world + gn * u.shadow_offset[index], 1.0);
  let uv = p.xy * vec2(0.5, -0.5) + vec2(0.5);
  let depth = p.z + u.shadow_bias[index];
  var sum = 0.0;
  var taps = 0.0;
  let half_size = i32(u.shadow_filter) / 2;
  for (var y = -half_size; y <= half_size; y = y + 1) {
    for (var x = -half_size; x <= half_size; x = x + 1) {
      let o = vec2<f32>(f32(x), f32(y)) * u.shadow_texel;
      sum = sum + textureSampleCompareLevel(shadow_map, shadow_samp, uv + o,
        index, depth);
      taps = taps + 1.0;
    }
  }
  var result = sum / max(taps, 1.0);
  let outside = any(uv < vec2(0.0)) || any(uv > vec2(1.0)) || p.z < 0.0 || p.z > 1.0;
  if (outside) {
    result = 1.0;
  }
  return result;
}

fn shadow_factor(world: vec3<f32>, gn: vec3<f32>) -> f32 {
  var result = 1.0;
  if (u.shadow_cascades != 0u) {
    let depth = (u.view * vec4(world, 1.0)).z;
    var index = u.shadow_cascades - 1u;
    for (var i = 0u; i < u.shadow_cascades; i = i + 1u) {
      if (depth < u.shadow_split[i]) {
        index = i;
        break;
      }
    }
    result = shadow_lookup(index, world, gn);
    let end = u.shadow_split[index];
    let start = select(0.0, u.shadow_split[max(index, 1u) - 1u], index > 0u);
    let band = (end - start) * 0.1;
    if (index + 1u < u.shadow_cascades && depth > end - band) {
      let t = clamp((depth - (end - band)) / max(band, 1e-4), 0.0, 1.0);
      result = mix(result, shadow_lookup(index + 1u, world, gn), t);
    }
    let stop = u.shadow_distance * (1.0 - u.shadow_fade);
    let fade = clamp((depth - stop) / max(u.shadow_distance - stop, 1e-4), 0.0, 1.0);
    result = mix(result, 1.0, fade);
  }
  return result;
}
