// Tone map and exposure pass: scales the image by the adapted exposure when flags.x is set and
// maps it to the display range with the Khronos PBR Neutral curve when flags.y is set. With
// neither it copies the image, which is how a view writes its HDR image into its target.
#include "include/effect.wgsl"

struct Tonemap {
    flags: vec4u,
};

// Adapted exposure: x is the exposure in stops, y the scale 2^x.
struct Exposure {
    ev: f32,
    scale: f32,
    average: f32,
    unused: f32,
};

@group(0) @binding(2) var<uniform> tonemap: Tonemap;
@group(0) @binding(5) var<storage, read> exposure: Exposure;

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

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureSampleLevel(input_texture, input_sampler, v.uv, 0.0);
    var c = max(src.rgb, vec3f(0.0));
    if (tonemap.flags.x != 0u) {
        c *= exposure.scale;
    }

    if (tonemap.flags.y != 0u) {
        c = pbr_neutral(c);
    }

    return vec4f(c, src.a);
}
