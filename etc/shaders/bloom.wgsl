#include "include/effect.wgsl"
#include "include/bloom.wgsl"

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  let base = fx_to_linear(source.rgb);
  let bloom = bloom_tent(fx_setup0, fx_sampler, in.uv);
  return vec4(fx_to_display(base + bloom * max(fx.params[0].x, 0.0)), source.a);
}
