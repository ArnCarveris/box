#include "include/effect.wgsl"

// Distance the fog integrates to for a pixel that hit no geometry.
const FOG_FAR: f32 = 5000.0;

// Optical depth of a ray through a density that falls off exponentially with
// world height, integrated analytically between two distances.
fn fog_depth(density: f32, falloff: f32, height: f32, y0: f32, dy: f32,
             t0: f32, t1: f32) -> f32 {
  let len = t1 - t0;
  if (len <= 0.0 || density <= 0.0) {
    return 0.0;
  }

  let base = density * exp(clamp(-falloff * (y0 + dy * t0 - height), -32.0, 32.0));
  let k = falloff * dy;
  if (abs(k) < 0.00001) {
    return base * len;
  }

  return base * (1.0 - exp(clamp(-k * len, -32.0, 32.0))) / k;
}

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  let depth = fx_view_depth(in.uv);
  let world = fx_world_pos(in.uv, depth);
  let eye = fx.eye.xyz;
  let to_pixel = world - eye;
  let length_to_pixel = max(length(to_pixel), 0.0001);
  let dir = to_pixel / length_to_pixel;
  var dist = length_to_pixel;
  if (depth <= 0.0) {
    dist = FOG_FAR;
  }

  dist = min(dist, FOG_FAR);
  let start = max(fx.params[1].y, 0.0);
  let optical = fog_depth(fx.params[0].w, fx.params[1].x, fx.params[1].w,
    eye.y, dir.y, min(start, dist), dist);
  var opacity = fx.params[1].z;
  if (opacity <= 0.0) {
    opacity = 1.0;
  }

  let factor = min(1.0 - exp(-optical), opacity);
  let mixed = mix(fx_to_linear(source.rgb), fx.params[0].xyz, factor);
  return vec4(fx_to_display(mixed), source.a);
}
