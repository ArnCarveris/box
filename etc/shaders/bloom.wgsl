// Bloom: thresholded downsamples into a mip chain, tent upsamples, a history blend and a
// composite of the first mip into the image.
#include "include/effect.wgsl"

struct Bloom {
    // threshold, threshold - knee, 2 * knee, 0.25 / knee
    threshold: vec4f,
    // x: firefly clamp, 0 for none, y: blend of the first mip, z: history blend
    params: vec4f,
    // xy: size of a texel of the first mip in uv
    first: vec4f,
};

@group(0) @binding(2) var<uniform> bloom: Bloom;
@group(0) @binding(4) var aux_texture: texture_2d<f32>;
@group(0) @binding(5) var bloom_out: texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var<uniform> bloom_step: vec4f;

fn soft_threshold(color: vec3f) -> vec3f {
    let brightness = max(color.r, max(color.g, color.b));
    var softness = clamp(brightness - bloom.threshold.y, 0.0, bloom.threshold.z);
    softness = softness * softness * bloom.threshold.w;
    let contribution = max(brightness - bloom.threshold.x, softness);
    return color * (contribution / max(brightness, 1e-5));
}

fn clamp_firefly(color: vec3f) -> vec3f {
    if (bloom.params.x <= 0.0) {
        return color;
    }

    return color * min(1.0, bloom.params.x / max(luminance(color), 1e-5));
}

fn karis(color: vec3f) -> vec3f {
    return color / (1.0 + luminance(color) * 0.25);
}

// Clamp, weight and threshold one pixel of the image before the first downsample filters it.
fn prefilter(color: vec3f) -> vec3f {
    return soft_threshold(karis(clamp_firefly(max(color, vec3f(0.0)))));
}

// The 2x2 pixels around a pixel corner, each clamped, weighted and thresholded.
struct Quad {
    p00: vec3f,
    p10: vec3f,
    p01: vec3f,
    p11: vec3f,
};

fn quad(corner: vec2f, texel: vec2f) -> Quad {
    let uv = corner * texel;
    let r = textureGather(0, input_texture, input_sampler, uv);
    let g = textureGather(1, input_texture, input_sampler, uv);
    let b = textureGather(2, input_texture, input_sampler, uv);
    return Quad(prefilter(vec3f(r.w, g.w, b.w)), prefilter(vec3f(r.z, g.z, b.z)),
        prefilter(vec3f(r.x, g.x, b.x)), prefilter(vec3f(r.y, g.y, b.y)));
}

// Sum of a quad, its left and top pixels weighted with w0 and its right and bottom ones with w1.
fn quad_sum(q: Quad, w0: vec2f, w1: vec2f) -> vec3f {
    return q.p00 * (w0.x * w0.y) + q.p10 * (w1.x * w0.y) + q.p01 * (w0.x * w1.y) +
        q.p11 * (w1.x * w1.y);
}

fn tap(uv: vec2f, offset: vec2f, stride: vec2f) -> vec3f {
    let c = textureSampleLevel(input_texture, input_sampler, uv + offset * stride, 0.0).rgb;
    return max(c, vec3f(0.0));
}

fn sample13(uv: vec2f, stride: vec2f) -> vec3f {
    let a = tap(uv, vec2f(-2.0, 2.0), stride);
    let b = tap(uv, vec2f(0.0, 2.0), stride);
    let c = tap(uv, vec2f(2.0, 2.0), stride);
    let d = tap(uv, vec2f(-2.0, 0.0), stride);
    let e = tap(uv, vec2f(0.0, 0.0), stride);
    let f = tap(uv, vec2f(2.0, 0.0), stride);
    let g = tap(uv, vec2f(-2.0, -2.0), stride);
    let h = tap(uv, vec2f(0.0, -2.0), stride);
    let i = tap(uv, vec2f(2.0, -2.0), stride);
    let j = tap(uv, vec2f(-1.0, 1.0), stride);
    let k = tap(uv, vec2f(1.0, 1.0), stride);
    let l = tap(uv, vec2f(-1.0, -1.0), stride);
    let m = tap(uv, vec2f(1.0, -1.0), stride);
    let g0 = (a + b + d + e) * 0.25;
    let g1 = (b + c + e + f) * 0.25;
    let g2 = (d + e + g + h) * 0.25;
    let g3 = (e + f + h + i) * 0.25;
    let g4 = (j + k + l + m) * 0.25;
    return (g0 + g1 + g2 + g3) * 0.125 + g4 * 0.5;
}

