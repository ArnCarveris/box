// Point sprites of one particle pool. Every slot of the pool is one instance of
// a quad that faces the camera, turned by the rotation of the particle and sized
// by its size; a slot whose lifetime is zero holds no particle and is pushed
// outside the depth range, which clips it away.
//
// The pass reads the depth the scene left behind without writing it, so a
// particle behind geometry is hidden and particles never occlude each other.
// The color of a particle is linear, so it is encoded here when the target of
// the view is an eight bit image the hardware does not encode for.

#include "include/view.wgsl"

@group(0) @binding(0) var<uniform> u: View;

@group(1) @binding(0) var<storage, read> position: array<vec4<f32>>;
@group(1) @binding(1) var<storage, read> velocity: array<vec4<f32>>;
@group(1) @binding(2) var<storage, read> force: array<vec4<f32>>;
@group(1) @binding(3) var<storage, read> color: array<vec4<f32>>;
@group(1) @binding(4) var<storage, read> size: array<f32>;

struct ParticleOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) color: vec4<f32>,
  @location(1) uv: vec2<f32>,
};

@vertex fn vs_main(
  @builtin(vertex_index) vertex: u32,
  @builtin(instance_index) instance: u32
) -> ParticleOut {
  var order = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 3u);
  let index = order[vertex];
  let corner = vec2(f32(index & 1u), f32((index >> 1u) & 1u)) * 2.0 - 1.0;

  var out: ParticleOut;
  out.uv = corner;
  out.color = color[instance];
  out.pos = vec4(2.0, 2.0, -1.0, 1.0);
  let life = velocity[instance].w;
  if (life <= 0.0) {
    return out;
  }

  let angle = force[instance].w;
  let turned = vec2(corner.x * cos(angle) - corner.y * sin(angle),
    corner.x * sin(angle) + corner.y * cos(angle));
  let right = vec3(u.view[0].x, u.view[1].x, u.view[2].x);
  let up = vec3(u.view[0].y, u.view[1].y, u.view[2].y);
  let half_size = max(size[instance], 0.0) * 0.5;
  let world = position[instance].xyz + (right * turned.x + up * turned.y) * half_size;
  out.pos = u.proj * u.view * vec4(world, 1.0);

  return out;
}

@fragment fn fs_main(in: ParticleOut) -> @location(0) vec4<f32> {
  let d = length(in.uv);
  if (d > 1.0) {
    discard;
  }

  var rgb = max(in.color.rgb, vec3(0.0));
  if (u.gamma_encode > 0.5) {
    rgb = pow(rgb, vec3(1.0 / 2.2));
  }

  let alpha = in.color.a * smoothstep(1.0, 0.6, d);

  return vec4(rgb, alpha);
}
