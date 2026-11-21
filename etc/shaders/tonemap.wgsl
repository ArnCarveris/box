// Tone map and exposure pass: scales the image by the adapted exposure when flags.x is set and
// maps it to the display range by the operator in flags.y: 1 the Khronos PBR Neutral curve, 2
// Tony McMapface. With neither it copies the image, which is how a view writes its HDR image
// into its target. The output is linear; the target encodes it.
//
// A color grading pass and a vignette pass that follow the tone map are applied here, in that
// order, instead of in passes of their own: flags.z has TONEMAP_GRADING and TONEMAP_VIGNETTE
// set for them, and the LUT of the grading is at binding 4.
#include "include/effect.wgsl"
#include "include/grading.wgsl"
#include "include/vignette.wgsl"

struct Tonemap {
    flags: vec4u,
    grading: Grading,
    vignette: Vignette,
};

const TONEMAP_GRADING: u32 = 1u;
const TONEMAP_VIGNETTE: u32 = 2u;

// Adapted exposure: x is the exposure in stops, y the scale 2^x.
struct Exposure {
    ev: f32,
    scale: f32,
    average: f32,
    ready: f32,
};

@group(0) @binding(2) var<uniform> tonemap: Tonemap;
@group(0) @binding(5) var<storage, read> exposure: Exposure;
// The Tony McMapface LUT (etc/luts/tony_mc_mapface.dds), sampled with the linear clamp sampler
// of binding 1; a placeholder for the other operators.
@group(0) @binding(6) var tony_lut: texture_3d<f32>;

// Khronos PBR Neutral: colors below the shoulder keep their value, brighter ones roll off to
// white with their hue and desaturate as they approach it.
fn pbr_neutral(color: vec3f) -> vec3f {
    let start = 0.8 - 0.04;
    let desaturation = 0.15;
    let low = min(color.r, min(color.g, color.b));
    let offset = select(0.04, low - 6.25 * low * low, low < 0.08);
    var c = color - vec3f(offset);
    let peak = max(c.r, max(c.g, c.b));
    if (peak < start) {
        return c;
    }

    let d = 1.0 - start;
    let new_peak = 1.0 - d * d / (peak + d - start);
    c *= new_peak / peak;
    let g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(c, vec3f(new_peak), g);
}

// Noise in [0, 1) that differs between neighboring pixels (Jimenez 2014).
fn interleaved_gradient_noise(p: vec2f) -> f32 {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// Tony McMapface (Tomasz Stachowiak, MIT): the LUT sampled at c / (c + 1), inset by half a
// texel so 0 and 1 land on the centers of the first and last texels, plus a dither of up to
// half an 8 bit step against banding.
fn tony_mc_mapface(color: vec3f, pixel: vec2f) -> vec3f {
    let encoded = color / (color + vec3f(1.0));
    let dims = vec3f(textureDimensions(tony_lut));
    let uv = clamp(encoded * ((dims - vec3f(1.0)) / dims) + (vec3f(0.5) / dims),
        vec3f(0.0), vec3f(1.0));
    let mapped = textureSampleLevel(tony_lut, input_sampler, uv, 0.0).rgb;
    let dither = (interleaved_gradient_noise(floor(pixel)) - 0.5) / 255.0;
    return clamp(mapped + vec3f(dither), vec3f(0.0), vec3f(1.0));
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureSampleLevel(input_texture, input_sampler, v.uv, 0.0);
    var c = max(src.rgb, vec3f(0.0));
    if (tonemap.flags.x != 0u) {
        c *= exposure.scale;
    }

    if (tonemap.flags.y == 1u) {
        c = pbr_neutral(c);
    } else if (tonemap.flags.y == 2u) {
        c = tony_mc_mapface(c, v.pos.xy);
    }

    if ((tonemap.flags.z & TONEMAP_GRADING) != 0u) {
        c = grade(tonemap.grading, c);
    }

    if ((tonemap.flags.z & TONEMAP_VIGNETTE) != 0u) {
        c = vignette_apply(tonemap.vignette, c, v.uv);
    }

    return vec4f(c, src.a);
}
