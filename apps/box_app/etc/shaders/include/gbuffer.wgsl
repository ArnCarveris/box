#pragma once

// Octahedral mapping of a unit vector onto the square, which keeps a world
// normal in two channels and leaves the other two of the attachment free.
fn oct_encode(n: vec3<f32>) -> vec2<f32> {
  let p = n / max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
  var r = p.xy;
  if (p.z < 0.0) {
    let s = select(vec2(-1.0), vec2(1.0), p.xy >= vec2(0.0));
    r = (vec2(1.0) - abs(vec2(p.y, p.x))) * s;
  }

  return r;
}

fn oct_decode(e: vec2<f32>) -> vec3<f32> {
  var n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
  if (n.z < 0.0) {
    let s = select(vec2(-1.0), vec2(1.0), n.xy >= vec2(0.0));
    let xy = (vec2(1.0) - abs(vec2(n.y, n.x))) * s;
    n = vec3(xy.x, xy.y, n.z);
  }

  return normalize(n);
}
