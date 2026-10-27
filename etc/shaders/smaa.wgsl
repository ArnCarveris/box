// SMAA 1x, a port of the reference SMAA shader (Jorge Jimenez et al., MIT, see
// src/render/smaa/LICENSE) with color edge detection, diagonal and corner detection:
// `edges` finds the edges of the image (binding 0) by the contrast of its colors, clamped to
// 0..1 and encoded to sRGB, into an RG8 image cleared to 0; `weights` searches along those
// edges (binding 3) for their ends and looks up the coverage of the pattern they form in the
// area texture (binding 4), with the search texture (binding 5) to find the ends; `blend` mixes
// every pixel with its neighbors by the weights (binding 6), in linear light, all four channels.
#include "include/effect.wgsl"

struct Smaa {
    // 1 / width, 1 / height, width, height of the render size
    metrics: vec4f,
    // x: threshold, y: search steps, z: diagonal search steps, 0 off, w: corner rounding 0..1
    params: vec4f,
};

@group(0) @binding(2) var<uniform> smaa: Smaa;
@group(0) @binding(3) var edges_texture: texture_2d<f32>;
@group(0) @binding(4) var area_texture: texture_2d<f32>;
@group(0) @binding(5) var search_texture: texture_2d<f32>;
@group(0) @binding(6) var weights_texture: texture_2d<f32>;

// Factor of the local contrast adaptation of the edge detection
const LOCAL_CONTRAST: f32 = 2.0;
// Longest distance the area texture covers, orthogonal and diagonal
const AREA_MAX_DISTANCE: f32 = 16.0;
const AREA_MAX_DISTANCE_DIAG: f32 = 20.0;
// Size of a texel of the area texture and the height of one of its subsample blocks
const AREA_PIXEL: vec2f = vec2f(1.0 / 160.0, 1.0 / 560.0);
const AREA_SUBTEX: f32 = 1.0 / 7.0;
// Size of the search texture, unpacked and as stored
const SEARCH_SIZE: vec2f = vec2f(66.0, 33.0);
const SEARCH_PACKED_SIZE: vec2f = vec2f(64.0, 16.0);

struct EdgesOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) offset0: vec4f,
    @location(2) offset1: vec4f,
    @location(3) offset2: vec4f,
};

struct WeightsOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) pixcoord: vec2f,
    @location(2) offset0: vec4f,
    @location(3) offset1: vec4f,
    @location(4) offset2: vec4f,
};

struct BlendOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) offset: vec4f,
};

// Round halves away from zero, as HLSL does.
fn round_away2(x: vec2f) -> vec2f {
    return sign(x) * floor(abs(x) + 0.5);
}

fn round_away4(x: vec4f) -> vec4f {
    return sign(x) * floor(abs(x) + 0.5);
}

@vertex fn edges_vs(@builtin(vertex_index) i: u32) -> EdgesOut {
    let f = fullscreen(i);
    let m = smaa.metrics;
    var o: EdgesOut;
    o.pos = f.pos;
    o.uv = f.uv;
    o.offset0 = m.xyxy * vec4f(-1.0, 0.0, 0.0, -1.0) + f.uv.xyxy;
    o.offset1 = m.xyxy * vec4f(1.0, 0.0, 0.0, 1.0) + f.uv.xyxy;
    o.offset2 = m.xyxy * vec4f(-2.0, 0.0, 0.0, -2.0) + f.uv.xyxy;
    return o;
}

@vertex fn weights_vs(@builtin(vertex_index) i: u32) -> WeightsOut {
    let f = fullscreen(i);
    let m = smaa.metrics;
    var o: WeightsOut;
    o.pos = f.pos;
    o.uv = f.uv;
    o.pixcoord = f.uv * m.zw;
    o.offset0 = m.xyxy * vec4f(-0.25, -0.125, 1.25, -0.125) + f.uv.xyxy;
    o.offset1 = m.xyxy * vec4f(-0.125, -0.25, -0.125, 1.25) + f.uv.xyxy;
    o.offset2 = m.xxyy * vec4f(-2.0, 2.0, -2.0, 2.0) * smaa.params.y +
        vec4f(o.offset0.xz, o.offset1.yw);
    return o;
}

