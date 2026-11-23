#pragma once

#include "blend.wgsl"

// Group 1 of fs_blend and fs_depth_cutout_blend besides emissive_map and material_sampler: the
// albedo (decoded from sRGB), ORM and normal of each layer as a slice of an array, the mask as
// stored, the uniforms (BlendUniforms of blend.wgsl) and the height of each layer in the red of
// a slice, as stored (a one slice placeholder with a contrast of 0 or without height images).
@group(1) @binding(5) var blend_albedo: texture_2d_array<f32>;
@group(1) @binding(6) var blend_orm: texture_2d_array<f32>;
@group(1) @binding(7) var blend_normal: texture_2d_array<f32>;
@group(1) @binding(8) var blend_mask: texture_2d<f32>;
@group(1) @binding(9) var<uniform> blending: BlendUniforms;
@group(1) @binding(10) var blend_heights: texture_2d_array<f32>;

// Depth prepass of a lit view (box_gpu_draw_prepass_draws). fs_depth_cutout_blend discards what
// fs_blend_cutout discards; the forward pass after the prepass does not alpha test, its Equal
// depth test rejects those.
@fragment fn fs_depth_cutout_blend(f: CutoutOut) {
    range_discard(f.range, f.pos);
    let albedo = blend_alpha(blend_albedo, blend_mask, blend_heights, material_sampler, blending,
        f.uv, cutout_scalars(f).w, f.object, f.object_normal);
    if (cut_out_depth(f, f.alpha * albedo)) {
        discard;
    }
}

@fragment fn fs_count_cutout_blend(f: CutoutOut) -> @location(0) vec4f {
    range_discard(f.range, f.pos);
    let albedo = blend_alpha(blend_albedo, blend_mask, blend_heights, material_sampler, blending,
        f.uv, cutout_scalars(f).w, f.object, f.object_normal);
    if (cut_out_depth(f, f.alpha * albedo)) {
        discard;
    }

    return vec4f(0.0, 1.0, 0.0, 0.0);
}

struct BlendFragment {
    uv: vec2f,
    gx: vec2f,
    gy: vec2f,
    px: vec3f,
    py: vec3f,
    w: vec4f,
    tiled: u32,
    slice: vec4u,
    cell: vec4u,
    f: vec2f,
    tx: vec2f,
    ty: vec2f,
    albedo: vec4f,
};

fn tile_kind(p: vec2i, size: vec2i) -> u32 {
    let k = u32(round(textureLoad(blend_mask, clamp(p, vec2i(0), size - vec2i(1)), 0).r * 255.0));
    return select(0u, min(k, blending.count - 1u), (blending.flags & BLEND_MASKED) != 0u);
}

fn tile_hash(c: vec2i) -> u32 {
    var h = (bitcast<u32>(c.x) * 0x8da6b343u) ^ (bitcast<u32>(c.y) * 0xd8163841u);
    h = (h ^ (h >> 15u)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12u)) * 0x297a2d39u;
    return h ^ (h >> 15u);
}

fn tile_sort(k: vec4u) -> vec4u {
    let a = vec4u(min(k.x, k.y), max(k.x, k.y), min(k.z, k.w), max(k.z, k.w));
    let b = vec4u(min(a.x, a.z), min(a.y, a.w), max(a.x, a.z), max(a.y, a.w));
    return vec4u(b.x, min(b.y, b.z), max(b.y, b.z), b.w);
}

fn tile_variant(k: vec4u, q: u32, h: u32, extended: bool) -> u32 {
    let m = select(0u, 1u, k.x >= q) | select(0u, 2u, k.y >= q) | select(0u, 4u, k.z >= q) |
        select(0u, 8u, k.w >= q);
    return select(m, 16u + ((h >> 1u) & 15u), m == 15u && extended && (h & 1u) != 0u);
}

fn tile_at(b: BlendFragment, v: u32, cols: f32) -> vec2f {
    let cell = vec2f(f32((v % 4u) + 4u * (v / 16u)), f32((v / 4u) % 4u));
    return (cell + b.f) / vec2f(cols, 4.0);
}

fn tile_cols() -> f32 {
    let dims = vec2f(textureDimensions(blend_albedo));
    return select(4.0, 8.0, dims.x >= dims.y * 1.5);
}

