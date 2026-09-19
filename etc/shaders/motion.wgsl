#include "include/fullscreen.wgsl"

struct Motion {
  inv_clip: mat4x4<f32>,
  prev_clip: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> m: Motion;
@group(0) @binding(1) var depth_tex: texture_depth_2d;

struct MotionOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> MotionOut {
  let corner = fullscreen_corner(index);
  var out: MotionOut;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

@fragment fn fs_main(in: MotionOut) -> @location(0) vec4<f32> {
  let size = vec2<i32>(textureDimensions(depth_tex, 0));
  let c = clamp(vec2<i32>(in.pos.xy), vec2(0, 0), size - vec2(1, 1));
  let depth = textureLoad(depth_tex, c, 0);
  var delta = vec2<f32>(0.0);
  if (depth > 0.0) {
    let ndc = vec4(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0, depth, 1.0);
    let world = m.inv_clip * ndc;
    if (abs(world.w) > 1e-8) {
      let prev = m.prev_clip * vec4(world.xyz / world.w, 1.0);
      if (abs(prev.w) > 1e-8) {
        let puv = vec2(prev.x / prev.w * 0.5 + 0.5, 0.5 - prev.y / prev.w * 0.5);
        delta = clamp(in.uv - puv, vec2(-1.0), vec2(1.0));
      }
    }
  }
  return vec4(delta, 0.0, 1.0);
}