@vertex fn blend_vs(@builtin(vertex_index) i: u32) -> BlendOut {
    let f = fullscreen(i);
    var o: BlendOut;
    o.pos = f.pos;
    o.uv = f.uv;
    o.offset = smaa.metrics.xyxy * vec4f(1.0, 0.0, 0.0, 1.0) + f.uv.xyxy;
    return o;
}

// Point read of the image at uv, clamped to its edges, clamped to 0..1 and encoded to sRGB.
fn color_point(uv: vec2f) -> vec3f {
    let size = vec2i(smaa.metrics.zw);
    let p = clamp(vec2i(floor(uv * smaa.metrics.zw)), vec2i(0), size - 1);
    let c = clamp(textureLoad(input_texture, p, 0).rgb, vec3f(0.0), vec3f(1.0));
    let high = 1.055 * pow(c, vec3f(1.0 / 2.4)) - 0.055;
    return select(high, c * 12.92, c <= vec3f(0.0031308));
}

// Largest channel difference of two colors.
fn color_delta(a: vec3f, b: vec3f) -> f32 {
    let t = abs(a - b);
    return max(max(t.r, t.g), t.b);
}

// Color edge detection with local contrast adaptation: r is an edge on the left, g on the top.
@fragment fn edges(v: EdgesOut) -> @location(0) vec4f {
    let threshold = vec2f(smaa.params.x);
    let c = color_point(v.uv);
    var delta = vec4f(0.0);
    delta.x = color_delta(c, color_point(v.offset0.xy));
    delta.y = color_delta(c, color_point(v.offset0.zw));
    var e = step(threshold, delta.xy);
    if (dot(e, vec2f(1.0)) == 0.0) {
        discard;
    }

    delta.z = color_delta(c, color_point(v.offset1.xy));
    delta.w = color_delta(c, color_point(v.offset1.zw));
    var max_delta = max(delta.xy, delta.zw);
    delta.z = color_delta(c, color_point(v.offset2.xy));
    delta.w = color_delta(c, color_point(v.offset2.zw));
    max_delta = max(max_delta, delta.zw);
    let final_delta = max(max_delta.x, max_delta.y);
    e *= step(vec2f(final_delta), LOCAL_CONTRAST * delta.xy);
    return vec4f(e, 0.0, 0.0);
}

// Edges at uv, filtered.
fn edges_at(uv: vec2f) -> vec2f {
    return textureSampleLevel(edges_texture, input_sampler, uv, 0.0).rg;
}

// Decode the bilinear read of two edges along a diagonal into the edges themselves.
fn decode_diag2(e: vec2f) -> vec2f {
    return round_away2(vec2f(e.x * abs(5.0 * e.x - 5.0 * 0.75), e.y));
}

fn decode_diag4(e: vec4f) -> vec4f {
    let rb = e.xz * abs(5.0 * e.xz - 5.0 * 0.75);
    return round_away4(vec4f(rb.x, e.y, rb.y, e.w));
}

// Result of a diagonal search: the distance and whether the last step found an edge in coord,
// the last edges read in e.
struct DiagSearch {
    coord: vec2f,
    e: vec2f,
};

// Search along a diagonal of edges that read cleanly from one texel.
fn search_diag1(uv: vec2f, dir: vec2f) -> DiagSearch {
    var coord = vec4f(uv, -1.0, 1.0);
    let t = vec3f(smaa.metrics.xy, 1.0);
    let end = smaa.params.z - 1.0;
    var e = vec2f(0.0);
    while (coord.z < end && coord.w > 0.9) {
        let next = t * vec3f(dir, 1.0) + coord.xyz;
        e = edges_at(next.xy);
        coord = vec4f(next, dot(e, vec2f(0.5)));
    }

    return DiagSearch(coord.zw, e);
}

