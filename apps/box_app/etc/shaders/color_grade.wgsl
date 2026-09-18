#include "include/effect.wgsl"

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  var rgb = source.rgb * exp2(fx.params[0].x);
  let luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  rgb = max(mix(vec3(luma), rgb, fx.params[0].y), vec3(0.0));
  return vec4(rgb, source.a);
}
