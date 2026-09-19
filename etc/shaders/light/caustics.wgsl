// Caustics: the bright bands a water surface focuses onto what is under it.
//
// The second shipped plugin, and the one a project copies for its own: it is
// the same file layout as the cloud deck with a different function body and a
// Caustics {scale, speed, intensity, depth} of its own, so a plugin that neither
// ships with the engine nor changes it reads exactly like this.
#ifndef BOX_MODULATOR_SPLICE
#include "../include/light_modulator.wgsl"
#endif

fn box_light_modulate(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>, time: f32,
    params: LightModulatorParams) -> vec3<f32> {
  let scale = select(2.0, params.a.x, params.a.x > 0.0);
  let speed = select(1.0, params.a.y, params.a.y != 0.0);
  let intensity = select(1.0, params.a.z, params.a.z > 0.0);
  let surface = params.a.w;
  let q = p.xz / max(scale, 1e-3);
  let t = time * speed;
  var sum = 0.0;
  for (var i = 0u; i < 4u; i = i + 1u) {
    let fi = f32(i);
    let dir = vec2(cos(fi * 1.7 + 0.3), sin(fi * 2.3 + 1.1));
    sum = sum + sin(dot(q, dir) * 3.0 + t * (1.0 + fi * 0.3));
  }
  let bands = pow(max(sum * 0.25, 0.0), 3.0);
  let below = 1.0 - smoothstep(surface - 0.5, surface + 0.5, p.y);
  return vec3(1.0) + vec3(0.7, 1.0, 0.95) * bands * intensity * below;
}
