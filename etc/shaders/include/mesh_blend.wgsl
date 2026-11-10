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
    let albedo = blend_alpha(blend_albedo, blend_mask, blend_heights, material_sampler, blending,
        f.uv, cutout_scalars(f).w, f.object, f.object_normal);
    if (f.alpha * albedo < cutout_scalars(f).x) {
        discard;
    }
}

@fragment fn fs_count_cutout_blend(f: CutoutOut) -> @location(0) vec4f {
    let albedo = blend_alpha(blend_albedo, blend_mask, blend_heights, material_sampler, blending,
        f.uv, cutout_scalars(f).w, f.object, f.object_normal);
    if (f.alpha * albedo < cutout_scalars(f).x) {
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
};

@diagnostic(off, derivative_uniformity)
fn blend_fragment(f: VertexOut) -> BlendFragment {
    var o: BlendFragment;
    o.uv = f.uv.xy;
    if (vo_scalars(f).w != 0.0) {
        o.uv = mapping_uv(f.object, f.object_normal, f.uv.xy);
    }
    o.gx = dpdx(o.uv);
    o.gy = dpdy(o.uv);
    o.px = dpdx(f.world);
    o.py = dpdy(f.world);
    o.w = blend_height_weights(blend_heights, material_sampler, blending,
        blend_weights(textureSampleGrad(blend_mask, material_sampler, o.uv, o.gx, o.gy),
        blending), o.uv, o.gx, o.gy);
    return o;
}

fn blend_fragment_albedo(b: BlendFragment) -> vec4f {
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
    var m = vec3f(0.0);
    for (var k = 0u; k < 4u; k++) {
        if (b.w[k] <= 0.0) {
            continue;
        }
        let t = blending.transforms[k];
        let at = b.uv * t.xy + t.zw;
        let lx = b.gx * t.xy;
        let ly = b.gy * t.xy;
        o.orm += b.w[k] * textureSampleGrad(blend_orm, material_sampler, at, k, lx, ly).rgb;
        m += b.w[k] * (textureSampleGrad(blend_normal, material_sampler, at, k, lx, ly).xyz *
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
