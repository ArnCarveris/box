// 2D shapes: instanced unit quads shaded with the analytic signed distance of a rounded
// rectangle (Rect, Circle) or a line segment, anti-aliased over the screen footprint of each
// fragment so edges stay crisp at any scale, distance and angle. The vertex stage grows the
// quad by a pixel so the fringe outside the edge is drawn too. A side thinner than a pixel is
// widened to one and faded by as much, so thin lines keep an even brightness instead of
// breaking into dashes. Unlit: the base color times the albedo layer plus the emission, with
// the border mixed over it.
//
// Instance words of a rectangle: shape.x is the corner radius and shape.y the border width, in
// units of half its smaller side, shape.zw the linear border color as four half floats, and
// emissive.w its width over its height. A line: shape.xy is the start, shape.zw the end and
// emissive.w the negated width; its quad is scaled to bounds centered on the origin that hold
// both ends, twice the largest coordinate plus the width on each axis. A glyph of a Text has an
// emissive.w of 0: its quad is the glyph area, uv the area of the atlas page and shape.xy the
// distance range of the page; its coverage comes from the multi channel signed distance field
// of the page (distance_map, a linear layer), so the edge stays sharp at any scale. Instances
// are read by the slot the cull pass wrote, from the instance buffer of the batch (group 2).
#include "include/lights.wgsl"

// BlendMode of the pipeline: the output is shaped for the blend equation it is drawn with.
override blend_mode: u32 = 0u;

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(0) var albedo_map: texture_2d<f32>;
@group(1) @binding(2) var distance_map: texture_2d<f32>;
@group(1) @binding(4) var material_sampler: sampler;

// The instance layout of the forward pass with the material words read as integers.
struct Shape2DInstance {
    model: mat4x4f,
    color: vec4f,
    emissive: vec4f,
    shape: vec4u,
    uv: vec4f,
};

@group(2) @binding(0) var<storage, read> instances: array<Shape2DInstance>;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) slot: u32,
};

struct VertexOut {
    @builtin(position) pos: vec4f,
    // Position in shape units, the shape centered on the origin
    @location(0) local: vec2f,
    // Texture coordinate across the shape before the UV transform, 0..1 inside the shape
    @location(1) uv: vec2f,
    // Half extents of the shape in shape units
    @location(2) @interpolate(flat) extent: vec2f,
    // Corner radius and border width in shape units
    @location(3) @interpolate(flat) params: vec2f,
    @location(4) @interpolate(flat) border: vec4f,
    @location(5) @interpolate(flat) color: vec4f,
    @location(6) @interpolate(flat) emissive: vec3f,
    @location(7) @interpolate(flat) uv_transform: vec4f,
    // 1 for a glyph, 0 for a shape
    @location(8) @interpolate(flat) glyph: f32,
};

// Pixels the image moves per model unit along a direction, at a projected point.
fn pixels_per_unit(mvp: mat4x4f, clip: vec4f, dir: vec2f) -> f32 {
    let d = mvp * vec4f(dir, 0.0, 0.0);
    let w = max(clip.w, 1e-6);
    let ndc = (d.xy * w - clip.xy * d.w) / (w * w);
    return select(0.0, length(ndc * 0.5 * view.viewport.xy), clip.w > 1e-6);
}

@vertex fn vs(v: VertexIn) -> VertexOut {
    let inst = instances[v.slot];
    let model = mat4x4f(vec4f(inst.model[0].xyz, 0.0), vec4f(inst.model[1].xyz, 0.0),
        vec4f(inst.model[2].xyz, 0.0), vec4f(inst.model[3].xyz, 1.0));
    let mvp = view.view_proj * model;
    var origin = vec2f(0.0);
    var axis = vec2f(1.0, 0.0);
    var to_model = vec2f(1.0);
    var extent: vec2f;
    var o: VertexOut;
    o.params = vec2f(0.0);
    o.border = vec4f(0.0);
    let glyph = inst.emissive.w == 0.0;
    if (glyph) {
        extent = vec2f(0.5);
        o.params = vec2f(bitcast<f32>(inst.shape.x), bitcast<f32>(inst.shape.y));
    } else if (inst.emissive.w < 0.0) {
        let a = bitcast<vec2f>(inst.shape.xy);
        let b = bitcast<vec2f>(inst.shape.zw);
        let len = length(b - a);
        axis = select(vec2f(1.0, 0.0), (b - a) / len, len > 0.0);
        to_model = 1.0 / (2.0 * max(abs(a), abs(b)) + vec2f(-inst.emissive.w));
        origin = (a + b) * 0.5 * to_model;
        extent = vec2f(len, -inst.emissive.w) * 0.5;
    } else {
        let aspect = max(inst.emissive.w, 1e-6);
        extent = select(vec2f(1.0, 1.0 / aspect), vec2f(aspect, 1.0), aspect >= 1.0);
        to_model = 0.5 / extent;
        o.params = vec2f(bitcast<f32>(inst.shape.x), bitcast<f32>(inst.shape.y));
        o.border = vec4f(unpack2x16float(inst.shape.z), unpack2x16float(inst.shape.w));
    }

    let dir_x = axis * to_model;
    let dir_y = vec2f(-axis.y, axis.x) * to_model;
    let corner = sign(v.position.xy);
    let edge = corner * extent;
    let clip = mvp * vec4f(origin + dir_x * edge.x + dir_y * edge.y, 0.0, 1.0);
    let ppu = vec2f(pixels_per_unit(mvp, clip, dir_x), pixels_per_unit(mvp, clip, dir_y));
    let limit = 2.0 * max(extent.x, extent.y);
    let grow = min(1.0 / max(ppu, vec2f(1e-9)), vec2f(limit));
    let pad = select(vec2f(0.0), grow, (ppu > vec2f(0.0)) & vec2<bool>(!glyph));
    let q = edge + corner * pad;
    o.pos = mvp * vec4f(origin + dir_x * q.x + dir_y * q.y, 0.0, 1.0);
    o.local = q;
    o.uv = vec2f(q.x / (2.0 * extent.x) + 0.5, 0.5 - q.y / (2.0 * extent.y));
    o.extent = extent;
    o.color = inst.color;
    o.emissive = inst.emissive.rgb;
    o.uv_transform = inst.uv;
    o.glyph = select(0.0, 1.0, glyph);
    return o;
}

