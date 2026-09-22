#ifndef BOX_MODULATOR_SPLICE
#include "../../../etc/shaders/include/light_modulator.wgsl"
#endif

fn box_light_modulate(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>, time: f32,
    params: LightModulatorParams) -> vec3<f32> {
  return vec3<f32>(params.a.z);
}
