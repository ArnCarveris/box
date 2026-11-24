// The texture layers of a fragment of the mesh shaders without a triplanar blend or a
// TextureBlend: its UVs or its planar projection (TextureMapping with a blend of 0) and the
// tangent frame of its normal map. The includer includes mesh_common.wgsl first.
#pragma once

// Tangent frame from the screen derivatives of the position and the UVs (cotangent frame), its
// tangent and bitangent scaled by the same factor, the longer of the two of unit length. The
// framebuffer y of dpdy points down, which negates both derivative axes of the frame. degenerate
// is set where the UV derivatives give no frame. It reads dpdx and dpdy, so it is called where
// every fragment of a quad takes the same path (hard_layers).
struct Frame {
    tangent: vec3f,
    bitangent: vec3f,
    degenerate: bool,
};

@diagnostic(off, derivative_uniformity)
fn cotangent_frame(n: vec3f, p: vec3f, uv: vec2f) -> Frame {
    return cotangent_frame_grad(n, dpdx(p), dpdy(p), dpdx(uv), dpdy(uv));
}

fn cotangent_frame_grad(n: vec3f, dp1: vec3f, dp2: vec3f, duv1: vec2f, duv2: vec2f) -> Frame {
    let dp2perp = cross(dp2, n);
    let dp1perp = cross(n, dp1);
    let t = dp2perp * duv1.x + dp1perp * duv2.x;
    let b = dp2perp * duv1.y + dp1perp * duv2.y;
    let len2 = max(dot(t, t), dot(b, b));
    let scale = inverseSqrt(max(len2, 1e-20));
    return Frame(-t * scale, b * scale, len2 < 1e-20);
}

// A tangent space normal whose y points up the image, toward smaller v, in the cotangent frame
// of the normal n.
fn perturb(n: vec3f, frame: Frame, m: vec3f) -> vec3f {
    let tangent = mat3x3f(frame.tangent, frame.bitangent, n);
    return select(normalize(tangent * m), n, frame.degenerate);
}

struct Layers {
    albedo: vec4f,
    emissive: vec3f,
    orm: vec3f,
    normal: vec3f,
    frame: Frame,
};

// The emissive and ORM layers at uv with explicit gradients, white without a map
// (material_emissive_map, material_orm_map).
fn emissive_grad(uv: vec2f, gx: vec2f, gy: vec2f) -> vec3f {
    if (!material_emissive_map) {
        return vec3f(1.0);
    }
    return textureSampleGrad(emissive_map, material_sampler, uv, gx, gy).rgb;
}

fn orm_grad(uv: vec2f, gx: vec2f, gy: vec2f) -> vec3f {
    if (!material_orm_map) {
        return vec3f(1.0);
    }
    return textureSampleGrad(orm_map, material_sampler, uv, gx, gy).rgb;
}

// The layers of a fragment without a triplanar blend. Only called when vo_scalars(f).w (flat) is
// at most 1, the same for every fragment of a quad, so the implicit derivatives hold.
@diagnostic(off, derivative_uniformity)
fn hard_layers(f: VertexOut, geometric: vec3f) -> Layers {
    var uv = f.uv.xy;
    if (vo_scalars(f).w != 0.0) {
        uv = mapping_uv(f.object, f.object_normal.xyz, f.uv.xy);
    }
    var o: Layers;
    o.albedo = textureSample(albedo_map, material_sampler, uv);
    o.emissive = vec3f(1.0);
    if (material_emissive_map) {
        o.emissive = textureSample(emissive_map, material_sampler, uv).rgb;
    }
    o.orm = vec3f(1.0);
    if (material_orm_map) {
        o.orm = textureSample(orm_map, material_sampler, uv).rgb;
    }
    let m = textureSample(normal_map, material_sampler, normal_uv(f, uv)).xyz * 2.0 - 1.0;
    o.frame = cotangent_frame(geometric, f.world, uv);
    o.normal = perturb(geometric, o.frame, m);
    return o;
}
