#include "include/fullscreen.wgsl"

@group(0) @binding(0) var src: texture_2d<f32>;

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
  let corner = fullscreen_corner(index);
  return vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
}

@fragment fn fs_main(@builtin(position) pos: vec4<f32>) -> @location(0) f32 {
  let size = vec2<i32>(textureDimensions(src, 0));
  let base = vec2<i32>(pos.xy) * 2;
  let span = vec2<i32>(2, 2) + vec2(size.x & 1, size.y & 1);
  var result = 1.0;
  for (var y = 0; y < span.y; y = y + 1) {
    for (var x = 0; x < span.x; x = x + 1) {
      let c = clamp(base + vec2(x, y), vec2(0, 0), size - vec2(1, 1));
      result = min(result, textureLoad(src, c, 0).r);
    }
  }
  return result;
}
