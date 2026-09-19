#include "include/fullscreen.wgsl"

@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;

struct Out {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> Out {
  let corner = fullscreen_corner(index);
  var out: Out;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

fn tap(uv: vec2<f32>) -> vec4<f32> {
  return textureSampleLevel(src, samp, uv, 0.0);
}

@fragment fn fs_down(in: Out) -> @location(0) vec4<f32> {
  let ps = 1.0 / vec2<f32>(textureDimensions(src, 0));
  let pl = 2.0 * ps;
  let uv = in.uv;
  let a = tap(uv + vec2(-pl.x, pl.y));
  let b = tap(uv + vec2(0.0, pl.y));
  let c = tap(uv + pl);
  let d = tap(uv + vec2(-pl.x, 0.0));
  let e = tap(uv);
  let f = tap(uv + vec2(pl.x, 0.0));
  let g = tap(uv - pl);
  let h = tap(uv + vec2(0.0, -pl.y));
  let i = tap(uv + vec2(pl.x, -pl.y));
  let j = tap(uv + vec2(-ps.x, ps.y));
  let k = tap(uv + ps);
  let l = tap(uv - ps);
  let m = tap(uv + vec2(ps.x, -ps.y));
  var sum = (a + c + g + i) * 0.03125;
  sum = sum + (b + d + f + h) * 0.0625;
  sum = sum + (e + j + k + l + m) * 0.125;
  return sum;
}

@fragment fn fs_up(in: Out) -> @location(0) vec4<f32> {
  let o = 0.5 / vec2<f32>(textureDimensions(src, 0));
  var sum = tap(in.uv + vec2(-o.x * 2.0, 0.0));
  sum = sum + tap(in.uv + vec2(-o.x, o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(0.0, o.y * 2.0));
  sum = sum + tap(in.uv + vec2(o.x, o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(o.x * 2.0, 0.0));
  sum = sum + tap(in.uv + vec2(o.x, -o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(0.0, -o.y * 2.0));
  sum = sum + tap(in.uv + vec2(-o.x, -o.y)) * 2.0;
  return sum / 12.0;
}

@fragment fn fs_blit(in: Out) -> @location(0) vec4<f32> {
  return tap(in.uv);
}
