#include "include/env.wgsl"
#include "include/env_sample.wgsl"

// The environment of a view whose EnvironmentLight names an atmosphere is
// filled from the sky view table instead of from the two colors, so the image
// based lighting of the scene is the sky the frame draws.
#define SKY_BINDING 3
#define SKY_VIEW_BINDING 4
#define SKY_SAMPLER_BINDING 5
#include "include/sky.wgsl"

@fragment fn fs_gradient(in: EnvOut) -> @location(0) vec4<f32> {
  let d = face_dir(u32(e.params.x), in.uv);
  let t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
  return vec4(mix(e.ground.rgb, e.sky.rgb, t), 1.0);
}

@fragment fn fs_brdf(in: EnvOut) -> @location(0) vec4<f32> {
  let ndv = max(in.uv.x, 1e-3);
  let roughness = max(in.uv.y, 0.02);
  let v = vec3(sqrt(max(1.0 - ndv * ndv, 0.0)), 0.0, ndv);
  let n = vec3(0.0, 0.0, 1.0);
  let count = u32(ENV_BRDF_SAMPLES);
  let k = roughness * roughness * 0.5;
  var scale = 0.0;
  var bias = 0.0;
  for (var i = 0u; i < count; i = i + 1u) {
    let h = importance_ggx(hammersley(i, count), n, roughness);
    let l = normalize(2.0 * dot(v, h) * h - v);
    let ndl = max(l.z, 0.0);
    let ndh = max(h.z, 1e-4);
    let vdh = max(dot(v, h), 0.0);
    if (ndl > 0.0) {
      let g = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
      let vis = g * vdh / (ndh * ndv);
      let fc = pow(1.0 - vdh, 5.0);
      scale = scale + (1.0 - fc) * vis;
      bias = bias + fc * vis;
    }
  }
  return vec4(scale / f32(count), bias / f32(count), 0.0, 1.0);
}

@fragment fn fs_sky(in: EnvOut) -> @location(0) vec4<f32> {
  return vec4(sky_view_radiance(face_dir(u32(e.params.x), in.uv)), 1.0);
}
