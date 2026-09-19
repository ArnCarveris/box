#include "include/effect.wgsl"

// Screen space reflections. Every pixel marches its reflection ray through the
// world, projects each step into the image and calls it a crossing when the ray
// passed behind the surface the depth target shows there while the step before
// it was still in front of that surface; the crossing is then refined by
// bisection. A crossing is found from the change of side alone, so a step that
// lands far behind a steeply receding surface is still a hit and the phase of
// the per pixel step offset cannot turn one pixel into a miss while its
// neighbour hits. How far behind the refined crossing landed, measured against
// the distance the ray and the surface both moved over that step, becomes the
// confidence of the hit, which fades it into the prefiltered environment
// instead of stepping to it.
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

// Fraction of the view distance a ray starts off its own surface, so a grazing
// reflection does not find the pixel it left.
const SSR_BIAS: f32 = 0.004;

// Fraction of its span a ray fades out over at the end, so the pixels that hit
// on their last step and the pixels that ran out both reach the environment.
const SSR_TRAVEL: f32 = 0.7;

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

fn ssr_behind(ray: f32, scene: f32) -> bool {
  return scene > 0.0 && ray > scene;
}

// Confidence a crossing keeps: one while it landed within the tolerance the
// step allows, falling to zero one tolerance further behind.
fn ssr_confidence(gap: f32, tolerance: f32) -> f32 {
  let tol = max(tolerance, 0.000001);
  return clamp(1.0 - max(gap - tol, 0.0) / tol, 0.0, 1.0);
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

// How far a texture coordinate is from the nearest border of the image.
fn ssr_border(uv: vec2<f32>) -> f32 {
  return min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
}

// The fade follows the whole path of a ray rather than the texel it hit, so a
// ray that leaves the image finds its neighbours already faded to the same
// environment instead of standing out as a single missed pixel.
fn ssr_edge_fade(border: f32, fade: f32) -> f32 {
  return smoothstep(0.0, max(fade, 0.0001), border);
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
    let jitter = 0.5;

    let origin = p + n * max(length(p - eye) * SSR_BIAS, 0.01);
    var low = 0.0;
    var high = 0.0;
    var hit = vec2<f32>(0.0);
    var tolerance = thickness;
    var previous = 0.0;
    var border = 1.0;
    var front = true;
    var found = false;
    for (var i = 0; i < steps; i = i + 1) {
      let t = span * (f32(i) + jitter) / f32(steps);
      let point = origin + r * t;
      let proj = ssr_project(point);
      if (proj.z <= 0.0 || any(proj.xy < vec2(0.0)) || any(proj.xy > vec2(1.0))) {
        border = 0.0;
        break;
      }

      border = min(border, ssr_border(proj.xy));
      let scene = ssr_scene(proj.xy);
      let ray = length(point - eye);
      if (ssr_behind(ray, scene)) {
        if (front) {
          low = previous;
          high = t;
          hit = proj.xy;
          tolerance = max(thickness, (t - previous) + abs(ray - scene));
          found = true;
          break;
        }
      } else {
        front = true;
        previous = t;
      }
    }

    var reflection = ambient;
    if (found) {
      for (var k = 0; k < SSR_REFINE; k = k + 1) {
        let mid = (low + high) * 0.5;
        let point = origin + r * mid;
        let proj = ssr_project(point);
        let scene = ssr_scene(proj.xy);
        if (ssr_behind(length(point - eye), scene)) {
          high = mid;
          hit = proj.xy;
        } else {
          low = mid;
        }
      }

      let point = origin + r * high;
      let gap = length(point - eye) - ssr_scene(hit);
      let confidence = ssr_confidence(gap, tolerance) *
        ssr_edge_fade(border, fx.params[1].x) *
        (1.0 - smoothstep(SSR_TRAVEL, 1.0, high / span));
      reflection = mix(ambient, ssr_gather(hit, roughness), confidence);
    }

    let ratio = clamp(reflection / max(ambient, vec3(0.001)), vec3(0.0),
      vec3(SSR_RATIO_MAX));
    rgb = max(rgb * (vec3(1.0) + weight * (ratio - vec3(1.0))), vec3(0.0));
  }

  return vec4(fx_to_display(rgb), source.a);
}
