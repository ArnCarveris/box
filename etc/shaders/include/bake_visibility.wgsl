#pragma once

fn bake_intersect(id: u32, origin: vec3<f32>, dest: vec3<f32>) -> bool {
  let base = bake_data[3] + id * 12u;
  let x = bitcast<vec4<f32>>(vec4(bake_data[base], bake_data[base + 1u], bake_data[base + 2u], bake_data[base + 3u]));
  let y = bitcast<vec4<f32>>(vec4(bake_data[base + 4u], bake_data[base + 5u], bake_data[base + 6u], bake_data[base + 7u]));
  let z = bitcast<vec4<f32>>(vec4(bake_data[base + 8u], bake_data[base + 9u], bake_data[base + 10u], bake_data[base + 11u]));
  let start = vec3(dot(x, vec4(origin, 1.0)), dot(y, vec4(origin, 1.0)), dot(z, vec4(origin, 1.0)));
  let direction = dest - origin;
  let delta = vec3(dot(x.xyz, direction), dot(y.xyz, direction), dot(z.xyz, direction));
  let moving = abs(delta) >= vec3(1e-8);
  let inverse = 1.0 / select(vec3(1.0), delta, moving);
  let a = (-1.0 - start) * inverse;
  let b = (1.0 - start) * inverse;
  let first = select(vec3(-1e30), min(a, b), moving);
  let last = select(vec3(1e30), max(a, b), moving);
  let near = max(0.0, max(first.x, max(first.y, first.z)));
  let far = min(1.0, min(last.x, min(last.y, last.z)));
  return all(moving | (abs(start) <= vec3(1.0))) && near <= far && far > 1e-5 && near < 0.99999;
}

fn bake_visible_ignoring(origin: vec3<f32>, dest: vec3<f32>, ignore: u32) -> bool {
  let size = bake_spacing();
  var coord = vec3<i32>(floor(origin / size - vec3(0.5)));
  let end = vec3<i32>(floor(dest / size - vec3(0.5)));
  let delta = dest - origin;
  let step = select(vec3<i32>(1), vec3<i32>(-1), delta < vec3(0.0));
  let moving = abs(delta) > vec3(1e-8);
  let safe = select(vec3(1.0), delta, moving);
  let stride = select(vec3(1e30), vec3(size) / abs(safe), moving);
  let boundary = (vec3<f32>(coord) + vec3(0.5) + select(vec3(0.0), vec3(1.0), delta > vec3(0.0))) * size;
  var next = select(vec3(1e30), (boundary - origin) / safe, moving);
  let difference = vec3<u32>(abs(end - coord));
  let visits = 1u + difference.x + difference.y + difference.z;
  var cached_coord = vec3<i32>(0x7fffffff);
  var chunk = 0xffffffffu;
  for (var i = 0u; i < visits; i ++) {
    let chunk_coord = vec3<i32>(floor(vec3<f32>(coord) / 4.0));
    if (any(chunk_coord != cached_coord)) {
      chunk = bake_chunk(chunk_coord);
      cached_coord = chunk_coord;
    }
    if (chunk != 0xffffffffu) {
      let local = vec3<u32>(coord - chunk_coord * 4);
      let p = bake_data[bake_chunk_offset(chunk) + 11u] +
        (local.x + local.y * 4u + local.z * 16u) * 2u;
      let offset = bake_data[p];
      let count = bake_data[p + 1u];
      for (var j = 0u; j < count; j ++) {
        let id = bake_data[offset + j];
        if (id != ignore && bake_intersect(id, origin, dest)) { return false; }
      }
    }
    var a = select(1u, 0u, next.x < next.y);
    a = select(2u, a, next[a] < next.z);
    coord[a] += step[a];
    next[a] += stride[a];
  }
  return true;
}

fn bake_visible(origin: vec3<f32>, dest: vec3<f32>) -> bool {
  return bake_visible_ignoring(origin, dest, 0xffffffffu);
}
