  if (id.x >= c.range.y) {
    return;
  }
  let base = (c.range.x + id.x) * WORDS;
  let center = vec3<f32>(bitcast<f32>(src[base + BOUNDS]),
    bitcast<f32>(src[base + BOUNDS + 1u]), bitcast<f32>(src[base + BOUNDS + 2u]));
  let radius = bitcast<f32>(src[base + BOUNDS + 3u]);
  if (!in_frustum(center, radius)) {
    return;
  }