// Search along a diagonal of edges read with a bilinear tap between two texels.
fn search_diag2(uv: vec2f, dir: vec2f) -> DiagSearch {
    var coord = vec4f(uv, -1.0, 1.0);
    coord.x += 0.25 * smaa.metrics.x;
    let t = vec3f(smaa.metrics.xy, 1.0);
    let end = smaa.params.z - 1.0;
    var e = vec2f(0.0);
    while (coord.z < end && coord.w > 0.9) {
        let next = t * vec3f(dir, 1.0) + coord.xyz;
        e = decode_diag2(edges_at(next.xy));
        coord = vec4f(next, dot(e, vec2f(0.5)));
    }

    return DiagSearch(coord.zw, e);
}

// Area of a diagonal pattern from its distances and its crossing edges.
fn area_diag(dist: vec2f, e: vec2f, offset: f32) -> vec2f {
    var uv = vec2f(AREA_MAX_DISTANCE_DIAG) * e + dist;
    uv = AREA_PIXEL * uv + 0.5 * AREA_PIXEL;
    uv.x += 0.5;
    uv.y += AREA_SUBTEX * offset;
    return textureSampleLevel(area_texture, input_sampler, uv, 0.0).rg;
}

// Weights of the diagonal patterns through the pixel.
fn diag_weights(uv: vec2f, e: vec2f) -> vec2f {
    let m = smaa.metrics;
    var result = vec2f(0.0);
    var d = vec4f(0.0);
    if (e.r > 0.0) {
        let s = search_diag1(uv, vec2f(-1.0, 1.0));
        d.x = s.coord.x + f32(s.e.y > 0.9);
        d.z = s.coord.y;
    }

    let s1 = search_diag1(uv, vec2f(1.0, -1.0));
    d.y = s1.coord.x;
    d.w = s1.coord.y;
    if (d.x + d.y > 2.0) {
        let coords = vec4f(-d.x + 0.25, d.x, d.y, -d.y - 0.25) * m.xyxy + uv.xyxy;
        let raw = vec4f(
            textureSampleLevel(edges_texture, input_sampler, coords.xy, 0.0, vec2i(-1, 0)).rg,
            textureSampleLevel(edges_texture, input_sampler, coords.zw, 0.0, vec2i(1, 0)).rg);
        let c = decode_diag4(raw).yxwz;
        let cc = select(vec2f(2.0) * c.xz + c.yw, vec2f(0.0), d.zw >= vec2f(0.9));
        result += area_diag(d.xy, cc, 0.0);
    }

    let s2 = search_diag2(uv, vec2f(-1.0, -1.0));
    d.x = s2.coord.x;
    d.z = s2.coord.y;
    d.y = 0.0;
    d.w = 0.0;
    if (textureSampleLevel(edges_texture, input_sampler, uv, 0.0, vec2i(1, 0)).r > 0.0) {
        let s = search_diag2(uv, vec2f(1.0, 1.0));
        d.y = s.coord.x + f32(s.e.y > 0.9);
        d.w = s.coord.y;
    }

    if (d.x + d.y > 2.0) {
        let coords = vec4f(-d.x, -d.x, d.y, d.y) * m.xyxy + uv.xyxy;
        let c = vec4f(
            textureSampleLevel(edges_texture, input_sampler, coords.xy, 0.0, vec2i(-1, 0)).g,
            textureSampleLevel(edges_texture, input_sampler, coords.xy, 0.0, vec2i(0, -1)).r,
            textureSampleLevel(edges_texture, input_sampler, coords.zw, 0.0, vec2i(1, 0)).gr);
        let cc = select(vec2f(2.0) * c.xz + c.yw, vec2f(0.0), d.zw >= vec2f(0.9));
        result += area_diag(d.xy, cc, 0.0).yx;
    }

    return result;
}

// Distance to the end of a line of edges from the two edges a bilinear tap read.
fn search_length(e: vec2f, offset: f32) -> f32 {
    var scale = SEARCH_SIZE * vec2f(0.5, -1.0);
    var bias = SEARCH_SIZE * vec2f(offset, 1.0);
    scale += vec2f(-1.0, 1.0);
    bias += vec2f(0.5, -0.5);
    scale *= 1.0 / SEARCH_PACKED_SIZE;
    bias *= 1.0 / SEARCH_PACKED_SIZE;
    return textureSampleLevel(search_texture, input_sampler, scale * e + bias, 0.0).r;
}

