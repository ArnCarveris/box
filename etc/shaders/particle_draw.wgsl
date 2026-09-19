// Sprites of one particle pool. Every particle the simulation left alive this
// frame is one instance of a quad that faces the camera, turned by the rotation
// of the particle and sized by its size. The instance is an entry of the
// compacted list the simulation appends to, and the number of instances is the
// length of that list, which the pass is drawn from indirectly, so a pool that
// is half empty costs half a draw and no slot that holds no particle is ever
// expanded. A blended pool is drawn over its slots instead, up to the highest
// one the simulation found in use, because the order two sprites blend in has
// to be the same from frame to frame and the order a compaction produces is
// not. The billboard appearance draws the mesh of the emitter in place of that
// quad, with the local x and y of every vertex laid over the camera axes, so
// the mesh keeps facing the camera.
//
// A stretch lays the quad over the direction the particle moves in on the
// screen instead of over the axes of the camera and grows it along that
// direction with the speed of the particle, which is what draws a rain drop or
// a spark as the line it travels; the sprite of a particle that is standing
// still keeps the axes it had.
//
// The curves of the emitter are read here rather than written into the
// channels: the age and the lifetime of a particle are already in the buffers,
// so a size, opacity, rotation or color that changes over the life costs no
// state and leaves the values the particle was spawned with intact.
//
// The pass reads the depth the scene left behind without writing it, so a
// particle behind geometry is hidden and particles never occlude each other.
// With a softness the depth is read a second time and the particle fades out as
// it approaches what is behind it, which is what stops a puff of smoke from
// cutting a hard line into a wall. The color of a particle is linear, so it is
// encoded here when the target of the view is an eight bit image the hardware
// does not encode for.

#include "include/view.wgsl"
#include "include/particle.wgsl"
#include "include/textures.wgsl"

@group(0) @binding(0) var<uniform> u: View;

@group(1) @binding(0) var<storage, read> position: array<vec4<f32>>;
@group(1) @binding(1) var<storage, read> velocity: array<vec4<f32>>;
@group(1) @binding(2) var<storage, read> color: array<vec2<u32>>;
@group(1) @binding(3) var<storage, read> extra: array<u32>;
@group(1) @binding(4) var<storage, read> slots: array<u32>;
@group(1) @binding(5) var<uniform> e: Emitter;

@group(3) @binding(0) var scene_depth: texture_depth_2d;

struct ParticleOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) color: vec4<f32>,
  @location(1) uv: vec2<f32>,
  @location(2) sheet: vec2<f32>,
  @location(3) view_z: f32,
};

struct MeshVertex {
  @location(0) position: vec3<f32>,
  @location(1) uv: vec2<f32>,
};

// Linear color the gradient of the emitter holds at a normalised age.
fn gradient_at(t: f32) -> vec3<f32> {
  let x = clamp(t, 0.0, 1.0) * 3.0;
  let i = min(u32(x), 2u);
  let f = x - f32(i);
  return mix(e.gradient[i].rgb, e.gradient[i + 1u].rgb, f);
}

// Frame of the sheet of the emitter a particle of an age shows, which plays
// once over the life of the particle unless the sheet names a frame rate.
fn frame_at(t: f32, life: f32) -> f32 {
  let frames = max(e.sheet.x, 1.0) * max(e.sheet.y, 1.0);
  var frame = floor(clamp(t, 0.0, 0.999) * frames);
  if (e.sheet.z > 0.0) {
    frame = floor(t * life * e.sheet.z) % frames;
  }

  return frame;
}

// Corner of the sheet of the emitter the frame of a particle sits in.
fn sheet_at(corner: vec2<f32>, t: f32, life: f32) -> vec2<f32> {
  let cols = max(e.sheet.x, 1.0);
  let rows = max(e.sheet.y, 1.0);
  let frame = frame_at(t, life);
  let cell = vec2(frame % cols, floor(frame / cols));

  return (cell + corner * 0.5 + 0.5) / vec2(cols, rows);
}

// Direction a streak is laid along, which is where the particle moves on the
// screen, or the up axis of the view when it is standing still.
fn streak_axis(screen: vec2<f32>) -> vec2<f32> {
  let len = length(screen);
  if (len < 1.0e-5) {
    return vec2(0.0, 1.0);
  }

  return screen / len;
}

