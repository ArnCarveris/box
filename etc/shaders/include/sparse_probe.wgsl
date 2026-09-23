#pragma once

fn bake_spacing() -> f32 {
  return max(bitcast<f32>(bake_data[0]), 0.0001);
}

fn bake_chunk(coord: vec3<i32>) -> u32 {
  let directory = bake_data[6];
  if (directory != 0u) {
    let lo = bitcast<vec3<i32>>(vec3(bake_data[directory], bake_data[directory + 1u], bake_data[directory + 2u]));
    let dim = vec3(bake_data[directory + 3u], bake_data[directory + 4u], bake_data[directory + 5u]);
    let local = vec3<u32>(coord) - vec3<u32>(lo);
    if (any(coord < lo) || any(local >= dim)) { return 0xffffffffu; }
    return bake_data[directory + 6u + local.x + dim.x * (local.y + dim.y * local.z)];
  }
  let count = bake_data[1];
  if (count == 0u) { return 0xffffffffu; }
  let c = vec3<u32>(coord);
  var slot = ((c.x * 73856093u) ^ (c.y * 19349663u) ^ (c.z * 83492791u)) & (count - 1u);
  for (var i = 0u; i < count; i ++) {
    let p = 8u + slot * 4u;
    let id = bake_data[p + 3u];
    if (id == 0u) { return 0xffffffffu; }
    if (all(vec3<i32>(bitcast<i32>(bake_data[p]), bitcast<i32>(bake_data[p + 1u]),
      bitcast<i32>(bake_data[p + 2u])) == coord)) { return id - 1u; }
    slot = (slot + 1u) & (count - 1u);
  }
  return 0xffffffffu;
}

fn bake_chunk_offset(chunk: u32) -> u32 {
  return bake_data[2] + chunk * 12u;
}

fn bake_probe_position(chunk: u32, probe: u32) -> vec3<f32> {
  let p = bake_chunk_offset(chunk);
  let coord = vec3<i32>(bitcast<i32>(bake_data[p]), bitcast<i32>(bake_data[p + 1u]), bitcast<i32>(bake_data[p + 2u]));
  let local = vec3<u32>(probe & 3u, (probe >> 2u) & 3u, (probe >> 4u) & 3u);
  return (vec3<f32>(coord) * 4.0 + vec3<f32>(local) + vec3(0.5)) * bake_spacing();
}