fn search_x_left(start: vec2f, end: f32) -> f32 {
    var uv = start;
    var e = vec2f(0.0, 1.0);
    while (uv.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = edges_at(uv);
        uv = -vec2f(2.0, 0.0) * smaa.metrics.xy + uv;
    }

    let offset = -(255.0 / 127.0) * search_length(e, 0.0) + 3.25;
    return smaa.metrics.x * offset + uv.x;
}

fn search_x_right(start: vec2f, end: f32) -> f32 {
    var uv = start;
    var e = vec2f(0.0, 1.0);
    while (uv.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = edges_at(uv);
        uv = vec2f(2.0, 0.0) * smaa.metrics.xy + uv;
    }

    let offset = -(255.0 / 127.0) * search_length(e, 0.5) + 3.25;
    return -smaa.metrics.x * offset + uv.x;
}

fn search_y_up(start: vec2f, end: f32) -> f32 {
    var uv = start;
    var e = vec2f(1.0, 0.0);
    while (uv.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = edges_at(uv);
        uv = -vec2f(0.0, 2.0) * smaa.metrics.xy + uv;
    }

    let offset = -(255.0 / 127.0) * search_length(e.gr, 0.0) + 3.25;
    return smaa.metrics.y * offset + uv.y;
}

fn search_y_down(start: vec2f, end: f32) -> f32 {
    var uv = start;
    var e = vec2f(1.0, 0.0);
    while (uv.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = edges_at(uv);
        uv = vec2f(0.0, 2.0) * smaa.metrics.xy + uv;
    }

    let offset = -(255.0 / 127.0) * search_length(e.gr, 0.5) + 3.25;
    return -smaa.metrics.y * offset + uv.y;
}

// Area of an orthogonal pattern from the square roots of its distances and its crossing edges.
fn area(dist: vec2f, e1: f32, e2: f32, offset: f32) -> vec2f {
    var uv = vec2f(AREA_MAX_DISTANCE) * round_away2(4.0 * vec2f(e1, e2)) + dist;
    uv = AREA_PIXEL * uv + 0.5 * AREA_PIXEL;
    uv.y = AREA_SUBTEX * offset + uv.y;
    return textureSampleLevel(area_texture, input_sampler, uv, 0.0).rg;
}

// Weights of a horizontal pattern with its sharp corners kept, less what the corner rounding
// rounds.
fn corner_horizontal(w: vec2f, uv: vec4f, d: vec2f) -> vec2f {
    let left_right = step(d.xy, d.yx);
    let rounding = (1.0 - smaa.params.w) * left_right / (left_right.x + left_right.y);
    var factor = vec2f(1.0);
    factor.x -= rounding.x *
        textureSampleLevel(edges_texture, input_sampler, uv.xy, 0.0, vec2i(0, 1)).r;
    factor.x -= rounding.y *
        textureSampleLevel(edges_texture, input_sampler, uv.zw, 0.0, vec2i(1, 1)).r;
    factor.y -= rounding.x *
        textureSampleLevel(edges_texture, input_sampler, uv.xy, 0.0, vec2i(0, -2)).r;
    factor.y -= rounding.y *
        textureSampleLevel(edges_texture, input_sampler, uv.zw, 0.0, vec2i(1, -2)).r;
    return w * saturate(factor);
}

fn corner_vertical(w: vec2f, uv: vec4f, d: vec2f) -> vec2f {
    let left_right = step(d.xy, d.yx);
    let rounding = (1.0 - smaa.params.w) * left_right / (left_right.x + left_right.y);
    var factor = vec2f(1.0);
    factor.x -= rounding.x *
        textureSampleLevel(edges_texture, input_sampler, uv.xy, 0.0, vec2i(1, 0)).g;
    factor.x -= rounding.y *
        textureSampleLevel(edges_texture, input_sampler, uv.zw, 0.0, vec2i(1, 1)).g;
    factor.y -= rounding.x *
        textureSampleLevel(edges_texture, input_sampler, uv.xy, 0.0, vec2i(-2, 0)).g;
    factor.y -= rounding.y *
        textureSampleLevel(edges_texture, input_sampler, uv.zw, 0.0, vec2i(-2, 1)).g;
    return w * saturate(factor);
}