fn blend_tiles(o: ptr<function, BlendFragment>) {
    let size = vec2i(textureDimensions(blend_mask));
    let p = (*o).uv * vec2f(size) - 0.5;
    let c = vec2i(floor(p));
    let k = vec4u(tile_kind(c, size), tile_kind(c + vec2i(1, 0), size),
        tile_kind(c + vec2i(0, 1), size), tile_kind(c + vec2i(1, 1), size));
    let cols = tile_cols();
    let cell = f32(textureDimensions(blend_albedo).y) * 0.25;
    let gx = (*o).gx * vec2f(size);
    let gy = (*o).gy * vec2f(size);
    let reach = max(max(length(gx), length(gy)) * cell, 1e-6);
    let lod = clamp(log2(reach), 0.0, max(log2(cell) - 1.0, 0.0));
    let squeeze = min(exp2(lod) / reach, 1.0);
    let edge = min(0.5 * reach * squeeze / cell, 0.25);
    (*o).f = clamp(p - floor(p), vec2f(edge), vec2f(1.0 - edge));
    (*o).tx = gx * squeeze / vec2f(cols, 4.0);
    (*o).ty = gy * squeeze / vec2f(cols, 4.0);
    let s = tile_sort(k);
    let h = tile_hash(c);
    var n = 0u;
    var color = vec3f(0.0);
    var w = vec4f(0.0);
    for (var j = 0u; j < 4u; j++) {
        if (j > 0u && s[j] == s[j - 1u]) {
            continue;
        }
        let v = tile_variant(k, s[j], h, cols > 4.0);
        (*o).slice[n] = s[j];
        (*o).cell[n] = v;
        let a = textureSampleGrad(blend_albedo, material_sampler, tile_at(*o, v, cols), s[j],
            (*o).tx, (*o).ty);
        let alpha = select(a.a, 1.0, n == 0u);
        color = mix(color, a.rgb, alpha);
        w = w * (1.0 - alpha);
        w[n] = alpha;
        n++;
    }
    (*o).w = w;
    (*o).albedo = vec4f(color, 1.0);
}

// The mask of a fragment at uv: the mask image.
fn blend_mask_at(uv: vec2f, gx: vec2f, gy: vec2f) -> vec4f {
    return textureSampleGrad(blend_mask, material_sampler, uv, gx, gy);
}

fn blend_slice(b: BlendFragment, k: u32) -> u32 {
    return select(k, b.slice[k], b.tiled != 0u);
}

fn blend_at(b: BlendFragment, k: u32, cols: f32) -> vec2f {
    let t = blending.transforms[k];
    return select(b.uv * t.xy + t.zw, tile_at(b, b.cell[k], cols), b.tiled != 0u);
}

fn blend_lx(b: BlendFragment, k: u32) -> vec2f {
    return select(b.gx * blending.transforms[k].xy, b.tx, b.tiled != 0u);
}

fn blend_ly(b: BlendFragment, k: u32) -> vec2f {
    return select(b.gy * blending.transforms[k].xy, b.ty, b.tiled != 0u);
}

@diagnostic(off, derivative_uniformity)
fn blend_fragment(f: VertexOut) -> BlendFragment {
    var o: BlendFragment;
    o.uv = f.uv.xy;
    if (vo_scalars(f).w != 0.0) {
        o.uv = mapping_uv(f.object, f.object_normal.xyz, f.uv.xy);
    }
    o.gx = dpdx(o.uv);
    o.gy = dpdy(o.uv);
    o.px = dpdx(f.world);
    o.py = dpdy(f.world);
    o.tiled = blending.flags & BLEND_TILES;
    if (o.tiled != 0u) {
        blend_tiles(&o);
    } else {
        o.w = blend_height_weights(blend_heights, material_sampler, blending,
            blend_weights(blend_mask_at(o.uv, o.gx, o.gy), blending), o.uv, o.gx, o.gy);
    }
    return o;
}

fn blend_fragment_albedo(b: BlendFragment) -> vec4f {
    if (b.tiled != 0u) {
        return b.albedo;
    }
    var albedo = vec4f(0.0);
    for (var k = 0u; k < 4u; k++) {
        if (b.w[k] <= 0.0) {
            continue;
        }
        let t = blending.transforms[k];
        albedo += b.w[k] * textureSampleGrad(blend_albedo, material_sampler, b.uv * t.xy + t.zw,
            k, b.gx * t.xy, b.gy * t.xy);
    }
    return albedo;
}

fn blend_maps(geometric: vec3f, b: BlendFragment, albedo: vec4f, front: bool) -> Layers {
    var o: Layers;
    o.albedo = albedo;
    o.emissive = vec3f(0.0);
    o.orm = vec3f(0.0);
    o.frame = cotangent_frame_grad(geometric, b.px, b.py, b.gx, b.gy);
    o.normal = geometric;
    if (!front) {
        return o;
    }
    o.emissive = textureSampleGrad(emissive_map, material_sampler, b.uv, b.gx, b.gy).rgb;
    let cols = tile_cols();
    var m = vec3f(0.0);
    for (var k = 0u; k < 4u; k++) {
        if (b.w[k] <= 0.0) {
            continue;
        }
        let at = blend_at(b, k, cols);
        let s = blend_slice(b, k);
        let lx = blend_lx(b, k);
        let ly = blend_ly(b, k);
        o.orm += b.w[k] * textureSampleGrad(blend_orm, material_sampler, at, s, lx, ly).rgb;
        m += b.w[k] * (textureSampleGrad(blend_normal, material_sampler, at, s, lx, ly).xyz *
            2.0 - 1.0);
    }
    o.normal = perturb(geometric, o.frame,
        select(vec3f(0.0, 0.0, 1.0), normalize(m), dot(m, m) > 1e-12));
    return o;
}

fn blend_layers(f: VertexOut, geometric: vec3f, front: bool) -> Layers {
    let b = blend_fragment(f);
    var albedo = vec4f(0.0);
    if (front || kind.alpha_cutout != 0u) {
        albedo = blend_fragment_albedo(b);
    }
    return blend_maps(geometric, b, albedo, front);
}
