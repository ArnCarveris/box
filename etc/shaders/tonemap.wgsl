#include "include/effect.wgsl"

// Hue preserving display transform in the spirit of Tony McMapface: the peak
// channel is compressed onto a shoulder and the result is blended towards white
// as it saturates, which is the desaturation the reference LUT is known for
// without shipping a LUT. It is the Khronos PBR Neutral curve.
fn tonemap(color: vec3<f32>) -> vec3<f32> {
  let start = 0.76;
  let desaturation = 0.15;
  var c = max(color, vec3(0.0));
  let low = min(c.r, min(c.g, c.b));
  var offset = 0.04;
  if (low < 0.08) {
    offset = low - 6.25 * low * low;
  }

  c = c - vec3(offset);
  let peak = max(c.r, max(c.g, c.b));
  if (peak < start) {
    return c;
  }

  let d = 1.0 - start;
  let new_peak = 1.0 - d * d / (peak + d - start);
  c = c * (new_peak / peak);
  let blend = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
  return mix(c, vec3(new_peak), blend);
}

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  var white = fx.params[0].x;
  if (white <= 0.0) {
    white = 1.0;
  }

  let mapped = tonemap(fx_to_linear(source.rgb) / white);
  var out = fx_to_display(mapped);
  if (fx.params[0].y > 0.5) {
    out = out + vec3((fx_bayer(vec2<u32>(in.pos.xy)) - 0.5) / 255.0);
  }

  return vec4(out, source.a);
}
