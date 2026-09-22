#pragma once

fn large_enough(base: u32, center: vec3<f32>, clip: mat4x4<f32>, pixels: vec4<f32>) -> bool {
  if (pixels.z <= 0.0) {
    return true;
  }
  let half = vec3<f32>(bitcast<f32>(src[base + u32(INSTANCE_HALF_EXTENT)]),
    bitcast<f32>(src[base + u32(INSTANCE_HALF_EXTENT) + 1u]),
    bitcast<f32>(src[base + u32(INSTANCE_HALF_EXTENT) + 2u]));
  if (all(half == vec3(0.0))) {
    return true;
  }
  let x = vec3<f32>(bitcast<f32>(src[base]), bitcast<f32>(src[base + 4u]),
    bitcast<f32>(src[base + 8u])) * half.x;
  let y = vec3<f32>(bitcast<f32>(src[base + 1u]), bitcast<f32>(src[base + 5u]),
    bitcast<f32>(src[base + 9u])) * half.y;
  let z = vec3<f32>(bitcast<f32>(src[base + 2u]), bitcast<f32>(src[base + 6u]),
    bitcast<f32>(src[base + 10u])) * half.z;
  var lo = vec2(1e30);
  var hi = vec2(-1e30);
  for (var i = 0u; i < 8u; i = i + 1u) {
    let corner = center + select(-x, x, (i & 1u) != 0u) +
      select(-y, y, (i & 2u) != 0u) + select(-z, z, (i & 4u) != 0u);
    let point = clip * vec4(corner, 1.0);
    if (point.w <= 1e-6) {
      return true;
    }
    let ndc = point.xy / point.w;
    lo = min(lo, ndc);
    hi = max(hi, ndc);
  }
  let span = (hi - lo) * pixels.xy * 0.5;
  return min(span.x, span.y) >= pixels.z;
}
