// Color grading: white balance and exposure as one matrix, contrast, saturation, vibrance,
// lift/gamma/gain and a 2D strip LUT, in that order. A step at its default is skipped, so the
// defaults leave the image as is.
//
// The LUT at binding 4 is N slices of NxN side by side: red to the right within a slice, green
// downward, blue from slice to slice. It is sampled as stored, sRGB encoded, with bilinear
// filtering within the two nearest slices blended by blue, on the color clamped to 0..1 and
// encoded, and its result is decoded back to linear.
#include "include/effect.wgsl"

struct Grading {
    // Rows of the white balance matrix times the exposure, w unused
    balance0: vec4f,
    balance1: vec4f,
    balance2: vec4f,
    // contrast, saturation, vibrance, 1 when the input is in display range
    params: vec4f,
    // rgb: lift, a: LUT strength
    lift: vec4f,
    // rgb: gain, a: slices of the LUT, 0 for none
    gain: vec4f,
    // rgb: 1 / gamma, a: 1 when lift or gamma change the color
    inv_gamma: vec4f,
};

@group(0) @binding(2) var<uniform> grading: Grading;
@group(0) @binding(4) var lut_texture: texture_2d<f32>;

// Log2 of the middle grey contrast pivots around in HDR
const LOG_GREY: f32 = -2.4739312;

fn srgb_encode(c: vec3f) -> vec3f {
    return select(1.055 * pow(c, vec3f(1.0 / 2.4)) - 0.055, c * 12.92, c <= vec3f(0.0031308));
}

fn srgb_decode(c: vec3f) -> vec3f {
    return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

// Blend toward the luminance (0) or away from it, clamped at 0.
fn saturate_color(c: vec3f, amount: f32) -> vec3f {
    let luma = luminance(c);
    return max(vec3f(luma) + (c - vec3f(luma)) * amount, vec3f(0.0));
}

// Bilinear sample of a slice of the strip at an encoded red and green.
fn lut_slice(e: vec2f, slice: f32, n: f32) -> vec3f {
    let x = (slice * n + e.x * (n - 1.0) + 0.5) / (n * n);
    let y = (e.y * (n - 1.0) + 0.5) / n;
    return textureSampleLevel(lut_texture, input_sampler, vec2f(x, y), 0.0).rgb;
}

fn lut_sample(e: vec3f, n: f32) -> vec3f {
    let b = e.b * (n - 1.0);
    let s0 = floor(b);
    let s1 = min(s0 + 1.0, n - 1.0);
    return mix(lut_slice(e.rg, s0, n), lut_slice(e.rg, s1, n), b - s0);
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    var c = vec3f(
        dot(grading.balance0.xyz, src.rgb),
        dot(grading.balance1.xyz, src.rgb),
        dot(grading.balance2.xyz, src.rgb));

    let contrast = grading.params.x;
    if (contrast != 1.0) {
        if (grading.params.w > 0.5) {
            c = (c - 0.5) * contrast + 0.5;
        } else {
            let graded = exp2((log2(max(c, vec3f(1e-20))) - LOG_GREY) * contrast + LOG_GREY);
            c = select(vec3f(0.0), graded, c > vec3f(0.0));
        }

        c = max(c, vec3f(0.0));
    }

    if (grading.params.y != 1.0) {
        c = saturate_color(c, grading.params.y);
    }

    let vibrance = grading.params.z;
    if (vibrance != 0.0) {
        let hi = max(c.r, max(c.g, c.b));
        let lo = min(c.r, min(c.g, c.b));
        let s = select(0.0, clamp((hi - lo) / hi, 0.0, 1.0), hi > 1e-6);
        c = saturate_color(c, 1.0 + vibrance * (1.0 - s));
    }

    c = c * grading.gain.rgb + grading.lift.rgb;
    if (grading.inv_gamma.a > 0.5) {
        c = select(vec3f(0.0), pow(max(c, vec3f(1e-20)), grading.inv_gamma.rgb), c > vec3f(0.0));
    }

    let n = grading.gain.a;
    if (n > 1.0) {
        let e = srgb_encode(clamp(c, vec3f(0.0), vec3f(1.0)));
        let graded = srgb_decode(lut_sample(e, n));
        c = mix(c, graded, grading.lift.a);
    }

    return vec4f(c, src.a);
}
