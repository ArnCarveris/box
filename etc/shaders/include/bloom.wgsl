#pragma once

fn bloom_tent(input_texture: texture_2d<f32>, bloom_sampler: sampler, uv: vec2<f32>) -> vec3<f32> {
  let a = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-1,  1)).rgb;
  let b = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 0,  1)).rgb;
  let c = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 1,  1)).rgb;
  let d = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-1,  0)).rgb;
  let e = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0).rgb;
  let f = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 1,  0)).rgb;
  let g = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-1, -1)).rgb;
  let h = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 0, -1)).rgb;
  let i = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 1, -1)).rgb;
  var sample = e * 0.25;
  sample += (b + d + f + h) * 0.125;
  sample += (a + c + g + i) * 0.0625;
  return sample;
}
