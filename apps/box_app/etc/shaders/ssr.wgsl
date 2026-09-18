#include "include/effect.wgsl"

// Screen space reflections. Every pixel marches its reflection ray through the
// world, projects each step into the image and calls it a hit when the ray has
// travelled past the surface the depth target shows there but by less than the
// thickness the pass allows; the crossing is then refined by bisection.
//
// The chain runs after the lighting, so the pass cannot reach the specular term
// of a pixel directly. The normal attachment of the mesh pass carries the
// fraction of a pixel that came from the prefiltered environment and the
// roughness it was filtered at, so the pass divides the reflection it found by
// that same prefiltered radiance and scales the fraction by the result. A miss
// keeps the prefiltered radiance, which leaves the pixel exactly as the mesh
// pass shaded it and is why the traced and the untraced parts of the image meet
// without a boundary.
//
// The depth pyramid of a view keeps the farthest depth of every region, because
// it exists to hide instances behind walls. That bound cannot tell a march that
// a region is free of nearer surfaces, so it would skip real hits; the march
// reads the depth target instead and spends its budget on the bisection.

// Bisections the march refines its crossing with.
const SSR_REFINE: i32 = 5;

// Longest a ray travels, in world units.
const SSR_MAX_DISTANCE: f32 = 40.0;

// Most the reflection may brighten the specular fraction of a pixel by.
const SSR_RATIO_MAX: f32 = 8.0;

// Texels the neighbourhood of a rough hit reaches over at full roughness.
const SSR_BLUR_TEXELS: f32 = 6.0;

// Texture coordinate and view space depth of a world point.
fn ssr_project(p: vec3<f32>) -> vec3<f32> {
  let clip = fx.clip * vec4(p, 1.0);
  if (clip.w <= 0.0001) {
    return vec3(0.0, 0.0, -1.0);
  }

  let ndc = clip.xyz / clip.w;
  return vec3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, clip.w);
}

// View distance of the surface the image shows at a texel, zero where the frame
// drew nothing.
fn ssr_scene(uv: vec2<f32>) -> f32 {
  let depth = fx_view_depth(uv);
  if (depth <= 0.0) {
    return 0.0;
  }

  return length(fx_world_pos(uv, depth) - fx.eye.xyz);
}

fn ssr_behind(ray: f32, scene: f32, thickness: f32) -> bool {
  return scene > 0.0 && ray > scene && ray - scene < thickness;
}

// A rough surface reads a wider neighbourhood of the image instead of tracing
// more rays, which is what the roughness of its lobe would have gathered.
fn ssr_gather(uv: vec2<f32>, roughness: f32) -> vec3<f32> {
  let radius = roughness * SSR_BLUR_TEXELS * fx.size.zw;
  var sum = fx_to_linear(fx_sample(uv).rgb);
  sum = sum + fx_to_linear(fx_sample(uv + vec2(radius.x, 0.0)).rgb);
  sum = sum + fx_to_linear(fx_sample(uv - vec2(radius.x, 0.0)).rgb);
  sum = sum + fx_to_linear(fx_sample(uv + vec2(0.0, radius.y)).rgb);
  sum = sum + fx_to_linear(fx_sample(uv - vec2(0.0, radius.y)).rgb);
  return sum / 5.0;
}

fn ssr_edge_fade(uv: vec2<f32>, fade: f32) -> f32 {
  let edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
  return smoothstep(0.0, max(fade, 0.0001), edge);
}

@fragment fn fs_main(in: FxOut) -> @location(0) vec4<f32> {
  let source = fx_sample(in.uv);
  var rgb = fx_to_linear(source.rgb);
  let depth = fx_view_depth(in.uv);
  let indirect = fx_indirect(in.uv);
  let max_roughness = max(fx.params[0].z, 0.0001);
  let roughness = indirect.z;
  let weight = indirect.y * (1.0 - smoothstep(max_roughness * 0.5, max_roughness,
    roughness)) * max(fx.params[0].w, 0.0);
  if (depth > 0.0 && weight > 0.001) {
    let eye = fx.eye.xyz;
    let p = fx_world_pos(in.uv, depth);
    let n = fx_world_normal(in.uv);
    let v = normalize(eye - p);
    let r = reflect(-v, n);
    let ambient = fx_env_sample(r, roughness);
    let steps = max(i32(fx.params[0].x), 4);
    let thickness = max(fx.params[0].y, 0.001);
    let span = clamp(length(p - eye) * 2.0, 2.0, SSR_MAX_DISTANCE);
    let jitter = fx_bayer(vec2<u32>(in.pos.xy));

    var low = 0.0;
    var high = 0.0;
    var hit = vec2<f32>(0.0);
    var found = false;
    for (var i = 1; i <= steps; i = i + 1) {
      let t = span * (f32(i) - 1.0 + jitter) / f32(steps);
      let point = p + r * t;
      let proj = ssr_project(point);
      if (proj.z <= 0.0 || any(proj.xy < vec2(0.0)) || any(proj.xy > vec2(1.0))) {
        break;
      }

      if (ssr_behind(length(point - eye), ssr_scene(proj.xy), thickness + t * 0.05)) {
        high = t;
        hit = proj.xy;
        found = true;
        break;
      }

      low = t;
    }

    if (found) {
      for (var k = 0; k < SSR_REFINE; k = k + 1) {
        let mid = (low + high) * 0.5;
        let point = p + r * mid;
        let proj = ssr_project(point);
        let scene = ssr_scene(proj.xy);
        if (scene > 0.0 && length(point - eye) > scene) {
          high = mid;
          hit = proj.xy;
        } else {
          low = mid;
        }
      }
    }

    var reflection = ambient;
    if (found) {
      let gathered = ssr_gather(hit, roughness);
      let fade = ssr_edge_fade(hit, fx.params[1].x);
      reflection = mix(ambient, gathered, fade);
    }

    let ratio = clamp(reflection / max(ambient, vec3(0.001)), vec3(0.0),
      vec3(SSR_RATIO_MAX));
    rgb = max(rgb * (vec3(1.0) + weight * (ratio - vec3(1.0))), vec3(0.0));
  }

  return vec4(fx_to_display(rgb), source.a);
}