// Signed distance to a rectangle with rounded corners centered on the origin, negative inside.
fn rounded_box(p: vec2f, extent: vec2f, radius: f32) -> f32 {
    let r = clamp(radius, 0.0, min(extent.x, extent.y));
    let q = abs(p) - extent + vec2f(r);
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// Part of a pixel covered by the inside of an edge at a distance, the pixel being px wide.
fn coverage(d: f32, px: f32) -> f32 {
    return clamp(0.5 - d / max(px, 1e-6), 0.0, 1.0);
}

fn median(v: vec3f) -> f32 {
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

// Coverage of a glyph after msdfgen's screenPxRange shader: the median of the field at level 0
// (a mip would average the channels and break the median) is the distance to the outline,
// scaled from the range of the page to screen pixels with the Euclidean footprint of a pixel, so
// rotation changes nothing. Below one pixel of range the edge stays one pixel wide and the glyph
// fades with the range instead of aliasing.
fn glyph_coverage(uv: vec2f, range: vec2f) -> f32 {
    let field = textureSampleLevel(distance_map, material_sampler, uv, 0.0);
    let footprint = vec2f(length(vec2f(dpdx(uv.x), dpdy(uv.x))),
        length(vec2f(dpdx(uv.y), dpdy(uv.y))));
    let texels = vec2f(1.0) / max(footprint, vec2f(1e-9));
    let px_range = 0.5 * dot(range, texels);
    let cover = clamp((median(field.rgb) - 0.5) * max(px_range, 1.0) + 0.5, 0.0, 1.0);
    return cover * clamp(px_range, 0.0, 1.0);
}

fn output(color: vec3f, alpha: f32, cover: f32) -> vec4f {
    switch blend_mode {
        case 2u: {
            return vec4f(color * cover, alpha);
        }
        case 4u, 7u: {
            return vec4f(mix(vec3f(1.0), color, alpha), alpha);
        }
        case 5u, 8u: {
            return vec4f(color * alpha, alpha);
        }
        default: {
            return vec4f(color, alpha);
        }
    }
}

@fragment fn fs(f: VertexOut) -> @location(0) vec4f {
    let footprint = vec2f(
        length(vec2f(dpdx(f.local.x), dpdy(f.local.x))),
        length(vec2f(dpdx(f.local.y), dpdy(f.local.y))));
    let extent = max(f.extent, footprint * 0.5);
    let thin = f.extent.x * f.extent.y / max(extent.x * extent.y, 1e-20);
    let d = rounded_box(f.local, extent, f.params.x);
    let inner_extent = max(extent - vec2f(f.params.y), vec2f(0.0));
    let inner = rounded_box(f.local, inner_extent, f.params.x - f.params.y);
    let px = length(vec2f(dpdx(d), dpdy(d)));
    let inner_px = length(vec2f(dpdx(inner), dpdy(inner)));
    let uv = clamp(f.uv, vec2f(0.0), vec2f(1.0)) * f.uv_transform.xy + f.uv_transform.zw;
    let texel = textureSample(albedo_map, material_sampler, uv);
    let glyph = f.glyph > 0.5;
    let glyph_cover = glyph_coverage(uv, f.params);
    let shape_cover = coverage(d, px);
    let fill = select(1.0, coverage(inner, inner_px), f.params.y > 0.0);
    let shape_color = mix(f.border.rgb, f.color.rgb * texel.rgb, fill);
    let color = select(shape_color, f.color.rgb, glyph) + f.emissive;
    let shape_alpha = shape_cover * thin * f.color.a * mix(f.border.a, texel.a, fill);
    let alpha = select(shape_alpha, glyph_cover * f.color.a, glyph);
    let cover = select(shape_cover, glyph_cover, glyph);
    if (alpha <= 0.001) {
        discard;
    }

    return output(color, alpha, cover);
}
