#include "include/effect.wgsl"

// Mixes the pyramid the frame built of this image back into it. The pyramid
// holds every texel of the image, so the mix keeps the energy of the frame
// instead of adding what a threshold let through.
@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  let base = fx_to_linear(source.rgb);
  let bloom = fx_pyramid_sample(in.uv);
  let mixed = mix(base, bloom, clamp(fx.params[0].x, 0.0, 1.0));
  return vec4(fx_to_display(mixed), source.a);
}
