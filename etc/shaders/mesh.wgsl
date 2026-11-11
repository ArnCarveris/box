// Forward pass of the meshes without a triplanar TextureMapping, Sheen, ClearCoat,
// SpecularAnisotropy, DiffuseTransmission or Transmission, which mesh_extended.wgsl draws
// (box_draws_extended). It shades the metallic roughness base of the material: a GGX
// specular and a Lambert diffuse lobe.
#include "include/mesh_common.wgsl"
#include "include/mesh_indirect.wgsl"
#include "include/mesh_direct.wgsl"
#include "include/mesh_surface.wgsl"
#include "include/mesh_blend.wgsl"

// The texture layers apply on front faces; a back face keeps the material constants, is
// unoccluded and faces the camera. The occlusion scales the indirect light only. geometric is
// the vertex normal before the normal map, negated on back faces, which the shadow lookup is
// offset along.
struct Surface {
    albedo: vec3f,
    alpha: f32,
    metallic: f32,
    roughness: f32,
    occlusion: f32,
    emissive: vec3f,
    normal: vec3f,
    geometric: vec3f,
};

fn surface(f: VertexOut, front: bool) -> Surface {
    let geometric = normalize(f.normal);
    return surface_of(f, front, geometric, hard_layers(f, geometric));
}

fn surface_of(f: VertexOut, front: bool, geometric: vec3f, layers: Layers) -> Surface {
    var s: Surface;
    s.albedo = f.color.rgb;
    s.alpha = f.color.a;
    s.metallic = clamp(f.uv.z, 0.0, 1.0);
    s.roughness = clamp(f.uv.w, MIN_ROUGHNESS, 1.0);
    s.occlusion = 1.0;
    s.emissive = vo_emissive(f);
    s.normal = -geometric;
    s.geometric = -geometric;
    if (front) {
        s.albedo *= layers.albedo.rgb;
        s.alpha *= layers.albedo.a;
        s.metallic = clamp(f.uv.z * layers.orm.b, 0.0, 1.0);
        s.roughness = clamp(f.uv.w * layers.orm.g, MIN_ROUGHNESS, 1.0);
        s.occlusion = layers.orm.r;
        s.emissive *= layers.emissive;
        s.normal = layers.normal;
        s.geometric = geometric;
    } else if (kind.alpha_cutout != 0u) {
        s.alpha *= layers.albedo.a;
    }

    return s;
}

// Light reflected toward the eye by one light arriving from direction l with a radiance.
fn light_lobes(s: Surface, v: vec3f, l: vec3f, radiance: vec3f, side: u32) -> Lit {
    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let h = normalize(l + v);
    let ndl = max(dot(n, l), 0.0);
    let ndh = max(dot(n, h), 0.0);
    let ldh = max(dot(l, h), 0.0);
    let f = fresnel_schlick(ldh, f0);
    let specular = distribution_ggx(ndh, a) * visibility_smith(ndv, ndl, a) * f;
    let diffuse = (vec3f(1.0) - f) * diffuse_color / PI;
    return Lit(diffuse * radiance * ndl, specular * radiance * ndl);
}

// An unlit view shows the albedo as the diffuse part and the emission as the specular one. The
// split sum scale and bias already integrate the Fresnel over the GGX lobe of the roughness, so
// the grazing reflection of a rough surface fades without a roughness aware f90. What the lobe
// reflects is not available to the diffuse term, as 1 - F in light_lobes().
fn shade(s: Surface, v: vec3f, world: vec3f) -> Lit {
    if (view.lights.z == 0u) {
        return Lit(s.albedo, s.emissive);
    }

    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let ab = env_brdf(s.roughness, ndv);
    let spec = mix(reflect(-v, n), n, a);
    let cluster = fragment_cluster(world);
    let e = indirect_light(world, n, spec, s.roughness, cluster);
    let specular_albedo = f0 * ab.x + ab.y;
    let indirect_specular = specular_albedo * e.specular *
        specular_occlusion(ndv, s.occlusion, a);
    let indirect_diffuse = (vec3f(1.0) - specular_albedo) * diffuse_color * e.diffuse *
        s.occlusion;
    let direct = lights_direct(s, v, world, cluster, 1u, 0.0);
    return Lit(direct.diffuse + indirect_diffuse, direct.specular + indirect_specular +
        s.emissive);
}

fn lit(f: VertexOut, shaded: Surface) -> vec4f {
    planet_fragment(f);
    var s = shaded;
    if (kind.blend_mode == 0u) {
        s.occlusion = min(s.occlusion, view_occlusion(f.pos));
    }

    let v = normalize(view.camera.xyz - f.world);
    let reflected = shade(s, v, f.world);
    var color = reflected.diffuse + reflected.specular;
    if (scene_aerial && view.aerial.w > 0.0) {
        color = aerial_perspective(color, s.alpha, f.pos, f.world);
    }

    return output(color, s.alpha);
}

@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    return lit(f, surface(f, front));
}

@fragment fn fs_cutout(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let s = surface(f, front);
    if (s.alpha < vo_scalars(f).x) {
        discard;
    }

    return lit(f, s);
}

@fragment fn fs_blend(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let geometric = normalize(f.normal);
    return lit(f, surface_of(f, front, geometric, blend_layers(f, geometric, front)));
}

@fragment fn fs_blend_cutout(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let geometric = normalize(f.normal);
    let b = blend_fragment(f);
    let albedo = blend_fragment_albedo(b);
    if (f.color.a * albedo.a < vo_scalars(f).x) {
        discard;
    }

    return lit(f, surface_of(f, front, geometric, blend_maps(geometric, b, albedo, front)));
}
