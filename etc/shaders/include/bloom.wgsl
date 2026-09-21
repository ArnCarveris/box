#pragma once

fn bloom_tent(input_texture: texture_2d<f32>, bloom_sampler: sampler, uv: vec2<f32>) -> vec3<f32> {
  let a = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  1)).rgb;
  let b = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0,  1)).rgb;
  let c = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  1)).rgb;
  let d = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  0)).rgb;
  let e = textureSample(input_texture, bloom_sampler, uv).rgb;
  let f = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  0)).rgb;
  let g = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1, -1)).rgb;
  let h = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0, -1)).rgb;
  let i = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1, -1)).rgb;
  var sample = e * 0.25;
  sample += (b + d + f + h) * 0.125;
  sample += (a + c + g + i) * 0.0625;
  return sample;
}
