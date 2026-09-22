#include "include/effect.wgsl"

// The chain runs after the lighting, so the pass cannot reach the ambient term
// of a pixel directly. The normal attachment of the mesh pass carries the
// fraction of a pixel that came from the environment and the probes, so this
// pass scales that fraction by the occlusion and leaves every direct source,
// the emissive included, exactly where it was.
const AO_TOLERANCE: f32 = 0.08;

// Depth aware upsample: the occlusion texture keeps the view distance of the
// texel it traced next to the occlusion, so a texel of another surface is
// weighted away instead of bleeding across the silhouette.
fn gtao_upsample(uv: vec2<f32>, dist: f32) -> f32 {
  let size = vec2<f32>(textureDimensions(fx_setup0, 0));
  let texel = vec2(1.0) / max(size, vec2(1.0));
  var sum = 0.0;
  var weight = 0.0;
  for (var y = 0; y < 2; y = y + 1) {
    for (var x = 0; x < 2; x = x + 1) {
      let offset = (vec2(f32(x), f32(y)) - vec2(0.5)) * texel;
      let tap = textureSampleLevel(fx_setup0, fx_sampler, uv + offset, 0.0);
      var w = 0.0001;
      if (tap.y > 0.0) {
        w = 1.0 / (1.0 + abs(tap.y - dist) / max(dist * AO_TOLERANCE, 0.01));
      }
      sum = sum + tap.x * w;
      weight = weight + w;
    }
  }

  return sum / max(weight, 0.00001);
}

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  let depth = fx_view_depth(in.uv);
  var rgb = fx_to_linear(source.rgb);
  if (depth > 0.0) {
    let dist = length(fx_world_pos(in.uv, depth) - fx.eye.xyz);
    let power = max(fx.params[0].w, 0.0001);
    let ao = pow(clamp(gtao_upsample(in.uv, dist), 0.0, 1.0), power);
    let indirect = fx_indirect(in.uv).x;
    rgb = rgb * (1.0 - indirect * (1.0 - ao));
  }

  return vec4(fx_to_display(rgb), source.a);
}
