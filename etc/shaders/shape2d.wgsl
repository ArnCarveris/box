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
// of the page (distance_map, a linear layer), so the edge stays sharp at any scale. shape.z of a
// glyph (f32 bits) is the part of the field its threshold falls by, 0 for a regular glyph, so a
// bold glyph grows its outline; an italic glyph leans through the shear of its transform.
// Instances are read by the slot the cull pass wrote, from the transform and shading streams of
// the instance page of the batch (group 2).
//
// The shadow of a Rect or Circle with a BoxShadow is a rectangle instance with a negative border
// width: shape.y is the negated standard deviation of its edge and shape.x the corner radius of
// the shadow box, the quad minus a margin of 3 standard deviations on each side, both in units of
// half the smaller side of the quad. Its coverage is the Gaussian falloff of the distance to the
// shadow box (an erf of the distance, the approximation of a blurred rounded box with one sample),
// softened by the footprint of the pixel so a shadow without blur keeps an anti-aliased edge.
//
// As in CSS a shadow is not drawn under its element, so it does not show through a translucent
// one. uv.xy is the center of the rounded box of the element relative to the shadow, uv.zw its half
// extents, shape.z its corner radius (f32 bits), all in the units of the shadow, and shape.w (f32
// bits) the alpha the element covers what is under it with at its edge, 0 when its blend mode does
// not cover (Additive and the like). Where the element covers a part c of the pixel with an alpha
// a, the shadow is weighted by (1 - c) / (1 - a c): the element drawn over it then leaves the
// shadow exactly on the part of the pixel outside the element, so no light or dark seam shows
// along its edge, and an opaque element looks as without the cutout. The distance to the element
// is taken in uniform control flow, a rounded box per fragment of every instance, for the
// derivatives that anti-alias it the same way the element is. Wide, faint shadows band on 8-bit
// sRGB targets, so the alpha of a shadow is dithered by about a step of the target with
// interleaved gradient noise of the pixel position, stable from frame to frame (shadow_dither).
// The shadows are drawn by pipelines of their own with the fs_shadow entry point (a batch of
// box_shadow instances), so fs takes none of these steps and a shadow pays for no texture
// samples.
//
// A Rect with an ImageSlice is drawn by pipelines of their own with the fs_slice entry point:
// uv.x of its shading is the index of its slice record (box_render_slice_t), which takes two
// consecutive records of the clip buffer, so it needs no binding of its own. The record cuts the
// element into nine parts at fractions of its size and holds the texture coordinates at those
// edges: a fragment maps its position on the element through its column and row to the texture,
// a corner scaled, an edge or the center stretched or repeated at its period. The texture is
// sampled with the gradients of the mapping of its part, so a seam between parts or tiles does not
// pick a coarse mip. Everything else is fs: the border, the corner radius, the tint and the clip.
// A plain Image pays nothing for it.
//
// A Polygon2D is drawn by pipelines of their own (a batch with polygon set): the polygon constant
// turns the vertex stage to it and fs_polygon shades it. emissive.w of its shading is the index of
// its polygon record in the clip buffer (box_render_polygon_t), which needs no binding of its own:
// a header with the center and half extents of the bounds of its outline, its edge count and half
// the width of a stroke (0 for a filled polygon), then its edges, three to a record, each the start
// and the end of a segment in the local units of the polygon. The vertex stage places the quad on
// the bounds (the instance scale is that of a line, bounds centered on the origin), so local is
// the position in the local units of the polygon. The fragment takes the signed distance to the
// nearest edge, negative where a ray crosses the edges an odd number of times (the even-odd rule,
// so holes need no winding of their own), or the distance to the polyline minus half the width
// of a stroke, and anti-aliases it over the footprint of the pixel along its gradient, which
// comes from the nearest edge; a stroke thinner than a pixel is widened to one and faded, as a
// line is. shape.y is the border width in local units and shape.zw its color, as for a rectangle.
// The cost per fragment is linear in the edges, a few operations each, which suits outlines of up
// to a few hundred edges; the edges are read three per record so each fetch serves three.
//
// The shadow of a polygon (fs_shadow_polygon) is a shadow quad of the plain vertex stage: the
// bounds of the outline moved by the offset and grown by the spread and 3 standard deviations.
// shape.x (f32) is the index of the polygon record, shape.y the negated standard deviation in
// units of half the smaller side of the quad, shape.z (f32 bits) the spread and shape.w the alpha
// the polygon covers it with; uv.xy the center of the bounds of the polygon relative to the
// shadow and uv.z half the smaller side of the quad in local units of the polygon. Its coverage is
// the Gaussian falloff of the distance to the outline moved by the offset, minus the spread, cut
// out by the coverage of the polygon itself as the shadow of a rectangle is.
//
// ids.x of the shading is the clip index of the instance, 0 when no ClipChildren ancestor clips
// it. A clip maps the world position of a fragment into the local plane of its
// element, where the coverage of its rounded area multiplies the alpha, anti-aliased over the
// footprint of the fragment there; the clips it is nested in follow through its parent index.
//
// ids.y is the rank of the instance in the 2D draw order of its tree (order.c). The elements of
// a UI lie in one plane and draw in order over equal depths, but quads of different sizes
// interpolate their depths from different vertices: in a plane rotated under perspective the
// depths of two of them round differently at the same pixel, and the one drawn later can fail
// the test. The fragment lifts its depth by its rank times a part of the depth slope (more than
// the subpixel snapping of the vertices moves the depth) and, in perspective, a part of the
// depth (more than the rounding of the vertex transform), so it wins over every coplanar element
// of its tree drawn before it without moving its pixels. A camera facing plane in an ortho view
// has neither, so screen space UI keeps the depths of its geometry.
#include "include/lights.wgsl"

