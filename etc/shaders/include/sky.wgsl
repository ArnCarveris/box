#pragma once

// Atmosphere the sky passes share: the medium, the phase functions and the
// parameterizations of the transmittance, multiple scattering and sky view
// tables. An includer names the bindings it has through SKY_BINDING,
// SKY_SAMPLER_BINDING and, when it reads them, SKY_TRANS_BINDING,
// SKY_MS_BINDING and SKY_VIEW_BINDING, so one file serves the table passes,
// the sky pass and the environment face pass.

const SKY_PI: f32 = 3.14159265358979;

// Kilometers the ray origin is lifted over the planet so a camera at sea level
// does not start exactly on the sphere the marches intersect.
const SKY_LIFT: f32 = 0.001;

struct Sky {
  inv_clip: mat4x4<f32>,
  camera: vec4<f32>,
  sun_dir: vec4<f32>,
  sun_color: vec4<f32>,
  ground_albedo: vec4<f32>,
  bounds: vec4<f32>,
  scat_rayleigh: vec4<f32>,
  scat_mie: vec4<f32>,
  ext_mie: vec4<f32>,
  abs_ozone: vec4<f32>,
  abs_strat: vec4<f32>,
  abs_haze: vec4<f32>,
  mie_params: vec4<f32>,
  world_scale: vec4<f32>,
  night: vec4<f32>,
  turbulence: vec4<f32>,
  moon_dir: vec4<f32>,
  moon_color: vec4<f32>,
  stars_color: vec4<f32>,
  stars: vec4<f32>,
  stars_rot: mat3x3<f32>,
  flags: vec4<u32>,
};

@group(0) @binding(SKY_BINDING) var<uniform> sky: Sky;
@group(0) @binding(SKY_SAMPLER_BINDING) var sky_sampler: sampler;

#ifdef SKY_TRANS_BINDING
@group(0) @binding(SKY_TRANS_BINDING) var sky_trans_lut: texture_2d<f32>;
#endif

#ifdef SKY_MS_BINDING
@group(0) @binding(SKY_MS_BINDING) var sky_ms_lut: texture_2d<f32>;
#endif

#ifdef SKY_VIEW_BINDING
@group(0) @binding(SKY_VIEW_BINDING) var sky_view_lut: texture_2d<f32>;
#endif

struct SkyMedium {
  scatter: vec3<f32>,
  extinct: vec3<f32>,
  rayleigh: vec3<f32>,
  mie: vec3<f32>,
};

fn sky_bottom() -> f32 {
  return sky.bounds.x;
}

fn sky_top() -> f32 {
  return sky.bounds.y;
}

// Radius the camera of the view sits at, never below the planet surface.
fn sky_eye_radius() -> f32 {
  return sky_bottom() + max(sky.camera.w, SKY_LIFT);
}

// The two layers that scatter and the three that only absorb, at an altitude
// in kilometers: the ozone and the stratospheric aerosols are tent profiles
// around their peak, the rest fall off exponentially.
fn sky_medium(alt_km: f32) -> SkyMedium {
  let density_rayleigh = exp(-alt_km / sky.bounds.z);
  let density_mie = exp(-alt_km / sky.bounds.w);
  let density_ozone = max(0.0, 1.0 - abs(alt_km - 25.0) / 15.0);
  let density_strat = max(0.0, 1.0 - abs(alt_km - 20.0) / 5.0);

  var m: SkyMedium;
  m.rayleigh = sky.scat_rayleigh.xyz * density_rayleigh;
  m.mie = sky.scat_mie.xyz * density_mie;
  m.scatter = m.rayleigh + m.mie;
  m.extinct = m.rayleigh + sky.ext_mie.xyz * density_mie +
    sky.abs_ozone.xyz * density_ozone + sky.abs_strat.xyz * density_strat +
    sky.abs_haze.xyz * density_mie;
  return m;
}

fn sky_rayleigh_phase(c: f32) -> f32 {
  return (3.0 / (16.0 * SKY_PI)) * (1.0 + c * c);
}

fn sky_mie_phase(c: f32, g: f32) -> f32 {
  let k = (3.0 / (8.0 * SKY_PI)) * (1.0 - g * g) / (2.0 + g * g);
  let denom = max(1.0 + g * g - 2.0 * g * c, 1e-6);
  return k * (1.0 + c * c) / max(pow(denom, 1.5), 1e-6);
}

// Distance to the nearest intersection of a ray from the planet center with a
// sphere of a radius, negative when the ray misses it or leaves it behind.
fn sky_hit(ro: vec3<f32>, rd: vec3<f32>, radius: f32) -> f32 {
  let b = dot(ro, rd);
  let c = dot(ro, ro) - radius * radius;
  let disc = b * b - c;
  if (disc < 0.0) {
    return -1.0;
  }

  let root = sqrt(disc);
  let near = -b - root;
  if (near >= 0.0) {
    return near;
  }

  let far = -b + root;
  if (far >= 0.0) {
    return far;
  }

  return -1.0;
}

