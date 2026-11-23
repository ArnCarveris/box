// Draw range of the instances of a batch (DrawRange, box_draws_range): near, far and fade in xyz
// of the cull job of the batch. The cull pass measures the distance from the camera of its group
// to the origin of an instance, the translation of its model matrix, drops the instances out of
// range and packs the dither code of the others above the slot it writes to the visible slots,
// which the vertex stages read as their slot attribute (slot_of, range_of).
#pragma once

// Bits of a visible slot that hold the slot of the instance page; a page holds at most
// BOX_DRAWS_PAGE_BYTES / 48 slots, fewer than 2^22. The dither code is above them.
const RANGE_SLOT_BITS: u32 = 22u;
const RANGE_SLOT_MASK: u32 = (1u << RANGE_SLOT_BITS) - 1u;

// Dither code bit of a fade out: a pixel draws when its noise is not below the threshold.
const RANGE_FADE_OUT: u32 = 256u;

// Slot of the instance page of a visible slot.
fn slot_of(packed: u32) -> u32 {
    return packed & RANGE_SLOT_MASK;
}

// Dither code of a visible slot, 0 to draw every pixel.
fn range_of(packed: u32) -> u32 {
    return packed >> RANGE_SLOT_BITS;
}

// Share of the fade band around edge a distance is at: 0 up to edge - fade, 1 from edge + fade,
// a step at edge without a fade.
fn range_share(distance: f32, edge: f32, fade: f32) -> f32 {
    if (fade <= 0.0) {
        return select(0.0, 1.0, distance >= edge);
    }
    return clamp((distance - (edge - fade)) / (2.0 * fade), 0.0, 1.0);
}

// Whether a range is set: a near or a far distance.
fn range_set(range: vec4f) -> bool {
    return range.x > 0.0 || range.y > 0.0;
}

// Fade in and fade out thresholds of a range at a distance, in 255ths: a pixel draws when its
// noise is below the fade in and not below the fade out threshold. Both are quantized the same
// way, so a fade out and a fade in of the same share draw complementary pixels.
fn range_thresholds(range: vec4f, distance: f32) -> vec2u {
    var result = vec2u(255u, 0u);
    if (range.x > 0.0) {
        result.x = u32(round(range_share(distance, range.x, range.z) * 255.0));
    }
    if (range.y > 0.0) {
        result.y = u32(round(range_share(distance, range.y, range.z) * 255.0));
    }
    return result;
}

// Whether thresholds draw no pixel: out of range.
fn range_hidden(t: vec2u) -> bool {
    return t.x <= t.y;
}

// Dither code of thresholds that draw: 0 for every pixel, else the threshold in the low byte,
// with RANGE_FADE_OUT for a fade out. An instance in a fade in and a fade out band at once, when
// the bands overlap, keeps the share of both as a fade out.
fn range_code(t: vec2u) -> u32 {
    var result = 0u;
    if (t.y > 0u && t.x < 255u) {
        result = (255u - (t.x - t.y)) | RANGE_FADE_OUT;
    } else if (t.y > 0u) {
        result = t.y | RANGE_FADE_OUT;
    } else if (t.x < 255u) {
        result = t.x;
    }
    return result;
}

// Screen door noise of a pixel in 255ths, 0 to below 255: interleaved gradient noise of its
// integer position, the same in every frame and every pass.
fn range_noise(pos: vec2f) -> f32 {
    let p = floor(pos);
    return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715)))) * 255.0;
}

// Whether a pixel at a framebuffer position draws for a dither code.
fn range_keeps(code: u32, pos: vec2f) -> bool {
    let n = range_noise(pos);
    let t = f32(code & 0xFFu);
    return code == 0u || select((n < t), (n >= t), (code & RANGE_FADE_OUT) != 0u);
}