fn tent(t: texture_2d<f32>, uv: vec2f) -> vec3f {
    let s = 1.0 / vec2f(textureDimensions(t, 0));
    var sum = textureSampleLevel(t, input_sampler, uv, 0.0).rgb * 4.0;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(-s.x, 0.0), 0.0).rgb * 2.0;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(s.x, 0.0), 0.0).rgb * 2.0;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(0.0, -s.y), 0.0).rgb * 2.0;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(0.0, s.y), 0.0).rgb * 2.0;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(-s.x, -s.y), 0.0).rgb;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(s.x, -s.y), 0.0).rgb;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(-s.x, s.y), 0.0).rgb;
    sum += textureSampleLevel(t, input_sampler, uv + vec2f(s.x, s.y), 0.0).rgb;
    return sum / 16.0;
}

// Weight of the cubic B-spline at a distance in texels, 0 from 2 texels on.
fn bspline_weight(d: f32) -> f32 {
    let x = abs(d);
    if (x < 1.0) {
        return 2.0 / 3.0 - x * x * (1.0 - 0.5 * x);
    }

    let r = max(2.0 - x, 0.0);
    return r * r * r / 6.0;
}

// Cubic B-spline of an image from 4 bilinear taps, smooth across texels however much it is
// upscaled.
fn bspline(t: texture_2d<f32>, uv: vec2f) -> vec3f {
    let size = vec2f(textureDimensions(t, 0));
    let p = uv * size - 0.5;
    let i = floor(p);
    let f = p - i;
    let r = 1.0 - f;
    let w0 = r * r * r / 6.0;
    let w3 = f * f * f / 6.0;
    let w1 = 2.0 / 3.0 - 0.5 * f * f * (2.0 - f);
    let w2 = 1.0 - w0 - w1 - w3;
    let g0 = w0 + w1;
    let g1 = w2 + w3;
    let lo = (i - 0.5 + w1 / g0) / size;
    let hi = (i + 1.5 + w3 / g1) / size;
    var sum = textureSampleLevel(t, input_sampler, vec2f(lo.x, lo.y), 0.0).rgb * g0.x * g0.y;
    sum += textureSampleLevel(t, input_sampler, vec2f(hi.x, lo.y), 0.0).rgb * g1.x * g0.y;
    sum += textureSampleLevel(t, input_sampler, vec2f(lo.x, hi.y), 0.0).rgb * g0.x * g1.y;
    sum += textureSampleLevel(t, input_sampler, vec2f(hi.x, hi.y), 0.0).rgb * g1.x * g1.y;
    return sum;
}

fn downsample_first_at(pos: vec2f) -> vec3f {
    // The 13 tap filter at a 2x reduction, its taps at pixel corners, as weights of the 6x6
    // pixels under it: half the outer 3x3 taps (a) and half the inner 2x2 taps (b).
    var a = array<f32, 6>(0.125, 0.125, 0.25, 0.25, 0.125, 0.125);
    var b = array<f32, 6>(0.0, 0.25, 0.25, 0.25, 0.25, 0.0);
    let texel = 1.0 / vec2f(textureDimensions(input_texture, 0));
    let corner = floor(pos) * 2.0 - 1.0;
    var sum = vec3f(0.0);
    for (var y = 0; y < 6; y += 2) {
        for (var x = 0; x < 6; x += 2) {
            let q = quad(corner + vec2f(f32(x), f32(y)), texel);
            let a0 = vec2f(a[x], a[y]);
            let a1 = vec2f(a[x + 1], a[y + 1]);
            let b0 = vec2f(b[x], b[y]);
            let b1 = vec2f(b[x + 1], b[y + 1]);
            sum += (quad_sum(q, a0, a1) + quad_sum(q, b0, b1)) * 0.5;
        }
    }

    return sum;
}

fn downsample_first_tent_at(pos: vec2f) -> vec3f {
    // A 4x4 tent at a 2x reduction, 1 3 3 1 / 8 along each axis, as 2x2 quads
    let texel = 1.0 / vec2f(textureDimensions(input_texture, 0));
    let corner = floor(pos) * 2.0;
    let outer = vec2f(0.125);
    let inner = vec2f(0.375);
    var sum = quad_sum(quad(corner, texel), outer, inner);
    sum += quad_sum(quad(corner + vec2f(2.0, 0.0), texel), vec2f(0.375, 0.125),
        vec2f(0.125, 0.375));
    sum += quad_sum(quad(corner + vec2f(0.0, 2.0), texel), vec2f(0.125, 0.375),
        vec2f(0.375, 0.125));
    sum += quad_sum(quad(corner + vec2f(2.0, 2.0), texel), inner, outer);
    return sum;
}