// Distance a ray travels through the medium before it leaves it or hits the
// planet, and whether it was the planet that stopped it.
fn sky_ray_length(ro: vec3<f32>, rd: vec3<f32>, hit_ground: ptr<function, bool>) -> f32 {
  let top = sky_hit(ro, rd, sky_top());
  let ground = sky_hit(ro, rd, sky_bottom());
  var result = top;
  *hit_ground = false;
  if (ground > 0.0 && ground < result) {
    result = ground;
    *hit_ground = true;
  }

  return result;
}

// The transmittance table is indexed by the distance a ray of an altitude has
// left through the medium, which spreads its texels over the horizon where the
// optical depth changes fastest.
fn sky_trans_uv(r: f32, mu: f32) -> vec2<f32> {
  let bottom = sky_bottom();
  let top = sky_top();
  let h = sqrt(max(0.0, top * top - bottom * bottom));
  let rho = sqrt(max(0.0, r * r - bottom * bottom));
  let disc = max(0.0, r * r * (mu * mu - 1.0) + top * top);
  let d = max(0.0, -r * mu + sqrt(disc));
  let d_min = top - r;
  let d_max = rho + h;
  var x = 0.0;
  if (d_max > d_min + 1e-6) {
    x = (d - d_min) / (d_max - d_min);
  }

  return vec2(clamp(x, 0.0, 1.0), clamp(rho / max(h, 1e-6), 0.0, 1.0));
}

fn sky_uv_trans(uv: vec2<f32>) -> vec2<f32> {
  let bottom = sky_bottom();
  let top = sky_top();
  let h = sqrt(max(0.0, top * top - bottom * bottom));
  let rho = h * uv.y;
  let r = sqrt(rho * rho + bottom * bottom);
  let d_min = top - r;
  let d_max = rho + h;
  let d = d_min + uv.x * (d_max - d_min);
  var mu = 1.0;
  if (d > 0.0) {
    mu = (h * h - rho * rho - d * d) / (2.0 * r * d);
  }

  return vec2(r, clamp(mu, -1.0, 1.0));
}

// The multiple scattering table is indexed by the sun elevation and the
// altitude, both of which the medium varies slowly over.
fn sky_ms_uv(cos_sun: f32, r: f32) -> vec2<f32> {
  let span = max(sky_top() - sky_bottom(), 1e-4);
  return vec2(clamp(0.5 + 0.5 * cos_sun, 0.0, 1.0),
    clamp((r - sky_bottom()) / span, 0.0, 1.0));
}

fn sky_uv_ms(uv: vec2<f32>) -> vec2<f32> {
  return vec2(uv.x * 2.0 - 1.0, uv.y * (sky_top() - sky_bottom()));
}

// Zenith angle the horizon sits at for a viewer at a radius.
fn sky_horizon(r: f32) -> f32 {
  let bottom = sky_bottom();
  let cosine = -sqrt(max(0.0, r * r - bottom * bottom)) / max(r, 1e-4);
  return acos(clamp(cosine, -1.0, 1.0));
}

// The sky view table spends half its rows over the sky and half under the
// horizon, each with a square distribution that crowds its texels against the
// horizon line where the gradient is steepest.
fn sky_view_dir(uv: vec2<f32>, r: f32) -> vec3<f32> {
  let horizon = sky_horizon(r);
  var zenith = 0.0;
  if (uv.y < 0.5) {
    let t = uv.y * 2.0;
    zenith = horizon * t * t;
  } else {
    let t = uv.y * 2.0 - 1.0;
    zenith = horizon + (SKY_PI - horizon) * t * t;
  }

  let azimuth = (uv.x - 0.5) * 2.0 * SKY_PI;
  let s = sin(zenith);
  return vec3(s * sin(azimuth), cos(zenith), s * cos(azimuth));
}

fn sky_view_uv(dir: vec3<f32>, r: f32) -> vec2<f32> {
  let horizon = sky_horizon(r);
  let zenith = acos(clamp(dir.y, -1.0, 1.0));
  var v = 0.0;
  if (zenith < horizon) {
    v = 0.5 * sqrt(clamp(zenith / max(horizon, 1e-6), 0.0, 1.0));
  } else {
    v = 0.5 + 0.5 * sqrt(clamp((zenith - horizon) / max(SKY_PI - horizon, 1e-6), 0.0, 1.0));
  }

  let u = atan2(dir.x, dir.z) / (2.0 * SKY_PI) + 0.5;
  return vec2(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0));
}

