#include "include/effect.wgsl"

// Multiplies the image by the exposure the frame adapted from its histogram,
// which the tone map pass rolls back into the range of the display.
@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  let scaled = fx_to_linear(source.rgb) * fx_exposure_scale();
  return vec4(fx_to_display(scaled), source.a);
}