// Blending weights of the edges of the pixel, SMAA 1x (subsample indices 0): rg for its top
// edge, ba for its left edge. Pixels without edges end right after reading them.
@fragment fn weights(v: WeightsOut) -> @location(0) vec4f {
    let m = smaa.metrics;
    var w = vec4f(0.0);
    var e = edges_at(v.uv);
    if (e.g > 0.0) {
        var diag = vec2f(0.0);
        if (smaa.params.z > 0.0) {
            diag = diag_weights(v.uv, e);
        }

        if (diag.x == -diag.y) {
            var coords = vec3f(0.0);
            coords.x = search_x_left(v.offset0.xy, v.offset2.x);
            coords.y = v.offset1.y;
            var d = vec2f(coords.x, 0.0);
            let e1 = edges_at(coords.xy).r;
            coords.z = search_x_right(v.offset0.zw, v.offset2.y);
            d.y = coords.z;
            d = abs(round_away2(m.zz * d - v.pixcoord.xx));
            let e2 = textureSampleLevel(edges_texture, input_sampler, coords.zy, 0.0,
                vec2i(1, 0)).r;
            let a = area(sqrt(d), e1, e2, 0.0);
            coords.y = v.uv.y;
            let rg = corner_horizontal(a, coords.xyzy, d);
            w.x = rg.x;
            w.y = rg.y;
        } else {
            w.x = diag.x;
            w.y = diag.y;
            e.r = 0.0;
        }
    }

    if (e.r > 0.0) {
        var coords = vec3f(0.0);
        coords.y = search_y_up(v.offset1.xy, v.offset2.z);
        coords.x = v.offset0.x;
        var d = vec2f(coords.y, 0.0);
        let e1 = edges_at(coords.xy).g;
        coords.z = search_y_down(v.offset1.zw, v.offset2.w);
        d.y = coords.z;
        d = abs(round_away2(m.ww * d - v.pixcoord.yy));
        let e2 = textureSampleLevel(edges_texture, input_sampler, coords.xz, 0.0,
            vec2i(0, 1)).g;
        let a = area(sqrt(d), e1, e2, 0.0);
        coords.x = v.uv.x;
        let ba = corner_vertical(a, coords.xyxz, d);
        w.z = ba.x;
        w.w = ba.y;
    }

    return w;
}

// Mix the pixel with the neighbor across its strongest edge by the weights, in linear light.
@fragment fn blend(v: BlendOut) -> @location(0) vec4f {
    let m = smaa.metrics;
    let here = textureSampleLevel(weights_texture, input_sampler, v.uv, 0.0);
    let a = vec4f(
        textureSampleLevel(weights_texture, input_sampler, v.offset.xy, 0.0).a,
        textureSampleLevel(weights_texture, input_sampler, v.offset.zw, 0.0).g,
        here.z,
        here.x);
    if (dot(a, vec4f(1.0)) < 1e-5) {
        return textureSampleLevel(input_texture, input_sampler, v.uv, 0.0);
    }

    let h = max(a.x, a.z) > max(a.y, a.w);
    let offset = select(vec4f(0.0, a.y, 0.0, a.w), vec4f(a.x, 0.0, a.z, 0.0), h);
    var weight = select(a.yw, a.xz, h);
    weight /= dot(weight, vec2f(1.0));
    let coord = offset * vec4f(m.xy, -m.xy) + v.uv.xyxy;
    var color = weight.x * textureSampleLevel(input_texture, input_sampler, coord.xy, 0.0);
    color += weight.y * textureSampleLevel(input_texture, input_sampler, coord.zw, 0.0);
    return color;
}
