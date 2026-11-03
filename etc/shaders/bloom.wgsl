// Bloom: `downsample_first` halves the image exactly: it loads the 6x6 pixels under the 13 tap
// filter of every texel, clamps every pixel against fireflies, weights it with the Karis weight
// and keeps what lies above the threshold with a soft knee before it filters, so a small bright
// detail adds the same glow wherever it lands on the pixel grid. `downsample` filters a level
// into the next, half its size, with the 13 tap filter; it halves the prefilter levels between
// the half size image and the first mip and every mip into the next. `downsample_fit` filters
// the last prefilter level into the first mip, which is at most half its size, with the taps
// half a texel of the first mip apart. `upsample` adds a 3x3 tent of a mip into the one above
// it with the blend constant of the pass, `history` blends the first mip with the previous
// frame and `composite` adds a cubic B-spline of the first mip (binding 4) to the image, times
// the blend of the first mip, which stays smooth where the first mip is upscaled a lot.
#include "include/effect.wgsl"

struct Bloom {
    // threshold, threshold - knee, 2 * knee, 0.25 / knee
    threshold: vec4f,
    // x: firefly clamp, 0 for none, y: blend of the first mip, z: history blend
    params: vec4f,
    // xy: offset of the taps of downsample_fit in uv, half a texel of the first mip
    first: vec4f,
};

@group(0) @binding(2) var<uniform> bloom: Bloom;
@group(0) @binding(4) var aux_texture: texture_2d<f32>;

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

@fragment fn downsample_first(v: FullscreenOut) -> @location(0) vec4f {
    // The 13 tap filter at a 2x reduction, its taps at pixel corners, as weights of the 6x6
    // pixels under it: half the outer 3x3 taps (a) and half the inner 2x2 taps (b).
    var a = array<f32, 6>(0.125, 0.125, 0.25, 0.25, 0.125, 0.125);
    var b = array<f32, 6>(0.0, 0.25, 0.25, 0.25, 0.25, 0.0);
    let size = vec2i(textureDimensions(input_texture, 0));
    let base = vec2i(floor(v.pos.xy)) * 2 - 2;
    var sum = vec3f(0.0);
    for (var y = 0; y < 6; y++) {
        let py = clamp(base.y + y, 0, size.y - 1);
        for (var x = 0; x < 6; x++) {
            let px = clamp(base.x + x, 0, size.x - 1);
            let c = textureLoad(input_texture, vec2i(px, py), 0).rgb;
            sum += prefilter(c) * (0.5 * (a[x] * a[y] + b[x] * b[y]));
        }
    }

    return vec4f(sum, 1.0);
}

@fragment fn downsample(v: FullscreenOut) -> @location(0) vec4f {
    let stride = 1.0 / vec2f(textureDimensions(input_texture, 0));
    return vec4f(sample13(v.uv, stride), 1.0);
}

@fragment fn downsample_fit(v: FullscreenOut) -> @location(0) vec4f {
    return vec4f(sample13(v.uv, bloom.first.xy), 1.0);
}

@fragment fn upsample(v: FullscreenOut) -> @location(0) vec4f {
    return vec4f(tent(input_texture, v.uv), 1.0);
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
