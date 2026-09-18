#include "include/effect.wgsl"

// The grade works on the image the way the display shows it, so the same
// exposure and saturation land the same whether the chain carries linear light
// or display encoded values.
@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  var rgb = fx_to_encoded(fx_to_linear(source.rgb)) * exp2(fx.params[0].x);
  let luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  rgb = max(mix(vec3(luma), rgb, fx.params[0].y), vec3(0.0));
  return vec4(fx_to_display(fx_from_encoded(rgb)), source.a);
}
