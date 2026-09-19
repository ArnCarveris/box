// The pass that puts the atmosphere into the image of a view. Where the depth
// target is empty it writes the sky view table and the sun disk, and where it
// is not it writes the light the air between the camera and the surface
// scattered, with what is left of the surface behind it in the alpha channel,
// so one premultiplied blend covers both.
//
// The overcast of the weather flattens all of it: the sky view radiance
// carries the cloud deck, the sun, the moon and the stars fade out behind it
// and the haze in front of the geometry loses its color with them.

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

// Part of the lit side that the dark side of the moon keeps, which is the
// light the planet itself sends back to it.
const SKY_EARTHSHINE: f32 = 0.015;

// Part of the cells of the star field that hold a star. The rest are empty,
// which is what keeps one hash per pixel enough to draw the whole sky.
const SKY_STAR_CUTOFF: f32 = 0.93;

// Radiance of the moon, a sphere lit by the sun: the normal of a point of the
// disk decides how much of the sun that point sees, so the phase follows the
// two directions instead of being authored.
fn sky_moon(dir: vec3<f32>, ro: vec3<f32>, r: f32, cosine: f32, width: f32) -> vec3<f32> {
  let edge = sky.moon_dir.w;
  if (edge >= 1.0 || cosine < edge - width || sky_hit(ro, dir, sky_bottom()) > 0.0) {
    return vec3(0.0);
  }

  let m = sky.moon_dir.xyz;
  let pole = select(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), abs(m.y) > 0.99);
  let radius = sqrt(max(1.0 - edge * edge, 1e-12));
  let right = normalize(cross(pole, m));
  let up = cross(m, right);
  let x = dot(dir, right) / radius;
  let y = dot(dir, up) / radius;
  let z = sqrt(max(1.0 - x * x - y * y, 0.0));
  let normal = right * x + up * y - m * z;
  let lit = max(dot(normal, sky.sun_dir.xyz), 0.0) + SKY_EARTHSHINE;
  return sky_transmittance(r, clamp(m.y, -1.0, 1.0)) * sky.moon_color.rgb * lit *
    smoothstep(edge - width, edge + width, cosine);
}

fn sky_star_hash(p: vec3<f32>) -> vec3<f32> {
  let q = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)),
    dot(p, vec3(113.5, 271.9, 124.6)));
  return fract(sin(q) * 43758.5453);
}

// Radiance of the star field, which is a grid of cells over the direction in
// star space: the cell a ray lands in holds at most one star, placed away from
// its walls so a pixel never has to look at a neighbor.
fn sky_stars(dir: vec3<f32>) -> vec3<f32> {
  let fade = sky.stars_color.w;
  let density = sky.stars.x;
  if (fade <= 0.0 || density <= 0.0) {
    return vec3(0.0);
  }

  let rot = sky.stars_rot;
  let local = vec3(dot(dir, rot[0]), dot(dir, rot[1]), dot(dir, rot[2]));
  let p = local * density;
  let cell = floor(p);
  let hash = sky_star_hash(cell);
  if (hash.z < SKY_STAR_CUTOFF) {
    return vec3(0.0);
  }

  let magnitude = (hash.z - SKY_STAR_CUTOFF) / (1.0 - SKY_STAR_CUTOFF);
  let jitter = fract(hash.x * 13.7 + hash.y * 7.3);
  let center = cell + vec3(0.25) + 0.5 * vec3(hash.x, hash.y, jitter);
  let angle = length(p - center) / density;
  let size = sky.stars.y * (0.5 + 0.5 * magnitude);
  let twinkle = 1.0 + sky.stars.z * sin(sky.stars.w * 5.0 + hash.x * 63.0);
  let tint = mix(vec3(1.0), normalize(hash + vec3(0.6)) * 1.7, 0.35);
  return sky.stars_color.rgb * tint * (0.08 + 0.92 * magnitude * magnitude) *
    (1.0 - smoothstep(size * 0.2, size, angle)) * max(twinkle, 0.0) * fade;
}

@fragment fn fs_main(in: SkyOut) -> @location(0) vec4<f32> {
  let depth = sky_depth_at(in.uv);
  let world = sky_world_pos(in.uv, depth);
  let ray = world - sky.camera.xyz;
  let dir = normalize(ray);
  let warped = sky_warp(dir);
  let sun_cos = dot(warped, sky.sun_dir.xyz);
  let width = max(fwidth(sun_cos), 0.000001);
  let moon_cos = dot(warped, sky.moon_dir.xyz);
  let moon_width = max(fwidth(moon_cos), 0.000001);
  let r = sky_eye_radius();
  let ro = vec3(0.0, r, 0.0);

  let overcast = sky_overcast();
  if (depth <= 0.0) {
    let night = sky_stars(warped) * sky_transmittance(r, clamp(warped.y, -1.0, 1.0));
    let bodies = sky_disk(warped, ro, r, sun_cos, width) +
      sky_moon(warped, ro, r, moon_cos, moon_width) + night;
    let radiance = sky_view_radiance(warped) + bodies * (1.0 - overcast);
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
  let haze = mix(gathered, vec3(sky_luminance(gathered)), overcast);
  return vec4(sky_encode(haze), 1.0 - dot(transmittance, vec3(1.0 / 3.0)));
}