fn downsample_fit_at(uv: vec2f) -> vec3f {
    // Every texel of the input weighted with the B-spline at its distance in texels of the
    // first mip, 1 to 2 input texels each. Two texels next to each other are one bilinear tap
    // between them, so a tap reads 2x2 texels.
    let size = vec2f(textureDimensions(input_texture, 0));
    let ratio = size * bloom.first.xy;
    let center = uv * size;
    let lo = ceil(center - 2.0 * ratio - 0.5);
    let hi = floor(center + 2.0 * ratio - 0.5);
    var sum = vec3f(0.0);
    var total = 0.0;
    for (var y = lo.y; y <= hi.y; y += 2.0) {
        let y0 = bspline_weight((y + 0.5 - center.y) / ratio.y);
        let y1 = bspline_weight((y + 1.5 - center.y) / ratio.y);
        let wy = y0 + y1;
        for (var x = lo.x; x <= hi.x; x += 2.0) {
            let x0 = bspline_weight((x + 0.5 - center.x) / ratio.x);
            let x1 = bspline_weight((x + 1.5 - center.x) / ratio.x);
            let w = (x0 + x1) * wy;
            if (w > 0.0) {
                let p = vec2f(x + 0.5 + x1 / (x0 + x1), y + 0.5 + y1 / wy);
                sum += textureSampleLevel(input_texture, input_sampler, p / size, 0.0).rgb * w;
                total += w;
            }
        }
    }

    return sum / max(total, 1e-5);
}

fn bloom_texel(id: vec3u) -> vec2f {
    return vec2f(id.xy) + 0.5;
}

fn bloom_inside(id: vec3u) -> bool {
    return all(id.xy < textureDimensions(bloom_out));
}

fn bloom_uv(id: vec3u) -> vec2f {
    return bloom_texel(id) / vec2f(textureDimensions(bloom_out));
}

@compute @workgroup_size(8, 8) fn downsample_first(@builtin(global_invocation_id) id: vec3u) {
    if (bloom_inside(id)) {
        textureStore(bloom_out, id.xy, vec4f(downsample_first_at(bloom_texel(id)), 1.0));
    }
}

@compute @workgroup_size(8, 8) fn downsample_first_tent(
    @builtin(global_invocation_id) id: vec3u)
{
    if (bloom_inside(id)) {
        textureStore(bloom_out, id.xy, vec4f(downsample_first_tent_at(bloom_texel(id)), 1.0));
    }
}

@compute @workgroup_size(8, 8) fn downsample(@builtin(global_invocation_id) id: vec3u) {
    if (bloom_inside(id)) {
        let stride = 1.0 / vec2f(textureDimensions(input_texture, 0));
        textureStore(bloom_out, id.xy, vec4f(sample13(bloom_uv(id), stride), 1.0));
    }
}

@compute @workgroup_size(8, 8) fn downsample_fit(@builtin(global_invocation_id) id: vec3u) {
    if (bloom_inside(id)) {
        textureStore(bloom_out, id.xy, vec4f(downsample_fit_at(bloom_uv(id)), 1.0));
    }
}

@compute @workgroup_size(8, 8) fn upsample(@builtin(global_invocation_id) id: vec3u) {
    if (bloom_inside(id)) {
        let below = textureLoad(aux_texture, id.xy, 0).rgb;
        let glow = tent(input_texture, bloom_uv(id)) * bloom_step.x;
        textureStore(bloom_out, id.xy, vec4f(below + glow, 1.0));
    }
}

@fragment fn history(v: FullscreenOut) -> @location(0) vec4f {
    let current = textureSampleLevel(input_texture, input_sampler, v.uv, 0.0).rgb;
    let previous = textureSampleLevel(aux_texture, input_sampler, v.uv, 0.0).rgb;
    return vec4f(mix(current, previous, bloom.params.z), 1.0);
}

@fragment fn composite(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureSampleLevel(input_texture, input_sampler, v.uv, 0.0);
    let glow = bspline(aux_texture, v.uv) * bloom.params.y;
    return vec4f(src.rgb + glow, src.a);
}