// BlendMode of the pipeline: the output is shaped for the blend equation it is drawn with.
override blend_mode: u32 = 0u;

// The instances are polygons (Polygon2D): the vertex stage places the quad on the bounds of the
// polygon record of each instance.
override polygon: bool = false;

@group(0) @binding(0) var<uniform> view: View;

// Depth slope in pixels the depth of an instance is lifted by per rank
const rank_slope: f32 = 1.0 / 64.0;

// Part of the depth of an instance it is lifted by per rank in a perspective view
const rank_depth: f32 = 1.0 / 131072.0;

// The area of a ClipChildren element (box_render_clip_t): x and y are the first two rows of its
// inverse world transform, area its half extents, corner radius and the index of its parent clip.
struct Clip {
    x: vec4f,
    y: vec4f,
    area: vec4f,
};

// Most nested clips a fragment intersects, BOX_RENDER_CLIP_DEPTH
const clip_depth: u32 = 16u;

@group(0) @binding(11) var<storage, read> clips: array<Clip>;

@group(1) @binding(0) var albedo_map: texture_2d<f32>;
@group(1) @binding(2) var distance_map: texture_2d<f32>;
@group(1) @binding(4) var material_sampler: sampler;

// The transform stream of the instance page (InstanceTransform of instance.wgsl): the rows of
// the model matrix.
struct Shape2DTransform {
    rows: array<vec4f, 3>,
};

// The shading stream of the 2D instance page (box_render_shading_t, see instance.wgsl) with the
// material words read as integers. color is the linear color and alpha as unorm16x4: r | g << 16,
// b | a << 16.
struct Shape2DShading {
    emissive: vec4f,
    shape: vec4u,
    uv: vec4f,
    color: vec2u,
    ids: vec2u,
};

@group(2) @binding(0) var<storage, read> transforms: array<Shape2DTransform>;
@group(2) @binding(2) var<storage, read> shadings: array<Shape2DShading>;

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
    // Border color of a rectangle; x the field offset of a glyph, its bold weight
    @location(4) @interpolate(flat) border: vec4f,
    @location(5) @interpolate(flat) color: vec4f,
    @location(6) @interpolate(flat) emissive: vec3f,
    @location(7) @interpolate(flat) uv_transform: vec4f,
    // 1 for a glyph, 0 for a shape
    @location(8) @interpolate(flat) glyph: f32,
    // World position, which the clips map into the plane of their element
    @location(9) world: vec3f,
    // Clip index, 0 for none
    @location(10) @interpolate(flat) clip: u32,
    // Rank in the draw order of the tree of the instance
    @location(11) @interpolate(flat) rank: u32,
    // Index of the polygon record of a polygon, 0 for any other shape
    @location(12) @interpolate(flat) record: u32,
};

