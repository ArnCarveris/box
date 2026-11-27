// The tone map operators as functions of the color alone, with their inverses: the tone map
// pass maps HDR color to the display range with them (tonemap.wgsl), and the 2D shapes of a
// tone mapped view take their colors back to the HDR values that map to them (shape2d.wgsl).
#pragma once

// Operator of View.display.y and of the tone map pass: none, Khronos PBR Neutral, Tony
// McMapface
const TONEMAP_NONE: u32 = 0u;
const TONEMAP_PBR_NEUTRAL: u32 = 1u;
const TONEMAP_TONY: u32 = 2u;

// Constants of PBR Neutral: the shoulder starts at start, d is the room above it, desaturation
// how much a rolled off color moves to white
const PBR_NEUTRAL_START: f32 = 0.76;
const PBR_NEUTRAL_D: f32 = 0.24;
const PBR_NEUTRAL_DESATURATION: f32 = 0.15;

// Khronos PBR Neutral: colors below the shoulder keep their value, brighter ones roll off to
// white with their hue and desaturate as they approach it.
fn pbr_neutral(color: vec3f) -> vec3f {
    let low = min(color.r, min(color.g, color.b));
    let offset = select(0.04, low - 6.25 * low * low, low < 0.08);
    var c = color - vec3f(offset);
    let peak = max(c.r, max(c.g, c.b));
    if (peak < PBR_NEUTRAL_START) {
        return c;
    }

    let d = PBR_NEUTRAL_D;
    let new_peak = 1.0 - d * d / (peak + d - PBR_NEUTRAL_START);
    c *= new_peak / peak;
    let g = 1.0 - 1.0 / (PBR_NEUTRAL_DESATURATION * (peak - new_peak) + 1.0);
    return mix(c, vec3f(new_peak), g);
}

// The inverse of pbr_neutral, exact for a display color of 0 to below 1: the mix toward white
// keeps the largest channel at the rolled off peak, so the peak, the desaturation and the scale
// of the shoulder follow from it, and the offset from the smallest channel before it.
fn pbr_neutral_inverse(display: vec3f) -> vec3f {
    let out = clamp(display, vec3f(0.0), vec3f(0.999));
    let new_peak = max(out.r, max(out.g, out.b));
    var c = out;
    if (new_peak >= PBR_NEUTRAL_START) {
        let d = PBR_NEUTRAL_D;
        let peak = PBR_NEUTRAL_START - d + d * d / (1.0 - new_peak);
        let g = 1.0 - 1.0 / (PBR_NEUTRAL_DESATURATION * (peak - new_peak) + 1.0);
        let scaled = (out - vec3f(g * new_peak)) / max(1.0 - g, 1e-6);
        c = scaled * (peak / new_peak);
    }

    // The offset is 0.04 from a smallest channel of 0.08 up, below it the smallest channel
    // after the offset is 6.25 low^2
    let shifted = max(min(c.r, min(c.g, c.b)), 0.0);
    let low = select(shifted + 0.04, 0.4 * sqrt(shifted), shifted < 0.04);
    let offset = low - shifted;
    return max(c + vec3f(offset), vec3f(0.0));
}

// The encoding Tony McMapface looks its LUT up at.
fn tony_encode(color: vec3f) -> vec3f {
    return color / (color + vec3f(1.0));
}

// The inverse of the encoding of Tony McMapface, which stands in for the inverse of the LUT,
// which has none in closed form: it takes the display color as the encoded one, so the color
// that comes back maps only near the display color, not to it.
fn tony_encode_inverse(display: vec3f) -> vec3f {
    let e = clamp(display, vec3f(0.0), vec3f(0.98));
    return e / (vec3f(1.0) - e);
}

// Largest display channel display_to_hdr takes back: near 1 the inverses of the curves grow
// without bound, at 0.98 PBR Neutral gives about 3.4 and the encoding of Tony McMapface 49.
const DISPLAY_MAX: f32 = 0.98;

// The HDR color of a view whose exposure scale and operator map back to a display color:
// the inverse of the operator tone divided by the scale.
fn display_to_hdr(display: vec3f, scale: f32, tone: u32) -> vec3f {
    var c = clamp(display, vec3f(0.0), vec3f(DISPLAY_MAX));
    if (tone == TONEMAP_PBR_NEUTRAL) {
        c = pbr_neutral_inverse(c);
    } else if (tone == TONEMAP_TONY) {
        c = tony_encode_inverse(c);
    }

    return c / max(scale, 1e-6);
}
