#pragma once

fn fullscreen_corner(index: u32) -> vec2<f32> {
  return vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
}