struct FragmentOut {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};

// Pixels the image moves per model unit along a direction, at a projected point.
fn pixels_per_unit(mvp: mat4x4f, clip: vec4f, dir: vec2f) -> f32 {
    let d = mvp * vec4f(dir, 0.0, 0.0);
    let w = max(clip.w, 1e-6);
    let ndc = (d.xy * w - clip.xy * d.w) / (w * w);
    return select(0.0, length(ndc * 0.5 * view.viewport.xy), clip.w > 1e-6);
}

@vertex fn vs(v: VertexIn) -> VertexOut {
    let inst = shadings[v.slot];
    let r = transforms[v.slot].rows;
    let model = mat4x4f(vec4f(r[0].x, r[1].x, r[2].x, 0.0), vec4f(r[0].y, r[1].y, r[2].y, 0.0),
        vec4f(r[0].z, r[1].z, r[2].z, 0.0), vec4f(r[0].w, r[1].w, r[2].w, 1.0));
    let mvp = view.view_proj * model;
    var origin = vec2f(0.0);
    var axis = vec2f(1.0, 0.0);
    var to_model = vec2f(1.0);
    var extent: vec2f;
    var bounds = vec4f(0.0);
    var o: VertexOut;
    o.params = vec2f(0.0);
    o.border = vec4f(0.0);
    o.record = 0u;
    let glyph = !polygon && inst.emissive.w == 0.0;
    if (polygon) {
        let index = u32(max(inst.emissive.w, 0.0) + 0.5);
        if (index != 0u && index < arrayLength(&clips)) {
            bounds = clips[index].x;
        }

        to_model = 1.0 / max(2.0 * (abs(bounds.xy) + bounds.zw), vec2f(1e-20));
        origin = bounds.xy * to_model;
        extent = bounds.zw;
        o.params = vec2f(0.0, bitcast<f32>(inst.shape.y));
        o.border = vec4f(unpack2x16float(inst.shape.z), unpack2x16float(inst.shape.w));
        o.record = index;
    } else if (glyph) {
        extent = vec2f(0.5);
        o.params = vec2f(bitcast<f32>(inst.shape.x), bitcast<f32>(inst.shape.y));
        o.border = vec4f(bitcast<f32>(inst.shape.z), 0.0, 0.0, 0.0);
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
        if (o.params.y < 0.0) {
            o.border = vec4f(bitcast<f32>(inst.shape.z), bitcast<f32>(inst.shape.w), 0.0, 0.0);
        }
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
    let point = vec4f(origin + dir_x * q.x + dir_y * q.y, 0.0, 1.0);
    o.pos = mvp * point;
    o.world = (model * point).xyz;
    o.clip = inst.ids.x;
    o.rank = inst.ids.y;
    o.local = q + bounds.xy;
    o.uv = vec2f(q.x / (2.0 * extent.x) + 0.5, 0.5 - q.y / (2.0 * extent.y));
    o.extent = extent;
    o.color = vec4f(unpack2x16unorm(inst.color.x), unpack2x16unorm(inst.color.y));
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

// Error function, the approximation of Abramowitz and Stegun 7.1.27 (error below 5e-4).
fn erf_approx(x: f32) -> f32 {
    let a = abs(x);
    let t = 1.0 + (0.278393 + (0.230389 + (0.000972 + 0.078108 * a) * a) * a) * a;
    let t2 = t * t;
    return sign(x) * (1.0 - 1.0 / (t2 * t2));
}

// Coverage of a shadow: the Gaussian falloff of the signed distance to the shadow box, the quad
// of the given half extents minus 3 standard deviations on each side. The standard deviation is
// widened by the footprint of the pixel, px in shape units, as by a Gaussian of 0.4 pixels (the
// slope of a one pixel ramp), so the edge of a shadow without blur is anti-aliased.
fn shadow_coverage(p: vec2f, extent: vec2f, radius: f32, sigma: f32, px: f32) -> f32 {
    let box_extent = max(extent - vec2f(3.0 * sigma), vec2f(0.0));
    let d = rounded_box(p, box_extent, radius);
    let s = max(sqrt(sigma * sigma + 0.16 * px * px), 1e-9);
    return 0.5 - 0.5 * erf_approx(d / (s * 1.41421356));
}

// Part of a pixel covered by the inside of an edge at a distance, the pixel being px wide.
fn coverage(d: f32, px: f32) -> f32 {
    return clamp(0.5 - d / max(px, 1e-6), 0.0, 1.0);
}

// Weight of a shadow under a pixel its element covers a part cover of with an alpha: the shadow
// left on the part outside the element once the element is drawn over it, 0 where the element
// covers the whole pixel.
fn cutout(cover: f32, alpha: f32) -> f32 {
    let rest = 1.0 - alpha * cover;
    return select(0.0, clamp((1.0 - cover) / max(rest, 1e-6), 0.0, 1.0), rest > 1e-5);
}

// Linear light one step of an 8-bit sRGB target spans at a linear value, from the slope of the
// sRGB curve (constant on its linear segment below 0.0031308).
fn srgb_step(l: f32) -> f32 {
    return 2.4 / (1.055 * 255.0) * pow(max(l, 0.0031308), 1.0 - 1.0 / 2.4);
}

// Dithered alpha of a shadow of a linear color at a pixel: interleaved gradient noise (Jimenez
// 2014) of the pixel position, the same every frame, of about one step of an 8-bit sRGB target
// from end to end. The step it changes the target by depends on what is under the shadow, which
// the shader does not know, so the amplitude is the smaller of the one for black under it (its
// brightest channel lightening it) and for white under it (its darkest channel darkening it),
// which keeps the noise within a step over any background. It is bounded by the distance of the
// alpha to 0 and 1, so it never clamps: the average stays the alpha and the faint tail of a shadow
// is not speckled out to the edge of its quad.
fn shadow_dither(alpha: f32, color: vec3f, pos: vec2f) -> f32 {
    let bright = max(max(color.r, color.g), color.b);
    let dark = 1.0 - min(min(color.r, color.g), color.b);
    let over_black = srgb_step(alpha * bright) / max(bright, 1e-6);
    let over_white = srgb_step(1.0 - alpha * dark) / max(dark, 1e-6);
    let amplitude = min(min(over_black, over_white), 2.0 * min(alpha, 1.0 - alpha));
    let n = fract(52.9829189 * fract(dot(floor(pos), vec2f(0.06711056, 0.00583715))));
    return alpha + (n - 0.5) * amplitude;
}

fn median(v: vec3f) -> f32 {
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

// Coverage of a glyph after msdfgen's screenPxRange shader: the median of the field at level 0
// (a mip would average the channels and break the median) is the distance to the outline,
// scaled from the range of the page to screen pixels with the Euclidean footprint of a pixel, so
// rotation changes nothing. Below one pixel of range the edge stays one pixel wide and the glyph
// fades with the range instead of aliasing. offset is added to the distance, 0 for a regular
// glyph and the bold weight for a bold one. A small bold glyph spreads its edge over more of the
// field than the offset leaves below the threshold, so the empty field of the quad would cover
// a little: the coverage is scaled so that it is 0 there, which leaves a regular glyph as is.
//
// The coverage is linear in screen pixels and blended in linear light, which the target then
// encodes: a pixel half covered by light text on a dark background shows about two thirds of the
// text, by dark text on a light background about a third, so small dark text loses its edges and
// looks thin and faint next to the same text in light (glyph_contrast corrects for it).
//
// uv_dx and uv_dy are the derivatives of uv, taken by the caller in uniform control flow, so the
// coverage takes none itself and runs under a branch for the glyph fragments only.
fn glyph_coverage(uv: vec2f, uv_dx: vec2f, uv_dy: vec2f, range: vec2f, offset: f32) -> f32 {
    let field = textureSampleLevel(distance_map, material_sampler, uv, 0.0);
    let footprint = vec2f(length(vec2f(uv_dx.x, uv_dy.x)), length(vec2f(uv_dx.y, uv_dy.y)));
    let texels = vec2f(1.0) / max(footprint, vec2f(1e-9));
    let px_range = 0.5 * dot(range, texels);
    let scale = max(px_range, 1.0);
    let empty = clamp((offset - 0.5) * scale + 0.5, 0.0, 0.999);
    let cover = clamp((median(field.rgb) - 0.5 + offset) * scale + 0.5, 0.0, 1.0);
    return (cover - empty) / (1.0 - empty) * clamp(px_range, 0.0, 1.0);
}

// sRGB encoding and decoding of a linear value in 0..1.
fn srgb_encode(l: f32) -> f32 {
    return select(1.055 * pow(l, 1.0 / 2.4) - 0.055, 12.92 * l, l <= 0.0031308);
}

fn srgb_decode(e: f32) -> f32 {
    return select(pow((e + 0.055) / 1.055, 2.4), e / 12.92, e <= 0.04045);
}

// Coverage of a glyph of a linear color corrected for the blend in linear light, so dark text on
// a light background is as heavy to the eye as light text on a dark one. The background is not
// known to the shader: a text of display luminance t is taken to lie on a background of 1 - t.
// Light text (t of 0.5 and up) keeps its coverage, the look it has today. Dark text gets the
// alpha that moves the display value of the background toward it by as much as the coverage
// moves the display value of the mirrored pair, light text of 1 - t on a background of t:
// 1 - enc(L + c (B - L)) is the display value it should reach, L and B the linear luminances
// of the text and of the background. The alpha is 0 and 1 where the coverage is, so the inside of
// a glyph and the empty field stay as they are, and it goes to the coverage as the text nears
// mid gray. The same correction holds in views with effects, whose tone mapping and encoding
// shape the display values the same way.
fn glyph_contrast(cover: f32, color: vec3f) -> f32 {
    let lum = clamp(dot(color, vec3f(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    let t = srgb_encode(lum);
    let b = srgb_decode(1.0 - t);
    let reach = srgb_decode(1.0 - srgb_encode(lum + cover * (b - lum)));
    let alpha = clamp((b - reach) / max(b - lum, 1e-6), 0.0, 1.0);
    return select(alpha, cover, t >= 0.499);
}

// Coverage of a fragment by the clips of its instance, the product over the chain of nested
// clips. The distance to each rounded area is scaled to pixels by the footprint of the fragment
// in the plane of the clip along the gradient of the distance, so the clip edge is anti-aliased
// like the edge of a shape; an empty area covers nothing. The derivatives come in from uniform
// control flow.
fn clip_coverage(first: u32, world: vec3f, world_dx: vec3f, world_dy: vec3f) -> f32 {
    var cover = 1.0;
    var index = first;
    let count = arrayLength(&clips);
    for (var i = 0u; i < clip_depth && index != 0u && index < count; i++) {
        let c = clips[index];
        let p = vec2f(dot(c.x, vec4f(world, 1.0)), dot(c.y, vec4f(world, 1.0)));
        let dx = vec2f(dot(c.x.xyz, world_dx), dot(c.y.xyz, world_dx));
        let dy = vec2f(dot(c.x.xyz, world_dy), dot(c.y.xyz, world_dy));
        let r = clamp(c.area.z, 0.0, min(c.area.x, c.area.y));
        let q = abs(p) - c.area.xy + vec2f(r);
        let d = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
        let corner = (q.x > 0.0) & (q.y > 0.0);
        let side = select(vec2f(0.0, 1.0), vec2f(1.0, 0.0), q.x > q.y);
        let toward = select(side, normalize(max(q, vec2f(1e-20))), corner);
        let g = toward * select(vec2f(1.0), vec2f(-1.0), p < vec2f(0.0));
        let px = length(vec2f(dot(g, dx), dot(g, dy)));
        cover *= select(coverage(d, px), 0.0, min(c.area.x, c.area.y) <= 0.0);
        index = u32(max(c.area.w, 0.0) + 0.5);
    }

    return cover;
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

// Depth of a fragment lifted over the coplanar elements of its tree drawn before it.
fn ranked_depth(depth: f32, rank: u32) -> f32 {
    let slope = max(abs(dpdx(depth)), abs(dpdy(depth)));
    let part = select(rank_depth, 0.0, view.lights.w == 1u);
    return depth + f32(rank) * (rank_slope * slope + part * depth);
}

@fragment fn fs(f: VertexOut) -> FragmentOut {
    let depth = ranked_depth(f.pos.z, f.rank);
    let world_dx = dpdx(f.world);
    let world_dy = dpdy(f.world);
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
    let uv_dx = dpdx(uv);
    let uv_dy = dpdy(uv);
    let texel = textureSample(albedo_map, material_sampler, uv);
    let glyph = f.glyph > 0.5;
    let shape_cover = coverage(d, px);
    let fill = select(1.0, coverage(inner, inner_px), f.params.y > 0.0);
    let shape_color = mix(f.border.rgb, f.color.rgb * texel.rgb, fill);
    let color = select(shape_color, f.color.rgb, glyph) + f.emissive;
    // The glyph coverage and its contrast correction take no derivatives and run only for
    // glyphs; the other shapes discard them.
    var glyph_cover = 0.0;
    if (glyph) {
        glyph_cover = glyph_coverage(uv, uv_dx, uv_dy, f.params, f.border.x);
        if (blend_mode <= 2u) {
            glyph_cover = glyph_contrast(glyph_cover, color);
        }
    }
    let shape_alpha = shape_cover * thin * f.color.a * mix(f.border.a, texel.a, fill);
    var alpha = select(shape_alpha, glyph_cover * f.color.a, glyph);
    var cover = select(shape_cover, glyph_cover, glyph);
    if (f.clip != 0u) {
        let clipped = clip_coverage(f.clip, f.world, world_dx, world_dy);
        alpha *= clipped;
        cover *= clipped;
    }

    if (alpha <= 0.001) {
        discard;
    }

    return FragmentOut(output(color, alpha, cover), depth);
}

// Distance of a point to one edge of a polygon, squared, with the vector from the nearest point of
// the edge to it, and whether a ray from it toward +x crosses the edge (1) or not (0): the edge is
// its start in e.xy and its end in e.zw.
fn polygon_edge(p: vec2f, e: vec4f) -> vec4f {
    let ab = e.zw - e.xy;
    let w = p - e.xy;
    let len = dot(ab, ab);
    let t = select(0.0, clamp(dot(w, ab) / len, 0.0, 1.0), len > 0.0);
    let v = w - ab * t;
    let crosses = (e.y > p.y) != (e.w > p.y) && p.x < e.x + (p.y - e.y) * ab.x / ab.y;
    return vec4f(dot(v, v), v, select(0.0, 1.0, crosses));
}

// Distance of a point to the polygon of a polygon record: x the signed distance to the outline of
// a filled polygon (negative inside by the even-odd rule) or the distance to the polyline of a
// stroke, yz the unit direction it grows in, w half the width of a stroke, 0 for a filled one.
// The loop is not uniform, so it takes no derivatives; its callers take them before it.
fn polygon_distance(index: u32, p: vec2f) -> vec4f {
    let count = arrayLength(&clips);
    var head = vec4f(0.0);
    if (index != 0u && index < count) {
        head = clips[index].y;
    }

    let edges = u32(max(head.x, 0.0));
    var best = 1e30;
    var dir = vec2f(0.0, 1.0);
    var crossings = 0.0;
    for (var i = 0u; i < edges; i += 3u) {
        let r = index + 1u + i / 3u;
        if (r >= count) {
            break;
        }

        let c = clips[r];
        var e = polygon_edge(p, c.x);
        crossings += e.w;
        if (e.x < best) {
            best = e.x;
            dir = e.yz;
        }

        if (i + 1u < edges) {
            e = polygon_edge(p, c.y);
            crossings += e.w;
            if (e.x < best) {
                best = e.x;
                dir = e.yz;
            }
        }

        if (i + 2u < edges) {
            e = polygon_edge(p, c.area);
            crossings += e.w;
            if (e.x < best) {
                best = e.x;
                dir = e.yz;
            }
        }
    }

    let d = sqrt(best);
    let n = select(vec2f(0.0, 1.0), dir / d, d > 1e-20);
    let inside = head.y <= 0.0 && fract(crossings * 0.5) > 0.25;
    return select(vec4f(d, n, head.y), vec4f(-d, -n, 0.0), inside);
}

// Polygon2D shapes: the signed distance to the edges of the polygon record anti-aliased over the
// footprint of the fragment along its gradient, with the border, the image over the bounds, the
// tint and the clip of fs.
@fragment fn fs_polygon(f: VertexOut) -> FragmentOut {
    let depth = ranked_depth(f.pos.z, f.rank);
    let world_dx = dpdx(f.world);
    let world_dy = dpdy(f.world);
    let local_dx = dpdx(f.local);
    let local_dy = dpdy(f.local);
    let uv = clamp(f.uv, vec2f(0.0), vec2f(1.0)) * f.uv_transform.xy + f.uv_transform.zw;
    let texel = textureSample(albedo_map, material_sampler, uv);
    let s = polygon_distance(f.record, f.local);
    let px = length(vec2f(dot(s.yz, local_dx), dot(s.yz, local_dy)));
    var d = s.x;
    var thin = 1.0;
    if (s.w > 0.0) {
        let stroke = max(s.w, px * 0.5);
        d = s.x + s.w - stroke;
        thin = s.w / stroke;
    }

    let shape_cover = coverage(d, px);
    let fill = select(1.0, coverage(d + f.params.y, px), f.params.y > 0.0);
    let color = mix(f.border.rgb, f.color.rgb * texel.rgb, fill) + f.emissive;
    var alpha = shape_cover * thin * f.color.a * mix(f.border.a, texel.a, fill);
    var cover = shape_cover;
    if (f.clip != 0u) {
        let clipped = clip_coverage(f.clip, f.world, world_dx, world_dy);
        alpha *= clipped;
        cover *= clipped;
    }

    if (alpha <= 0.001) {
        discard;
    }

    return FragmentOut(output(color, alpha, cover), depth);
}

// The shadows of Polygon2D shapes: the Gaussian falloff of the distance to the outline moved by
// the offset and grown by the spread, cut out by the polygon.
@fragment fn fs_shadow_polygon(f: VertexOut) -> FragmentOut {
    let depth = ranked_depth(f.pos.z, f.rank);
    let world_dx = dpdx(f.world);
    let world_dy = dpdy(f.world);
    let local_dx = dpdx(f.local);
    let local_dy = dpdy(f.local);
    let footprint = vec2f(
        length(vec2f(local_dx.x, local_dy.x)),
        length(vec2f(local_dx.y, local_dy.y)));
    let half = f.uv_transform.z;
    let index = u32(max(f.params.x, 0.0) + 0.5);
    var center = vec2f(0.0);
    if (index != 0u && index < arrayLength(&clips)) {
        center = clips[index].x.xy;
    }

    let p = (f.local - f.uv_transform.xy) * half + center;
    let shift = -f.uv_transform.xy * half;
    let shape = polygon_distance(index, p);
    let moved = polygon_distance(index, p - shift);
    let sigma = -f.params.y * half;
    let px = max(footprint.x, footprint.y) * half;
    let sd = max(sqrt(sigma * sigma + 0.16 * px * px), 1e-9);
    var cover = 0.5 - 0.5 * erf_approx((moved.x - f.border.x) / (sd * 1.41421356));
    let cut_px = length(vec2f(dot(shape.yz, local_dx), dot(shape.yz, local_dy))) * half;
    cover *= cutout(coverage(shape.x, cut_px), f.border.y);
    var alpha = shadow_dither(cover * f.color.a, f.color.rgb, f.pos.xy);
    if (f.clip != 0u) {
        let clipped = clip_coverage(f.clip, f.world, world_dx, world_dy);
        alpha *= clipped;
        cover *= clipped;
    }

    if (alpha <= 0.001) {
        discard;
    }

    return FragmentOut(output(f.color.rgb, alpha, cover), depth);
}

// Texture coordinate of a position along one axis of a sliced image and its rate of change per
// unit of the position: edges are the four edges of the parts, coords the coordinates at them,
// period the length the middle part repeats at, 0 when it stretches, half half a texel. The
// coordinate stays half a texel inside its part, so the filter does not blend in the texels of
// the next part at a seam or of the far end of a tile where it repeats.
fn slice_axis(p: f32, edges: vec4f, coords: vec4f, period: f32, half: f32) -> vec2f {
    var c = vec2f(coords.y, coords.z);
    var r: vec2f;
    if (p < edges.y) {
        let k = (coords.y - coords.x) / max(edges.y - edges.x, 1e-9);
        c = vec2f(coords.x, coords.y);
        r = vec2f(coords.x + (p - edges.x) * k, k);
    } else if (p > edges.z) {
        let k = (coords.w - coords.z) / max(edges.w - edges.z, 1e-9);
        c = vec2f(coords.z, coords.w);
        r = vec2f(coords.z + (p - edges.z) * k, k);
    } else if (period > 0.0) {
        let t = fract((p - edges.y) / period);
        r = vec2f(coords.y + (coords.z - coords.y) * t, (coords.z - coords.y) / period);
    } else {
        let k = (coords.z - coords.y) / max(edges.z - edges.y, 1e-9);
        r = vec2f(coords.y + (p - edges.y) * k, k);
    }

    let lo = min(c.x, c.y);
    let hi = max(c.x, c.y);
    let inset = min(half, (hi - lo) * 0.5);
    return vec2f(clamp(r.x, lo + inset, hi - inset), r.y);
}

// The 2D shapes with an ImageSlice: fs with the texel of the part of the nine slice image under
// the fragment, transparent in the center of a hollow one.
@fragment fn fs_slice(f: VertexOut) -> FragmentOut {
    let depth = ranked_depth(f.pos.z, f.rank);
    let world_dx = dpdx(f.world);
    let world_dy = dpdy(f.world);
    let uv_dx = dpdx(f.uv);
    let uv_dy = dpdy(f.uv);
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

    let index = u32(max(f.uv_transform.x, 0.0) + 0.5);
    let count = arrayLength(&clips);
    var texel = vec4f(0.0);
    if (index != 0u && index + 1u < count) {
        let a = clips[index];
        let b = clips[index + 1u];
        let p = clamp(f.uv, vec2f(0.0), vec2f(1.0));
        let mid_x = (p.x >= a.x.y) & (p.x <= a.x.z);
        let mid_y = (p.y >= a.y.y) & (p.y <= a.y.z);
        let half = 0.5 / vec2f(textureDimensions(albedo_map));
        let su = slice_axis(p.x, a.x, a.area, select(b.y.x, b.y.z, mid_y), half.x);
        let sv = slice_axis(p.y, a.y, b.x, select(b.y.y, b.y.w, mid_x), half.y);
        let k = vec2f(su.y, sv.y);
        texel = textureSampleGrad(albedo_map, material_sampler, vec2f(su.x, sv.x), uv_dx * k,
            uv_dy * k);
        texel = select(texel, vec4f(0.0), mid_x & mid_y & (b.area.x > 0.5));
    }

    let shape_cover = coverage(d, px);
    let fill = select(1.0, coverage(inner, inner_px), f.params.y > 0.0);
    let color = mix(f.border.rgb, f.color.rgb * texel.rgb, fill) + f.emissive;
    var alpha = shape_cover * thin * f.color.a * mix(f.border.a, texel.a, fill);
    var cover = shape_cover;
    if (f.clip != 0u) {
        let clipped = clip_coverage(f.clip, f.world, world_dx, world_dy);
        alpha *= clipped;
        cover *= clipped;
    }

    if (alpha <= 0.001) {
        discard;
    }

    return FragmentOut(output(color, alpha, cover), depth);
}

// The shadows of the 2D shapes, drawn by pipelines of their own: no texture samples and none of
// the shape and glyph coverage of fs, only the blurred shadow box cut out by its element.
@fragment fn fs_shadow(f: VertexOut) -> FragmentOut {
    let depth = ranked_depth(f.pos.z, f.rank);
    let world_dx = dpdx(f.world);
    let world_dy = dpdy(f.world);
    let footprint = vec2f(
        length(vec2f(dpdx(f.local.x), dpdy(f.local.x))),
        length(vec2f(dpdx(f.local.y), dpdy(f.local.y))));
    let cut = rounded_box(f.local - f.uv_transform.xy, max(f.uv_transform.zw, footprint * 0.5),
        f.border.x);
    let cut_px = length(vec2f(dpdx(cut), dpdy(cut)));
    var cover = shadow_coverage(f.local, f.extent, f.params.x, -f.params.y,
        max(footprint.x, footprint.y));
    cover *= cutout(coverage(cut, cut_px), f.border.y);
    var alpha = shadow_dither(cover * f.color.a, f.color.rgb, f.pos.xy);
    if (f.clip != 0u) {
        let clipped = clip_coverage(f.clip, f.world, world_dx, world_dy);
        alpha *= clipped;
        cover *= clipped;
    }

    if (alpha <= 0.001) {
        discard;
    }

    return FragmentOut(output(f.color.rgb, alpha, cover), depth);
}
