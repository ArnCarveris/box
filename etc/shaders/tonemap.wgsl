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
#include "include/tonemap_curve.wgsl"

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

// Noise in [0, 1) that differs between neighboring pixels (Jimenez 2014).
fn interleaved_gradient_noise(p: vec2f) -> f32 {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// Tony McMapface (Tomasz Stachowiak, MIT): the LUT sampled at c / (c + 1), inset by half a
// texel so 0 and 1 land on the centers of the first and last texels, plus a dither of up to
// half an 8 bit step against banding.
fn tony_mc_mapface(color: vec3f, pixel: vec2f) -> vec3f {
    let encoded = tony_encode(color);
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

    if (tonemap.flags.y == TONEMAP_PBR_NEUTRAL) {
        c = pbr_neutral(c);
    } else if (tonemap.flags.y == TONEMAP_TONY) {
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
