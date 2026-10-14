// Bloom: `downsample_first` filters the image into the first mip with a 13 tap filter whose
// groups are weighted against fireflies (Karis average), clamps bright pixels and keeps what
// lies above the threshold with a soft knee; `downsample` filters every mip into the next,
// `upsample` adds a 3x3 tent of a mip into the one above it with the blend constant of the
// pass, `history` blends the first mip with the previous frame and `composite` adds a tent of
// the first mip (binding 4) to the image, times the blend of the first mip.
#include "include/effect.wgsl"

struct Bloom {
    // threshold, threshold - knee, 2 * knee, 0.25 / knee
    threshold: vec4f,
    // x: firefly clamp, 0 for none, y: blend of the first mip, z: history blend
    params: vec4f,
    // xy: offset of the taps of the first downsample in uv, half a texel of the first mip
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

fn tap(uv: vec2f, offset: vec2f, stride: vec2f) -> vec3f {
    let c = textureSampleLevel(input_texture, input_sampler, uv + offset * stride, 0.0).rgb;
    return max(c, vec3f(0.0));
}

fn sample13(uv: vec2f, stride: vec2f, weighted: bool) -> vec3f {
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
    var g0 = (a + b + d + e) * 0.25;
    var g1 = (b + c + e + f) * 0.25;
    var g2 = (d + e + g + h) * 0.25;
    var g3 = (e + f + h + i) * 0.25;
    var g4 = (j + k + l + m) * 0.25;
    if (weighted) {
        g0 = karis(clamp_firefly(g0));
        g1 = karis(clamp_firefly(g1));
        g2 = karis(clamp_firefly(g2));
        g3 = karis(clamp_firefly(g3));
        g4 = karis(clamp_firefly(g4));
    }
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

@fragment fn downsample_first(v: FullscreenOut) -> @location(0) vec4f {
    let stride = max(1.0 / vec2f(textureDimensions(input_texture, 0)), bloom.first.xy);
    let c = sample13(v.uv, stride, true);
    return vec4f(soft_threshold(c), 1.0);
}

@fragment fn downsample(v: FullscreenOut) -> @location(0) vec4f {
    let stride = 1.0 / vec2f(textureDimensions(input_texture, 0));
    return vec4f(sample13(v.uv, stride, false), 1.0);
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
    let glow = tent(aux_texture, v.uv) * bloom.params.y;
    return vec4f(src.rgb + glow, src.a);
}
