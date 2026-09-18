#pragma once

fn shade(n: vec3<f32>, v: vec3<f32>, l: vec3<f32>, albedo: vec3<f32>,
    metallic: f32, roughness: f32) -> vec3<f32> {
  let h = normalize(l + v);
  let ndl = max(dot(n, l), 0.0);
  let shininess = (1.0 - roughness) * (1.0 - roughness) * 512.0 + 2.0;
  let spec = pow(max(dot(n, h), 0.0), shininess) * (1.0 - roughness);
  let f0 = mix(vec3(0.04), albedo, metallic);
  return (albedo * (1.0 - metallic) + f0 * spec) * ndl;
}

fn environment(n: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>,
    metallic: f32, roughness: f32) -> vec3<f32> {
  let ndv = max(dot(n, v), 1e-4);
  let f0 = mix(vec3(0.04), albedo, metallic);
  let irradiance = textureSampleLevel(env_irradiance, env_sampler, n, 0.0).rgb;
  let diffuse = irradiance * albedo * (1.0 - metallic);
  let r = reflect(-v, n);
  let mip = roughness * (f32(ENV_MIPS) - 1.0);
  let prefiltered = textureSampleLevel(env_specular, env_sampler, r, mip).rgb;
  let ab = textureSampleLevel(env_brdf, env_sampler, vec2(ndv, roughness), 0.0).xy;
  return diffuse + prefiltered * (f0 * ab.x + vec3(ab.y));
}

fn glass_specular(n: vec3<f32>, v: vec3<f32>, l: vec3<f32>, albedo: vec3<f32>,
    metallic: f32, roughness: f32, sun: vec3<f32>, shadow: f32) -> vec3<f32> {
  let f0 = mix(vec3(0.04), albedo, metallic);
  let h = normalize(l + v);
  let ndl = max(dot(n, l), 0.0);
  let ndv = max(dot(n, v), 1e-4);
  let shininess = (1.0 - roughness) * (1.0 - roughness) * 512.0 + 2.0;
  let lobe = pow(max(dot(n, h), 0.0), shininess) * (1.0 - roughness);
  let mip = roughness * (f32(ENV_MIPS) - 1.0);
  let mirror = textureSampleLevel(env_specular, env_sampler, reflect(-v, n), mip).rgb;
  let ab = textureSampleLevel(env_brdf, env_sampler, vec2(ndv, roughness), 0.0).xy;
  return f0 * lobe * ndl * sun * shadow + mirror * (f0 * ab.x + vec3(ab.y));
}

fn glass_tint(albedo: vec3<f32>) -> vec3<f32> {
  let peak = max(max(albedo.r, albedo.g), max(albedo.b, 1e-4));
  return albedo / peak;
}

fn resolve(color: vec3<f32>, spec: vec3<f32>, albedo: vec3<f32>, alpha: f32,
    frag: vec2<f32>, roughness: f32, n: vec3<f32>, v: vec3<f32>,
    blend: u32) -> vec4<f32> {
  var rgb = color;
  var a = alpha;
  if (blend == 1u || blend == 2u) {
    let dims = vec2<f32>(textureDimensions(scene_tex, 0));
    let levels = f32(textureNumLevels(scene_tex)) - 1.0;
    let perceptual = sqrt(clamp(roughness, 0.0, 1.0));
    let bend = (u.view * vec4(n, 0.0)).xy * vec2(1.0, -1.0);
    let uv = (frag + bend * dims.y * GLASS_REFRACT) / dims;
    let bg = textureSampleLevel(scene_tex, scene_samp, uv, perceptual * levels).rgb;
    let ndv = max(dot(n, v), 1e-4);
    let fx = 1.0 - ndv;
    let fresnel = 0.04 + 0.96 * fx * fx * fx * fx * fx;
    let through = 1.0 - a;
    var own = rgb * a;
    if (blend == 2u) {
      own = rgb;
    }
    rgb = own + spec * through +
      through * perceptual * (1.0 - fresnel) * bg * glass_tint(albedo);
    a = 1.0 - through * (1.0 - perceptual);
  }
  return vec4(rgb, a);
}