#ifdef SKY_TRANS_BINDING
fn sky_transmittance(r: f32, mu: f32) -> vec3<f32> {
  return textureSampleLevel(sky_trans_lut, sky_sampler, sky_trans_uv(r, mu), 0.0).rgb;
}

// Fraction of the sun a point of the medium still sees, which falls to zero
// over the few degrees the planet takes to cover it.
fn sky_sun_visible(r: f32, mu: f32) -> f32 {
  let bottom = sky_bottom();
  let horizon = -sqrt(max(0.0, 1.0 - (bottom * bottom) / max(r * r, 1e-6)));
  return smoothstep(horizon - 0.02, horizon + 0.02, mu);
}

fn sky_sun_light(r: f32, mu: f32) -> vec3<f32> {
  return sky_transmittance(r, mu) * sky_sun_visible(r, mu);
}
#endif

#ifdef SKY_MS_BINDING
// Radiance one sample of a view ray scatters towards the camera: the sunlight
// that reaches it directly, spread by the two phase functions, plus the light
// the rest of the sky sends it, which the multiple scattering table holds.
fn sky_inscatter(pos: vec3<f32>, r: f32, m: SkyMedium, cos_sun: f32) -> vec3<f32> {
  let up = pos / max(r, 1e-4);
  let mu = clamp(dot(up, sky.sun_dir.xyz), -1.0, 1.0);
  let single = m.rayleigh * sky_rayleigh_phase(cos_sun) +
    m.mie * sky_mie_phase(cos_sun, sky.mie_params.x);
  let multi = textureSampleLevel(sky_ms_lut, sky_sampler, sky_ms_uv(mu, r), 0.0).rgb;
  return (single * sky_sun_light(r, mu) + multi * m.scatter) * sky.sun_color.rgb;
}

// March a view ray through the medium, returning the radiance it gathers and
// writing the transmittance that is left of what lies behind it.
fn sky_march(ro: vec3<f32>, rd: vec3<f32>, t_max: f32, steps: i32, density: f32,
             out_transmittance: ptr<function, vec3<f32>>) -> vec3<f32> {
  *out_transmittance = vec3(1.0);
  if (t_max <= 0.0) {
    return vec3(0.0);
  }

  let dt = t_max / f32(steps);
  let cos_sun = clamp(dot(rd, sky.sun_dir.xyz), -1.0, 1.0);
  var transmittance = vec3(1.0);
  var radiance = vec3(0.0);
  for (var i = 0; i < steps; i = i + 1) {
    let pos = ro + rd * ((f32(i) + 0.5) * dt);
    let r = length(pos);
    var m = sky_medium(max(r - sky_bottom(), 0.0));
    m.scatter = m.scatter * density;
    m.rayleigh = m.rayleigh * density;
    m.mie = m.mie * density;
    m.extinct = m.extinct * density;
    let step = exp(-m.extinct * dt);
    let gathered = sky_inscatter(pos, r, m, cos_sun);
    radiance = radiance + transmittance *
      (gathered - gathered * step) / max(m.extinct, vec3(1e-6));
    transmittance = transmittance * step;
  }

  *out_transmittance = transmittance;
  return radiance;
}
#endif

#ifdef SKY_VIEW_BINDING
// Radiance the sky view table holds for a direction, which is the sky without
// the sun disk and without anything the geometry of the view covers.
fn sky_view_radiance(dir: vec3<f32>) -> vec3<f32> {
  return textureSampleLevel(sky_view_lut, sky_sampler,
    sky_view_uv(dir, sky_eye_radius()), 0.0).rgb;
}
#endif

// Warp of a view ray that stands for the shimmer of hot air, which leaves the
// ray alone while the turbulence of the atmosphere is zero.
fn sky_warp(dir: vec3<f32>) -> vec3<f32> {
  let amount = sky.turbulence.x;
  if (amount <= 0.0) {
    return dir;
  }

  let t = sky.turbulence.w * sky.turbulence.z;
  let p = dir * sky.turbulence.y;
  let a = sin(p + vec3(t * 0.73, t * 0.91, t * 1.13));
  let b = sin(p.yzx * 1.37 + vec3(t * 1.21, t * 0.83, t * 0.97));
  let offset = (a + 0.5 * b) * 0.666;
  let horizon = 0.3 + 0.7 * (1.0 - clamp(dir.y, -0.05, 1.0));
  return normalize(dir + vec3(offset.x * 0.05, offset.y, offset.z * 0.05) *
    0.008 * amount * horizon);
}

// The display encoding the target of the pass asks for, which is the identity
// on the float path a post process chain writes into.
fn sky_encode(c: vec3<f32>) -> vec3<f32> {
  if (sky.sun_dir.w > 0.5) {
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
  }

  return c;
}
