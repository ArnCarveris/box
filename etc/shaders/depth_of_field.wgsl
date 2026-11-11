// Depth of field: a gather bokeh at half resolution from the depth of the view.
//
// fs_prefilter halves the image with a Karis weighted average of each 2x2 block, so a bright
// pixel flickers no more than its neighbors as it moves, and stores the signed circle of
// confusion (CoC) of the block in alpha: the nearest one when any pixel is in front of the focus,
// else the sharpest far one, so a sharp pixel never spreads into the far field. fs_dilate_x and
// fs_dilate_y take the max of the near CoC of the pixels that can reach a pixel, the radius the
// near field gathers over, so a blurred foreground spreads over the sharp pixels behind it.
// fs_gather_far and fs_gather_near gather a disc of 48 golden angle taps; each tap only counts
// as far as its own CoC reaches, and a far tap no farther than the CoC of the center, so a sharp
// background never bleeds into a blurred foreground and the reverse. fs_post is a 3x3 median
// against undersampling. fs_composite mixes the sharp image with the far field by the CoC of each
// full resolution pixel, upsampled from the texels that are blurred themselves, and fs_near draws
// the premultiplied near field over it.
//
// circle_of_confusion is the one function that turns a pixel into its CoC.
#include "include/effect.wgsl"

struct Dof {
    // z = (x + y * d) / (z * d + w), the view depth of a reversed Z depth d
    depth: vec4f,
    // focus distance, focus range, near transition, far transition
    focus: vec4f,
    // x: largest radius in half resolution pixels, y: 1 when the near field blurs, z: CoC of a
    // pixel without geometry
    params: vec4f,
};

@group(0) @binding(2) var<uniform> dof: Dof;
@group(0) @binding(3) var depth_texture: texture_depth_2d;
@group(0) @binding(4) var aux_texture: texture_2d<f32>;

const TAPS: i32 = 48;
const GOLDEN_ANGLE: f32 = 2.39996323;

// Distance from the camera along its forward axis of a reversed Z depth above 0.
fn view_depth(d: f32) -> f32 {
    return (dof.depth.x + dof.depth.y * d) / max(dof.depth.z * d + dof.depth.w, 1e-20);
}

// Signed CoC of a view depth: -1 in front of the focus at the largest blur, 0 sharp, +1 behind.
fn depth_coc(z: f32) -> f32 {
    let half_range = dof.focus.y * 0.5;
    let near_edge = dof.focus.x - half_range;
    let far_edge = dof.focus.x + half_range;
    if (z < near_edge) {
        if (dof.params.y < 0.5) {
            return 0.0;
        }

        let t = select(1.0, (near_edge - z) / max(dof.focus.z, 1e-20), dof.focus.z > 0.0);
        return -min(t, 1.0);
    }

    if (z > far_edge) {
        let t = select(1.0, (z - far_edge) / max(dof.focus.w, 1e-20), dof.focus.w > 0.0);
        return min(t, 1.0);
    }

    return 0.0;
}

// Signed CoC of the pixel at uv with depth d; depth 0, a pixel without geometry, is far.
fn circle_of_confusion(uv: vec2f, d: f32) -> f32 {
    if (d <= 0.0) {
        return dof.params.z;
    }

    return depth_coc(view_depth(d));
}

@fragment fn fs_prefilter(v: FullscreenOut) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(input_texture));
    let full = vec2f(size);
    let base = vec2i(v.pos.xy) * 2;
    var color = vec3f(0.0);
    var weight = 0.0;
    var nearest = 0.0;
    var sharpest = 1.0;
    for (var i = 0; i < 4; i++) {
        let p = min(base + vec2i(i & 1, i >> 1u), size - 1);
        let c = max(textureLoad(input_texture, p, 0).rgb, vec3f(0.0));
        let coc = circle_of_confusion((vec2f(p) + 0.5) / full, textureLoad(depth_texture, p, 0));
        let w = 1.0 / (1.0 + luminance(c));
        color += c * w;
        weight += w;
        nearest = min(nearest, coc);
        sharpest = min(sharpest, max(coc, 0.0));
    }

    return vec4f(color / weight, select(sharpest, nearest, nearest < 0.0));
}

// Largest near CoC along one axis of the pixels whose blur reaches this pixel.
fn dilate(p: vec2i, axis: vec2i, from_alpha: bool) -> vec4f {
    let size = vec2i(textureDimensions(input_texture));
    let radius = dof.params.x;
    let r = i32(ceil(radius));
    var result = 0.0;
    for (var i = -r; i <= r; i++) {
        let q = clamp(p + axis * i, vec2i(0), size - 1);
        let s = textureLoad(input_texture, q, 0);
        let n = select(s.r, max(-s.a, 0.0), from_alpha);
        if (n * radius + 1.0 >= f32(abs(i))) {
            result = max(result, n);
        }
    }

    return vec4f(result, 0.0, 0.0, 1.0);
}

@fragment fn fs_dilate_x(v: FullscreenOut) -> @location(0) vec4f {
    return dilate(vec2i(v.pos.xy), vec2i(1, 0), true);
}

@fragment fn fs_dilate_y(v: FullscreenOut) -> @location(0) vec4f {
    return dilate(vec2i(v.pos.xy), vec2i(0, 1), false);
}

// Offset of tap i of a disc of radius 1, golden angle spiral of uniform density.
fn tap(i: i32) -> vec2f {
    let a = f32(i) * GOLDEN_ANGLE;
    return vec2f(cos(a), sin(a)) * sqrt((f32(i) + 0.5) / f32(TAPS));
}

