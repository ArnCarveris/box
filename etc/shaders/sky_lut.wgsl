// The three tables the atmosphere is rendered from. The transmittance table
// holds what is left of a ray of light that crosses the medium, the multiple
// scattering table holds the light the rest of the sky sends a point after any
// number of bounces, and the sky view table holds the radiance of every
// direction around the camera. The first two only depend on the medium, the
// third follows the sun and the altitude of the camera.

#define SKY_BINDING 0
#define SKY_TRANS_BINDING 1
#define SKY_MS_BINDING 2
#define SKY_SAMPLER_BINDING 3

#include "include/fullscreen.wgsl"
#include "include/sky.wgsl"

const SKY_TRANS_STEPS: i32 = 40;
const SKY_MS_STEPS: i32 = 20;
const SKY_MS_DIRS: i32 = 8;
const SKY_VIEW_STEPS: i32 = 30;

struct SkyLutOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> SkyLutOut {
  let corner = fullscreen_corner(index);
  var out: SkyLutOut;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

@fragment fn fs_transmittance(in: SkyLutOut) -> @location(0) vec4<f32> {
  let params = sky_uv_trans(in.uv);
  let r = params.x;
  let mu = params.y;
  let ro = vec3(0.0, r, 0.0);
  let rd = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
  var ground = false;
  let t_max = sky_ray_length(ro, rd, &ground);
  if (t_max <= 0.0) {
    return vec4(1.0, 1.0, 1.0, 1.0);
  }

  let dt = t_max / f32(SKY_TRANS_STEPS);
  var depth = vec3(0.0);
  for (var i = 0; i < SKY_TRANS_STEPS; i = i + 1) {
    let pos = ro + rd * ((f32(i) + 0.5) * dt);
    depth = depth + sky_medium(max(length(pos) - sky_bottom(), 0.0)).extinct * dt;
  }

  return vec4(exp(-depth), 1.0);
}

// One direction of the multiple scattering gather: the light a ray picks up
// under a uniform phase function, and the fraction of it that scatters on
// again, which the geometric series over the bounces is summed from.
fn sky_ms_ray(ro: vec3<f32>, rd: vec3<f32>, sun: vec3<f32>) -> vec4<f32> {
  var ground = false;
  let t_max = sky_ray_length(ro, rd, &ground);
  if (t_max <= 0.0) {
    return vec4(0.0);
  }

  let dt = t_max / f32(SKY_MS_STEPS);
  let uniform_phase = 1.0 / (4.0 * SKY_PI);
  var transmittance = vec3(1.0);
  var radiance = vec3(0.0);
  var bounced = vec3(0.0);
  for (var i = 0; i < SKY_MS_STEPS; i = i + 1) {
    let pos = ro + rd * ((f32(i) + 0.5) * dt);
    let r = length(pos);
    let m = sky_medium(max(r - sky_bottom(), 0.0));
    let step = exp(-m.extinct * dt);
    let up = pos / max(r, 1e-4);
    let mu = clamp(dot(up, sun), -1.0, 1.0);
    radiance = radiance + transmittance *
      sky_sun_light(r, mu) * uniform_phase * m.scatter * dt;
    bounced = bounced + transmittance *
      (m.scatter - m.scatter * step) / max(m.extinct, vec3(1e-6));
    transmittance = transmittance * step;
  }

  if (ground) {
    let hit = ro + rd * t_max;
    let mu = clamp(dot(normalize(hit), sun), -1.0, 1.0);
    if (mu > 0.0) {
      radiance = radiance + transmittance *
        sky_transmittance(sky_bottom(), mu) * sky.ground_albedo.rgb * mu / SKY_PI;
    }
  }

  return vec4(radiance, dot(bounced, vec3(1.0 / 3.0)));
}

@fragment fn fs_multiscatter(in: SkyLutOut) -> @location(0) vec4<f32> {
  let params = sky_uv_ms(in.uv);
  let r = sky_bottom() + max(params.y, SKY_LIFT);
  let ro = vec3(0.0, r, 0.0);
  let cos_sun = params.x;
  let sun = vec3(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);
  let weight = 1.0 / f32(SKY_MS_DIRS * SKY_MS_DIRS);
  var gathered = vec3(0.0);
  var bounced = 0.0;
  for (var i = 0; i < SKY_MS_DIRS; i = i + 1) {
    for (var j = 0; j < SKY_MS_DIRS; j = j + 1) {
      let phi = 2.0 * SKY_PI * (f32(i) + 0.5) / f32(SKY_MS_DIRS);
      let cos_theta = 1.0 - 2.0 * (f32(j) + 0.5) / f32(SKY_MS_DIRS);
      let sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
      let dir = vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
      let ray = sky_ms_ray(ro, dir, sun);
      gathered = gathered + ray.rgb * weight;
      bounced = bounced + ray.w * weight;
    }
  }

  return vec4(gathered / max(1.0 - bounced, 1e-4), 1.0);
}

@fragment fn fs_view(in: SkyLutOut) -> @location(0) vec4<f32> {
  let r = sky_eye_radius();
  let ro = vec3(0.0, r, 0.0);
  let rd = sky_view_dir(in.uv, r);
  var ground = false;
  let t_max = sky_ray_length(ro, rd, &ground);
  var transmittance = vec3(1.0);
  let gathered = sky_march(ro, rd, t_max, SKY_VIEW_STEPS, 1.0, &transmittance);
  let night = 1.0 - smoothstep(-0.05, 0.02, sky.sun_dir.y);
  return vec4(gathered + sky.night.rgb * night, 1.0);
}
