#pragma once

// Encoding of the second attachment of the mesh pass. Its four float channels
// hold the world normal octahedrally, the fraction of the pixel that came from
// indirect diffuse light, and the indirect specular fraction packed together
// with the roughness of the surface, which is what a post process pass needs to
// change the indirect light of a pixel without touching the direct light.

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

// Levels the indirect specular fraction and the roughness are quantised to.
// Their packed value stays a whole number below 2048, which a half float holds
// exactly, so both come back out of one channel unchanged.
const GB_SPECULAR_LEVELS: f32 = 63.0;
const GB_ROUGHNESS_LEVELS: f32 = 31.0;
const GB_SPECULAR_SCALE: f32 = 32.0;

fn gb_pack(specular: f32, roughness: f32) -> f32 {
  let s = floor(clamp(specular, 0.0, 1.0) * GB_SPECULAR_LEVELS + 0.5);
  let r = floor(clamp(roughness, 0.0, 1.0) * GB_ROUGHNESS_LEVELS + 0.5);
  return s * GB_SPECULAR_SCALE + r;
}

fn gb_specular(packed: f32) -> f32 {
  return floor(packed / GB_SPECULAR_SCALE) / GB_SPECULAR_LEVELS;
}

fn gb_roughness(packed: f32) -> f32 {
  let s = floor(packed / GB_SPECULAR_SCALE);
  return (packed - s * GB_SPECULAR_SCALE) / GB_ROUGHNESS_LEVELS;
}
