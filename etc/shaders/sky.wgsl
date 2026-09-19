// The pass that puts the atmosphere into the image of a view. Where the depth
// target is empty it writes the sky view table and the sun disk, and where it
// is not it writes the light the air between the camera and the surface
// scattered, with what is left of the surface behind it in the alpha channel,
// so one premultiplied blend covers both.

#define SKY_BINDING 0
#define SKY_TRANS_BINDING 1
#define SKY_MS_BINDING 2
#define SKY_SAMPLER_BINDING 3
#define SKY_VIEW_BINDING 4

#include "include/fullscreen.wgsl"
#include "include/sky.wgsl"

@group(0) @binding(5) var sky_depth: texture_depth_2d;

const SKY_AERIAL_STEPS: i32 = 6;

struct SkyOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> SkyOut {
  let corner = fullscreen_corner(index);
  var out: SkyOut;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

fn sky_depth_at(uv: vec2<f32>) -> f32 {
  let size = vec2<i32>(textureDimensions(sky_depth, 0));
  let texel = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(sky_depth, texel, 0);
}

fn sky_world_pos(uv: vec2<f32>, depth: f32) -> vec3<f32> {
  let ndc = vec3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, max(depth, 0.0000001));
  let p = sky.inv_clip * vec4(ndc, 1.0);
  return p.xyz / p.w;
}

// Radiance of the sun itself, which is only drawn where the planet does not
// cover it and fades over the width of a pixel at the edge of the disk.
fn sky_disk(dir: vec3<f32>, ro: vec3<f32>, r: f32, cosine: f32, width: f32) -> vec3<f32> {
  let edge = sky.mie_params.z;
  let intensity = sky.mie_params.w;
  if (intensity <= 0.0 || cosine < edge - 0.001 || sky_hit(ro, dir, sky_bottom()) > 0.0) {
    return vec3(0.0);
  }

  let mu = clamp(sky.sun_dir.y, -1.0, 1.0);
  return sky_transmittance(r, mu) * sky.sun_color.rgb * intensity *
    smoothstep(edge - width, edge + width, cosine);
}

@fragment fn fs_main(in: SkyOut) -> @location(0) vec4<f32> {
  let depth = sky_depth_at(in.uv);
  let world = sky_world_pos(in.uv, depth);
  let ray = world - sky.camera.xyz;
  let dir = normalize(ray);
  let warped = sky_warp(dir);
  let sun_cos = dot(warped, sky.sun_dir.xyz);
  let width = max(fwidth(sun_cos), 0.000001);
  let r = sky_eye_radius();
  let ro = vec3(0.0, r, 0.0);

  if (depth <= 0.0) {
    let radiance = sky_view_radiance(warped) + sky_disk(warped, ro, r, sun_cos, width);
    return vec4(sky_encode(radiance), 1.0);
  }

  let density = sky.sun_color.w;
  if (density <= 0.0) {
    return vec4(0.0, 0.0, 0.0, 0.0);
  }

  var distance_km = min(length(ray) * sky.world_scale.y, sky.mie_params.y);
  let top = sky_hit(ro, dir, sky_top());
  if (top > 0.0) {
    distance_km = min(distance_km, top);
  }

  var transmittance = vec3(1.0);
  let gathered = sky_march(ro, dir, distance_km, SKY_AERIAL_STEPS, density,
    &transmittance);
  return vec4(sky_encode(gathered), 1.0 - dot(transmittance, vec3(1.0 / 3.0)));
}
