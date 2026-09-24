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

fn down(uv: vec2<f32>) -> vec4<f32> {
  let ps = 1.0 / vec2<f32>(textureDimensions(src, 0));
  let pl = 2.0 * ps;
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

fn up(uv: vec2<f32>) -> vec4<f32> {
  let o = 0.5 / vec2<f32>(textureDimensions(src, 0));
  var sum = tap(uv + vec2(-o.x * 2.0, 0.0));
  sum = sum + tap(uv + vec2(-o.x, o.y)) * 2.0;
  sum = sum + tap(uv + vec2(0.0, o.y * 2.0));
  sum = sum + tap(uv + vec2(o.x, o.y)) * 2.0;
  sum = sum + tap(uv + vec2(o.x * 2.0, 0.0));
  sum = sum + tap(uv + vec2(o.x, -o.y)) * 2.0;
  sum = sum + tap(uv + vec2(0.0, -o.y * 2.0));
  sum = sum + tap(uv + vec2(-o.x, -o.y)) * 2.0;
  return sum / 12.0;
}

@fragment fn fs_down(in: Out) -> @location(0) vec4<f32> {
  return down(in.uv);
}

@fragment fn fs_up(in: Out) -> @location(0) vec4<f32> {
  return up(in.uv);
}

@fragment fn fs_blit(in: Out) -> @location(0) vec4<f32> {
  return tap(in.uv);
}

@group(0) @binding(2) var base: texture_2d<f32>;
@group(0) @binding(3) var dst: texture_storage_2d<rgba16float, write>;

fn level_uv(id: vec2<u32>) -> vec2<f32> {
  return (vec2<f32>(id) + vec2(0.5)) / vec2<f32>(textureDimensions(dst));
}

@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_down(@builtin(global_invocation_id) id: vec3<u32>) {
  let size = textureDimensions(dst);
  if (id.x >= size.x || id.y >= size.y) {
    return;
  }
  textureStore(dst, vec2<i32>(id.xy), down(level_uv(id.xy)));
}

@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_up(@builtin(global_invocation_id) id: vec3<u32>) {
  let size = textureDimensions(dst);
  if (id.x >= size.x || id.y >= size.y) {
    return;
  }
  let tent = up(level_uv(id.xy));
  let under = textureLoad(base, vec2<i32>(id.xy), 0);
  textureStore(dst, vec2<i32>(id.xy), vec4(mix(under.rgb, tent.rgb, BLUR_MIX_WEIGHT), tent.a));
}
