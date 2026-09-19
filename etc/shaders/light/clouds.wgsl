// Cloud shadows: a scrolling deck of tiled noise that darkens a light.
//
// A light modulator plugin is a WGSL file that defines box_light_modulate and
// reads its parameters out of the block the engine writes from the component
// the plugin registered, here a CloudShadows {scale, speed, density, softness}.
// The engine splices this file into the mesh shader once per modulated light,
// which is why the shared include is skipped while it is being spliced: the
// mesh shader already carries it.
#ifndef BOX_MODULATOR_SPLICE
#include "../include/light_modulator.wgsl"
#endif

fn box_light_modulate(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>, time: f32,
    params: LightModulatorParams) -> vec3<f32> {
  let scale = select(40.0, params.a.x, params.a.x > 0.0);
  let speed = select(4.0, params.a.y, params.a.y != 0.0);
  let density = clamp(params.a.z, 0.0, 1.0);
  let softness = clamp(select(0.35, params.a.w, params.a.w > 0.0), 0.02, 1.0);
  let drift = vec2(1.0, 0.35) * speed * time;
  let deck = box_modulator_fbm((p.xz + drift) / max(scale, 1e-3));
  let lit = smoothstep(0.5 - softness * 0.5, 0.5 + softness * 0.5, deck);
  return vec3(1.0 - density * (1.0 - lit));
}