// Texel of the half image under a tap: the nearest one, so the CoC of a tap is exact.
fn tap_texel(p: vec2f, o: vec2f) -> vec4f {
    let size = vec2i(textureDimensions(input_texture));
    return textureLoad(input_texture, clamp(vec2i(floor(p + o)), vec2i(0), size - 1), 0);
}

// The far field: rgb the blurred color, alpha the far CoC of the pixel.
@fragment fn fs_gather_far(v: FullscreenOut) -> @location(0) vec4f {
    let center = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    let c0 = max(center.a, 0.0);
    let radius = c0 * dof.params.x;
    if (radius < 0.5) {
        return vec4f(center.rgb, c0);
    }

    var sum = center.rgb;
    var weight = 1.0;
    for (var i = 0; i < TAPS; i++) {
        let o = tap(i) * radius;
        let s = tap_texel(v.pos.xy, o);
        let c = min(max(s.a, 0.0), c0);
        let w = select(0.0, saturate(c * dof.params.x - length(o) + 0.5), c > 0.0);
        sum += s.rgb * w;
        weight += w;
    }

    return vec4f(sum / weight, c0);
}

// The near field, premultiplied by its coverage.
@fragment fn fs_gather_near(v: FullscreenOut) -> @location(0) vec4f {
    let p = vec2i(v.pos.xy);
    let radius = textureLoad(aux_texture, p, 0).r * dof.params.x;
    if (radius < 0.5) {
        return vec4f(0.0);
    }

    let own = max(-textureLoad(input_texture, p, 0).a, 0.0) * dof.params.x;
    var sum = vec3f(0.0);
    var weight = 0.0;
    for (var i = 0; i < TAPS; i++) {
        let o = tap(i) * radius;
        let s = tap_texel(v.pos.xy, o);
        let n = max(-s.a, 0.0);
        let w = select(0.0, saturate(n * dof.params.x - length(o) + 0.5), n > 0.0);
        sum += s.rgb * w;
        weight += w;
    }

    let alpha = saturate(max(2.0 * weight / f32(TAPS), own - 0.5));
    return vec4f(sum / max(weight, 1e-4) * alpha, alpha);
}

// Put the smaller of two entries first.
fn sort_pair(m: ptr<function, array<vec4f, 9>>, a: i32, b: i32) {
    let lo = min((*m)[a], (*m)[b]);
    let hi = max((*m)[a], (*m)[b]);
    (*m)[a] = lo;
    (*m)[b] = hi;
}

// Median of each channel of the 3x3 pixels around a pixel.
@fragment fn fs_post(v: FullscreenOut) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(input_texture));
    let p = vec2i(v.pos.xy);
    var m: array<vec4f, 9>;
    for (var i = 0; i < 9; i++) {
        let q = clamp(p + vec2i(i % 3 - 1, i / 3 - 1), vec2i(0), size - 1);
        m[i] = textureLoad(input_texture, q, 0);
    }

    sort_pair(&m, 1, 2); sort_pair(&m, 4, 5); sort_pair(&m, 7, 8);
    sort_pair(&m, 0, 1); sort_pair(&m, 3, 4); sort_pair(&m, 6, 7);
    sort_pair(&m, 1, 2); sort_pair(&m, 4, 5); sort_pair(&m, 7, 8);
    sort_pair(&m, 0, 3); sort_pair(&m, 5, 8); sort_pair(&m, 4, 7);
    sort_pair(&m, 3, 6); sort_pair(&m, 1, 4); sort_pair(&m, 2, 5);
    sort_pair(&m, 4, 7); sort_pair(&m, 4, 2); sort_pair(&m, 6, 4);
    sort_pair(&m, 4, 2);
    return m[4];
}

// The sharp image mixed with the far field by the CoC of the pixel. The far field is upsampled
// from the 2x2 texels around the pixel whose own blur is at least about a pixel, so a sharp
// half resolution texel at the focus border does not spread over the blurred pixels next to it.
@fragment fn fs_composite(v: FullscreenOut) -> @location(0) vec4f {
    let p = vec2i(v.pos.xy);
    let src = textureLoad(input_texture, p, 0);
    let coc = circle_of_confusion(v.uv, textureLoad(depth_texture, p, 0));
    let w = saturate(coc * dof.params.x * 2.0 - 0.5);
    if (w <= 0.0) {
        return src;
    }

    let size = vec2i(textureDimensions(aux_texture));
    let pos = v.uv * vec2f(size) - 0.5;
    let base = floor(pos);
    let f = pos - base;
    var sum = vec3f(0.0);
    var weight = 0.0;
    for (var i = 0; i < 4; i++) {
        let o = vec2i(i & 1, i >> 1u);
        let s = textureLoad(aux_texture, clamp(vec2i(base) + o, vec2i(0), size - 1), 0);
        let bx = select(1.0 - f.x, f.x, o.x == 1);
        let by = select(1.0 - f.y, f.y, o.y == 1);
        let tw = bx * by * saturate(s.a * dof.params.x * 2.0);
        sum += s.rgb * tw;
        weight += tw;
    }

    let blurred = select(src.rgb, sum / max(weight, 1e-6), weight > 1e-4);
    return vec4f(mix(src.rgb, blurred, w), src.a);
}

@fragment fn fs_near(v: FullscreenOut) -> @location(0) vec4f {
    return textureSampleLevel(input_texture, input_sampler, v.uv, 0.0);
}
