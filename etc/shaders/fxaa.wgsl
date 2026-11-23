// Fast approximate anti aliasing (FXAA 3.11, Timothy Lottes): one pass over the image of a
// view. A pixel whose luminance differs enough from its four neighbors lies on an edge: the pass
// finds the direction of the edge, walks along it to both of its ends and takes the image a
// fraction of a pixel across the edge, by where the pixel lies between the ends. A pixel that
// stands out from all of its neighbors is blended into them by the subpixel amount.
//
// The luminance is the square root of that of the color clamped to 0..1, close to a display
// encoding. Alpha is that of the image where the pass reads it.
#include "include/effect.wgsl"

struct Fxaa {
    // edge threshold, least edge threshold, subpixel blend, unused
    params: vec4f,
    // xy: the size of a pixel in uv
    texel: vec4f,
};

@group(0) @binding(2) var<uniform> fxaa: Fxaa;

// Search steps along an edge to either side, and their lengths in pixels.
const FXAA_STEPS: u32 = 8u;

fn fxaa_step(i: u32) -> f32 {
    return select(select(select(8.0, 4.0, i < 7u), 2.0, i < 5u), 1.0, i < 2u);
}

fn fxaa_luma(uv: vec2f) -> f32 {
    let c = textureSampleLevel(input_texture, input_sampler, uv, 0.0).rgb;
    return sqrt(luminance(clamp(c, vec3f(0.0), vec3f(1.0))));
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let t = fxaa.texel.xy;
    let uv = v.uv;
    let center = textureSampleLevel(input_texture, input_sampler, uv, 0.0);
    let m = sqrt(luminance(clamp(center.rgb, vec3f(0.0), vec3f(1.0))));
    let n = fxaa_luma(uv + vec2f(0.0, -t.y));
    let s = fxaa_luma(uv + vec2f(0.0, t.y));
    let w = fxaa_luma(uv + vec2f(-t.x, 0.0));
    let e = fxaa_luma(uv + vec2f(t.x, 0.0));
    let high = max(m, max(max(n, s), max(w, e)));
    let low = min(m, min(min(n, s), min(w, e)));
    let range = high - low;
    if (range < max(fxaa.params.y, high * fxaa.params.x)) {
        return center;
    }

    let nw = fxaa_luma(uv + vec2f(-t.x, -t.y));
    let ne = fxaa_luma(uv + vec2f(t.x, -t.y));
    let sw = fxaa_luma(uv + vec2f(-t.x, t.y));
    let se = fxaa_luma(uv + vec2f(t.x, t.y));
    let across = abs(nw + sw - 2.0 * w) + 2.0 * abs(n + s - 2.0 * m) + abs(ne + se - 2.0 * e);
    let along = abs(nw + ne - 2.0 * n) + 2.0 * abs(w + e - 2.0 * m) + abs(sw + se - 2.0 * s);
    let horizontal = across >= along;
    let before = select(w, n, horizontal);
    let after = select(e, s, horizontal);
    let steeper = abs(before - m) >= abs(after - m);
    let scaled = 0.25 * max(abs(before - m), abs(after - m));
    let side = select(1.0, -1.0, steeper) * select(t.x, t.y, horizontal);
    let average = 0.5 * (select(after, before, steeper) + m);
    let step = select(vec2f(0.0, t.y), vec2f(t.x, 0.0), horizontal);
    let start = uv + select(vec2f(side, 0.0), vec2f(0.0, side), horizontal) * 0.5;
    var back = start;
    var front = start;
    var end_back = 0.0;
    var end_front = 0.0;
    var done_back = false;
    var done_front = false;
    for (var i = 0u; i < FXAA_STEPS; i++) {
        let length = fxaa_step(i);
        if (!done_back) {
            back -= step * length;
            end_back = fxaa_luma(back) - average;
            done_back = abs(end_back) >= scaled;
        }

        if (!done_front) {
            front += step * length;
            end_front = fxaa_luma(front) - average;
            done_front = abs(end_front) >= scaled;
        }

        if (done_back && done_front) {
            break;
        }
    }

    let to_back = select(uv.y - back.y, uv.x - back.x, horizontal);
    let to_front = select(front.y - uv.y, front.x - uv.x, horizontal);
    let nearer_back = to_back < to_front;
    let end = select(end_front, end_back, nearer_back);
    let lower = m < average;
    var offset = 0.0;
    if ((end < 0.0) != lower) {
        offset = 0.5 - min(to_back, to_front) / max(to_back + to_front, 1e-6);
    }

    let around = (2.0 * (n + s + w + e) + nw + ne + sw + se) / 12.0;
    let alone = clamp(abs(around - m) / range, 0.0, 1.0);
    let smooth_alone = (3.0 - 2.0 * alone) * alone * alone;
    offset = max(offset, smooth_alone * smooth_alone * fxaa.params.z);
    let at = uv + select(vec2f(side, 0.0), vec2f(0.0, side), horizontal) * offset;
    return textureSampleLevel(input_texture, input_sampler, at, 0.0);
}