// Place one particle of the pool, with the local offset of its corner already
// laid over the camera axes, and read the curves of the emitter at its age.
fn place(instance: u32, local: vec3<f32>, turn: bool) -> ParticleOut {
  var i = instance;
  if ((e.flags & ADDITIVE) != 0u) {
    i = slots[e.count + instance];
  }
  let tint = unpack_color(color[i]);
  let attr = unpack2x16float(extra[i]);
  var out: ParticleOut;
  out.uv = local.xy;
  out.sheet = local.xy;
  out.color = tint;
  out.view_z = 1.0;
  out.pos = vec4(2.0, 2.0, -1.0, 1.0);
  let life = velocity[i].w;
  if (life <= 0.0) {
    return out;
  }

  let t = clamp(position[i].w / life, 0.0, 1.0);
  var offset = local;
  if (turn) {
    let angle = attr.x + curve_at(e.turn_curve, t) * 6.2831853;
    offset = vec3(local.x * cos(angle) - local.y * sin(angle),
      local.x * sin(angle) + local.y * cos(angle), local.z);
  }

  let right = vec3(u.view[0].x, u.view[1].x, u.view[2].x);
  let up = vec3(u.view[0].y, u.view[1].y, u.view[2].y);
  let ahead = vec3(u.view[0].z, u.view[1].z, u.view[2].z);
  let half_size = max(attr.y * curve_at(e.size_curve, t), 0.0) * 0.5;
  var world = position[i].xyz +
    (right * offset.x + up * offset.y + ahead * offset.z) * half_size;
  if (e.stretch > 0.0) {
    let screen = vec2(dot(velocity[i].xyz, right), dot(velocity[i].xyz, up));
    let along = streak_axis(screen);
    let side = vec2(along.y, -along.x);
    let half_len = half_size + e.stretch * length(velocity[i].xyz) * 0.5;
    world = position[i].xyz + ahead * offset.z * half_size +
      right * (side.x * offset.x * half_size + along.x * offset.y * half_len) +
      up * (side.y * offset.x * half_size + along.y * offset.y * half_len);
  }
  out.color = vec4(tint.rgb * gradient_at(t),
    tint.a * curve_at(e.opacity_curve, t));
  out.sheet = sheet_at(local.xy, t, life);
  out.view_z = dot(vec4(u.view[0].z, u.view[1].z, u.view[2].z, u.view[3].z),
    vec4(world, 1.0));
  out.pos = u.proj * u.view * vec4(world, 1.0);

  return out;
}

@vertex fn vs_main(
  @builtin(vertex_index) vertex: u32,
  @builtin(instance_index) instance: u32
) -> ParticleOut {
  let corner = vec2(f32(vertex & 1u), f32((vertex >> 1u) & 1u)) * 2.0 - 1.0;

  return place(instance, vec3(corner, 0.0), true);
}

@vertex fn vs_mesh(
  v: MeshVertex,
  @builtin(instance_index) instance: u32
) -> ParticleOut {
  var out = place(instance, v.position * 2.0, true);
  out.sheet = v.uv;

  return out;
}

// How much of a particle is left where it runs into the geometry behind it.
fn soft_fade(screen: vec2<f32>, view_z: f32) -> f32 {
  let softness = e.sheet.w;
  if (softness <= 0.0) {
    return 1.0;
  }

  let size = vec2<i32>(textureDimensions(scene_depth, 0));
  let c = clamp(vec2<i32>(screen), vec2(0, 0), size - vec2(1, 1));
  let depth = textureLoad(scene_depth, c, 0);
  if (depth <= 0.0) {
    return 1.0;
  }

  return soft_at(u.proj[3][2] / depth, view_z, softness);
}

@fragment fn fs_main(in: ParticleOut) -> @location(0) vec4<f32> {
  var rgb = max(in.color.rgb, vec3(0.0));
  var alpha = in.color.a;
  if (e.appearance == DRAW_CIRCLE) {
    let d = length(in.uv);
    if (d > 1.0) {
      discard;
    }

    alpha = alpha * smoothstep(1.0, 0.6, d);
  } else if (e.texture != 0u) {
    let texel = tex_sample(e.texture, in.sheet, dpdx(in.sheet), dpdy(in.sheet));
    rgb = rgb * texel.rgb;
    alpha = alpha * texel.a;
  }

  alpha = alpha * soft_fade(in.pos.xy, in.view_z);
  if (u.gamma_encode > 0.5) {
    rgb = pow(rgb, vec3(1.0 / 2.2));
  }

  return vec4(rgb, alpha);
}
