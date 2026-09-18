#include "include/env.wgsl"

@group(0) @binding(1) var src: texture_cube<f32>;
@group(0) @binding(2) var src_sampler: sampler;

#include "include/env_sample.wgsl"

fn importance_cosine(xi: vec2<f32>, n: vec3<f32>) -> vec3<f32> {
  let phi = 6.28318531 * xi.x;
  let cos_theta = sqrt(max(1.0 - xi.y, 0.0));
  let sin_theta = sqrt(xi.y);
  let h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
  return tangent_to_world(h, n);
}

@fragment fn fs_specular(in: EnvOut) -> @location(0) vec4<f32> {
  let n = face_dir(u32(e.params.x), in.uv);
  let count = u32(ENV_SPECULAR_SAMPLES);
  var sum = vec3<f32>(0.0);
  var weight = 0.0;
  for (var i = 0u; i < count; i = i + 1u) {
    let h = importance_ggx(hammersley(i, count), n, e.params.y);
    let l = normalize(2.0 * dot(n, h) * h - n);
    let ndl = dot(n, l);
    if (ndl > 0.0) {
      sum = sum + textureSampleLevel(src, src_sampler, l, 0.0).rgb * ndl;
      weight = weight + ndl;
    }
  }
  return vec4(sum / max(weight, 1e-4), 1.0);
}

@fragment fn fs_irradiance(in: EnvOut) -> @location(0) vec4<f32> {
  let n = face_dir(u32(e.params.x), in.uv);
  let count = u32(ENV_IRRADIANCE_SAMPLES);
  var sum = vec3<f32>(0.0);
  for (var i = 0u; i < count; i = i + 1u) {
    let l = importance_cosine(hammersley(i, count), n);
    sum = sum + textureSampleLevel(src, src_sampler, l, 0.0).rgb;
  }
  return vec4(sum / f32(count), 1.0);
}
