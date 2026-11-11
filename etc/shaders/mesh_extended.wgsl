// Forward pass of the meshes the mesh shader does not draw (box_draws_extended): transmissive
// (Transmission), with a triplanar TextureMapping, Sheen, ClearCoat, SpecularAnisotropy,
// DiffuseTransmission or Fresnel, with or without a TextureBlend.
#include "include/mesh_common.wgsl"
#include "include/mesh_indirect.wgsl"
#include "include/mesh_direct.wgsl"
#include "include/mesh_surface.wgsl"
#include "include/mesh_blend.wgsl"

// The material features of the scene (Scene) compile out the code of those it lacks; the alpha
// test, the blend modes and the TextureBlend have no override. transmissive is set for the
// pipelines of Transmission shapes, through which the opaque snapshot of the view shows.
override material_sheen: bool = true;
override material_anisotropy: bool = true;
override material_clear_coat: bool = true;
override material_diffuse_transmission: bool = true;
override material_triplanar: bool = true;
override material_fresnel: bool = true;
override transmissive: bool = false;

// Snapshot of the view for transmission (gpu_transmission.c): its image right before a
// transmissive draw, with a blur pyramid, mip k + 1 downsampled from mip k. At most 4 snapshots
// are taken a frame; later glass samples the last one. A view without transmissive draws, and
// the captures, bind a 1x1 white image.
@group(0) @binding(19) var opaque_snapshot: texture_2d<f32>;

// Depth of the target copied with the first snapshot of the view, reversed Z with 0 for the sky.
// Blended and transmissive draws write no depth, so a blended shape in the snapshot blurs like
// whatever lies behind it. A view without transmissive draws, and the captures, bind a 1x1
// placeholder.
@group(0) @binding(20) var opaque_snapshot_depth: texture_depth_2d;

// Texture layers apply on front faces; a plain back face keeps the material constants, is
// unoccluded and faces the camera, except that an alpha tested one takes the albedo alpha, so its
// holes go through both sides. The occlusion scales the indirect light only. geometric is the
// vertex normal before the normal map, negated on back faces, which the shadow lookup is offset
// along; the clear coat lies on it. bitangent = cross(normal, tangent); anisotropy is 0 where the
// UVs give no direction. sheen is linear, sheen_roughness clamped to 0.07..1. back_color is the
// diffuse transmission color times the diffuse color times back_factor, and thickness the
// distance behind the surface of its shadow lookup.
struct Surface {
    albedo: vec3f,
    alpha: f32,
    metallic: f32,
    roughness: f32,
    occlusion: f32,
    emissive: vec3f,
    normal: vec3f,
    geometric: vec3f,
    coat: f32,
    coat_roughness: f32,
    anisotropy: f32,
    tangent: vec3f,
    bitangent: vec3f,
    sheen: vec3f,
    sheen_roughness: f32,
    back_color: vec3f,
    back_factor: f32,
    thickness: f32,
};

// Direction of v perpendicular to the normal n, of unit length; zero along n.
fn tangent_along(v: vec3f, n: vec3f) -> vec3f {
    let p = v - n * dot(n, v);
    return p * inverseSqrt(max(dot(p, p), 1e-12));
}

// The layers of a triplanar fragment. Explicit gradients from the derivatives of f.object, so a
// quad that straddles a change of the dominant axis keeps its mip level. The world normals of the
// projections blend; the frame is that of the dominant projection. Only called when
// vo_scalars(f).w (flat) is above 1, so the derivatives taken here hold.
@diagnostic(off, derivative_uniformity)
fn triplanar(f: VertexOut, geometric: vec3f) -> Layers {
    let dx = dpdx(f.object);
    let dy = dpdy(f.object);
    let n = f.object_normal;
    let w = mapping_weights(n, vo_scalars(f).w - 1.0);
    let s = mapping_signs(n);
    let dominant = mapping_axis(n);
    var planes = mapping_planes(f.object, s);
    var gx = mapping_planes(dx, s);
    var gy = mapping_planes(dy, s);
    var o: Layers;
    o.albedo = vec4f(0.0);
    o.emissive = vec3f(0.0);
    o.orm = vec3f(0.0);
    var normal = vec3f(0.0);
    for (var k = 0u; k < 3u; k++) {
        if (w[k] == 0.0) {
            continue;
        }
        let uv = planes[k] + f.uv.xy;
        o.albedo += w[k] * textureSampleGrad(albedo_map, material_sampler, uv, gx[k], gy[k]);
        o.emissive += w[k] * textureSampleGrad(emissive_map, material_sampler, uv, gx[k],
            gy[k]).rgb;
        o.orm += w[k] * textureSampleGrad(orm_map, material_sampler, uv, gx[k], gy[k]).rgb;
        let m = textureSampleGrad(normal_map, material_sampler, uv + f.normal_offset, gx[k],
            gy[k]).xyz * 2.0 - 1.0;
        let tangent = tangent_along(mapping_rotate(vo_rotation(f), mapping_tangent(k, s)),
            geometric);
        let bitangent = tangent_along(mapping_rotate(vo_rotation(f), mapping_bitangent(k)),
            geometric);
        normal += w[k] * (tangent * m.x + bitangent * m.y + geometric * m.z);
        if (k == dominant) {
            o.frame = Frame(tangent, bitangent, false);
        }
    }
    o.normal = select(geometric, normalize(normal), dot(normal, normal) > 1e-12);
    return o;
}

// The weighted albedo, ORM and tangent space normal of the layers of a TextureBlend. maps false
// samples the albedo alone (the alpha of a plain back face).
struct BlendSample {
    albedo: vec4f,
    orm: vec3f,
    normal: vec3f,
};

fn blend_sample(uv: vec2f, gx: vec2f, gy: vec2f, w: vec4f, maps: bool) -> BlendSample {
    var o: BlendSample;
    o.albedo = vec4f(0.0);
    o.orm = vec3f(0.0);
    var m = vec3f(0.0);
    for (var k = 0u; k < 4u; k++) {
        if (w[k] <= 0.0) {
            continue;
        }
        let t = blending.transforms[k];
        let at = uv * t.xy + t.zw;
        let lx = gx * t.xy;
        let ly = gy * t.xy;
        o.albedo += w[k] * textureSampleGrad(blend_albedo, material_sampler, at, k, lx, ly);
        if (!maps) {
            continue;
        }
        o.orm += w[k] * textureSampleGrad(blend_orm, material_sampler, at, k, lx, ly).rgb;
        m += w[k] * (textureSampleGrad(blend_normal, material_sampler, at, k, lx, ly).xyz * 2.0 -
            1.0);
    }
    o.normal = select(vec3f(0.0, 0.0, 1.0), normalize(m), dot(m, m) > 1e-12);
    return o;
}

// The layers of a triplanar fragment of a TextureBlend, as triplanar does for one set of textures.
// A back face samples only the albedo of an alpha tested pipeline. Only called when
// vo_scalars(f).w (flat) is above 1, so the derivatives taken here hold.
@diagnostic(off, derivative_uniformity)
fn blend_triplanar(f: VertexOut, geometric: vec3f, front: bool) -> Layers {
    let dx = dpdx(f.object);
    let dy = dpdy(f.object);
    let n = f.object_normal;
    let w = mapping_weights(n, vo_scalars(f).w - 1.0);
    let s = mapping_signs(n);
    let dominant = mapping_axis(n);
    let sampled = front || kind.alpha_cutout != 0u;
    var planes = mapping_planes(f.object, s);
    var gx = mapping_planes(dx, s);
    var gy = mapping_planes(dy, s);
    var o: Layers;
    o.albedo = vec4f(0.0);
    o.emissive = vec3f(0.0);
    o.orm = vec3f(0.0);
    var normal = vec3f(0.0);
    for (var k = 0u; k < 3u; k++) {
        if (w[k] == 0.0) {
            continue;
        }
        let tangent = tangent_along(mapping_rotate(vo_rotation(f), mapping_tangent(k, s)),
            geometric);
        let bitangent = tangent_along(mapping_rotate(vo_rotation(f), mapping_bitangent(k)),
            geometric);
        if (k == dominant) {
            o.frame = Frame(tangent, bitangent, false);
        }
        if (!sampled) {
            continue;
        }
        let uv = planes[k] + f.uv.xy;
        let lw = blend_height_weights(blend_heights, material_sampler, blending,
            blend_weights(textureSampleGrad(blend_mask, material_sampler, uv, gx[k], gy[k]),
            blending), uv, gx[k], gy[k]);
        let b = blend_sample(uv, gx[k], gy[k], lw, front);
        o.albedo += w[k] * b.albedo;
        if (!front) {
            continue;
        }
        o.emissive += w[k] * textureSampleGrad(emissive_map, material_sampler, uv, gx[k],
            gy[k]).rgb;
        o.orm += w[k] * b.orm;
        let m = b.normal;
        normal += w[k] * (tangent * m.x + bitangent * m.y + geometric * m.z);
    }
    o.normal = select(geometric, normalize(normal), dot(normal, normal) > 1e-12);
    return o;
}

fn surface(f: VertexOut, front: bool) -> Surface {
    let geometric = normalize(f.normal);
    var layers: Layers;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = triplanar(f, geometric);
    } else {
        layers = hard_layers(f, geometric);
    }
    return surface_of(f, front, geometric, layers);
}

fn surface_blend(f: VertexOut, front: bool) -> Surface {
    let geometric = normalize(f.normal);
    var layers: Layers;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        layers = blend_layers(f, geometric, front);
    }
    return surface_of(f, front, geometric, layers);
}

fn surface_of(f: VertexOut, front: bool, geometric: vec3f, layers: Layers) -> Surface {
    let mapped = layers.normal;
    let albedo = layers.albedo;
    let emissive = layers.emissive;
    let orm = layers.orm;
    let frame = layers.frame;
    var s: Surface;
    s.albedo = f.color.rgb;
    s.alpha = f.color.a;
    s.metallic = clamp(f.uv.z, 0.0, 1.0);
    s.roughness = clamp(f.uv.w, MIN_ROUGHNESS, 1.0);
    s.occlusion = 1.0;
    s.emissive = vo_emissive(f);
    s.normal = -geometric;
    s.geometric = -geometric;
    s.coat = clamp(vo_layers(f).x, 0.0, 1.0);
    s.coat_roughness = clamp(vo_layers(f).y, MIN_ROUGHNESS, 1.0);
    s.sheen = max(vo_sheen(f).rgb, vec3f(0.0));
    s.sheen_roughness = clamp(vo_sheen(f).w, SHEEN_MIN_ROUGHNESS, 1.0);
    if (front) {
        s.albedo *= albedo.rgb;
        s.alpha *= albedo.a;
        s.metallic = clamp(f.uv.z * orm.b, 0.0, 1.0);
        s.roughness = clamp(f.uv.w * orm.g, MIN_ROUGHNESS, 1.0);
        s.occlusion = orm.r;
        s.emissive *= emissive;
        s.normal = mapped;
        s.geometric = geometric;
    } else if (kind.alpha_cutout != 0u) {
        s.alpha *= albedo.a;
    }

    s.back_factor = 0.0;
    if (material_diffuse_transmission) {
        s.back_factor = clamp(vo_diffuse_transmission(f).w, 0.0, 1.0);
    }
    s.back_color = vec3f(0.0);
    s.thickness = 0.0;
    if (s.back_factor > 0.0) {
        s.back_color = max(vo_diffuse_transmission(f).rgb, vec3f(0.0)) * s.albedo *
            (1.0 - s.metallic) * s.back_factor;
        s.thickness = max(vo_scalars(f).y, 0.0);
    }

    // The direction of the anisotropy: the tangent of the frame turned by the rotation around
    // the normal, made perpendicular to the shading normal.
    s.anisotropy = 0.0;
    s.tangent = vec3f(0.0);
    s.bitangent = vec3f(0.0);
    if (material_anisotropy && vo_layers(f).z > 0.0 && !frame.degenerate) {
        let rotation = vo_layers(f).w * (2.0 * PI);
        let dir = frame.tangent * cos(rotation) + frame.bitangent * sin(rotation);
        let along = dir - s.normal * dot(dir, s.normal);
        let len2 = dot(along, along);
        if (len2 > 1e-12) {
            s.anisotropy = clamp(vo_layers(f).z, 0.0, 1.0);
            s.tangent = along * inverseSqrt(len2);
            s.bitangent = cross(s.normal, s.tangent);
        }
    }
    return s;
}

// Anisotropic GGX distribution (KHR_materials_anisotropy) with the roughness at along the
// tangent and ab along the bitangent.
fn distribution_ggx_anisotropic(at: f32, ab: f32, ndh: f32, tdh: f32, bdh: f32) -> f32 {
    let a2 = at * ab;
    let f = vec3f(ab * tdh, at * bdh, a2 * ndh);
    let w2 = a2 / dot(f, f);
    return a2 * w2 * w2 / PI;
}

// Anisotropic height correlated Smith visibility (KHR_materials_anisotropy).
fn visibility_smith_anisotropic(at: f32, ab: f32, ndl: f32, ndv: f32,
    bdv: f32, tdv: f32, tdl: f32, bdl: f32) -> f32
{
    let gv = ndl * length(vec3f(at * tdv, ab * bdv, ndv));
    let gl = ndv * length(vec3f(at * tdl, ab * bdl, ndl));
    return saturate(0.5 / max(gv + gl, 1e-7));
}

// Smallest sheen roughness, as three.js clamps it.
const SHEEN_MIN_ROUGHNESS: f32 = 0.07;

// Charlie sheen distribution (Estevez and Kulla) at the squared roughness alpha.
fn distribution_charlie(alpha: f32, ndh: f32) -> f32 {
    let inv = 1.0 / alpha;
    let cos2 = ndh * ndh;
    let sin2 = max(1.0 - cos2, 0.0078125);
    return (2.0 + inv) * pow(sin2, inv * 0.5) / (2.0 * PI);
}

// Sheen visibility of Neubelt and Pettineo.
fn visibility_neubelt(ndv: f32, ndl: f32) -> f32 {
    return saturate(1.0 / (4.0 * (ndl + ndv - ndl * ndv)));
}

// Analytic fit of the directional albedo of the Charlie sheen lobe, as three.js uses for image
// based light.
fn sheen_albedo(ndv: f32, roughness: f32) -> f32 {
    let r2 = roughness * roughness;
    let low = roughness < 0.25;
    let a = select(-8.48 * r2 + 14.3 * roughness - 9.95, -339.2 * r2 + 161.4 * roughness - 25.9,
        low);
    let b = select(1.97 * r2 - 3.27 * roughness + 0.72, 44.0 * r2 - 23.7 * roughness + 3.26, low);
    let bias = select(0.1 * (roughness - 0.25), 0.0, low);
    return min(exp(a * ndv + b) + bias, PI);
}

// Energy the base lobes keep under a sheen of a linear color, a constant compensation.
fn sheen_compensation(sheen: vec3f) -> f32 {
    return 1.0 - 0.157 * max(max(sheen.r, sheen.g), sheen.b);
}

// Light reflected toward the eye by one light arriving from direction l with a radiance. A clear
// coat reflects the GGX lobe of its normal and roughness times its Fresnel Fc, and the base below
// it 1 - Fc of what it reflects without one. An anisotropic base stretches its lobe along the
// tangent, its roughness there mixed toward 1 by the square of the strength. A sheen adds its
// Charlie lobe on the mapped normal and scales the base by its compensation, below the coat. A
// diffuse transmission keeps 1 - its factor of the diffuse color.
fn brdf(s: Surface, v: vec3f, l: vec3f, radiance: vec3f) -> Lit {
    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    var diffuse_color = s.albedo * (1.0 - s.metallic);
    if (s.back_factor > 0.0) {
        diffuse_color *= 1.0 - s.back_factor;
    }
    let ndv = max(dot(n, v), 1e-4);
    let h = normalize(l + v);
    let ndl = max(dot(n, l), 0.0);
    let ndh = max(dot(n, h), 0.0);
    let ldh = max(dot(l, h), 0.0);
    let f = fresnel_schlick(ldh, f0);
    var specular = distribution_ggx(ndh, a) * visibility_smith(ndv, ndl, a) * f;
    if (material_anisotropy && s.anisotropy > 0.0) {
        let ab = max(a, 0.001);
        let at = mix(ab, 1.0, s.anisotropy * s.anisotropy);
        let t = s.tangent;
        let b = s.bitangent;
        specular = distribution_ggx_anisotropic(at, ab, ndh, dot(t, h), dot(b, h)) *
            visibility_smith_anisotropic(at, ab, ndl, ndv, dot(b, v), dot(t, v), dot(t, l),
                dot(b, l)) * f;
    }
    let diffuse = (vec3f(1.0) - f) * diffuse_color / PI;
    var lit = Lit(diffuse * radiance * ndl, specular * radiance * ndl);
    if (material_sheen && any(s.sheen > vec3f(0.0))) {
        let alpha = s.sheen_roughness * s.sheen_roughness;
        let comp = sheen_compensation(s.sheen);
        let sheen_specular = s.sheen * distribution_charlie(alpha, ndh) *
            visibility_neubelt(ndv, ndl);
        lit.diffuse *= comp;
        lit.specular = lit.specular * comp + sheen_specular * radiance * ndl;
    }
    if (material_clear_coat && s.coat > 0.0) {
        let nc = s.geometric;
        let ac = s.coat_roughness * s.coat_roughness;
        let ndv_coat = max(dot(nc, v), 1e-4);
        let ndl_coat = max(dot(nc, l), 0.0);
        let ndh_coat = max(dot(nc, h), 0.0);
        let ldh_coat = ldh;
        let fc = fresnel_schlick(ldh_coat, vec3f(0.04)).x * s.coat;
        let coat_specular = distribution_ggx(ndh_coat, ac) *
            visibility_smith(ndv_coat, ndl_coat, ac) * fc;
        lit.diffuse *= 1.0 - fc;
        lit.specular = lit.specular * (1.0 - fc) + coat_specular * radiance * ndl_coat;
    }
    return lit;
}

// Light passed through the surface toward the eye by the back lobe of a diffuse transmission, of
// one light arriving from direction l with a radiance at the far side: a Lambert lobe on the
// inverted normal, without a Fresnel term or a specular lobe. The sheen scales it by its
// compensation and the clear coat by 1 - its Fresnel toward the eye, as they do the front
// diffuse lobe.
fn brdf_back(s: Surface, v: vec3f, l: vec3f, radiance: vec3f) -> vec3f {
    var back = s.back_color / PI * radiance * max(dot(-s.normal, l), 0.0);
    if (material_sheen && any(s.sheen > vec3f(0.0))) {
        back *= sheen_compensation(s.sheen);
    }
    if (material_clear_coat && s.coat > 0.0) {
        let ndv_coat = max(dot(s.geometric, v), 1e-4);
        back *= 1.0 - fresnel_schlick(ndv_coat, vec3f(0.04)).x * s.coat;
    }
    return back;
}

// The front lobes of a light for side 0, the back lobe of a diffuse transmission for side 1
// (lights_direct).
fn light_lobes(s: Surface, v: vec3f, l: vec3f, radiance: vec3f, side: u32) -> Lit {
    if (side == 1u) {
        return Lit(brdf_back(s, v, l, radiance), vec3f(0.0));
    }

    return brdf(s, v, l, radiance);
}

// An unlit view shows the albedo as the diffuse part and the emission as the specular one. The
// back lobe of a diffuse transmission takes no ambient occlusion, which describes the front.
fn shade_split(s: Surface, v: vec3f, world: vec3f) -> Lit {
    if (view.lights.z == 0u) {
        return Lit(s.albedo, s.emissive);
    }

    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    var diffuse_color = s.albedo * (1.0 - s.metallic);
    if (s.back_factor > 0.0) {
        diffuse_color *= 1.0 - s.back_factor;
    }
    let ndv = max(dot(n, v), 1e-4);
    let ab = env_brdf(s.roughness, ndv);
    var r = reflect(-v, n);

    // An anisotropic base reflects about a normal bent toward the direction of the anisotropy,
    // as the glTF sample viewer does.
    if (material_anisotropy && s.anisotropy > 0.0) {
        let along = cross(cross(s.bitangent, v), s.bitangent);
        let len2 = dot(along, along);
        let x = 1.0 - s.anisotropy * (1.0 - s.roughness);
        let k = x * x * x * x;
        if (len2 > 1e-12) {
            let bent = normalize(mix(along * inverseSqrt(len2), n, k));
            r = reflect(-v, bent);
        }
    }
    let spec = mix(r, n, a);
    let cluster = fragment_cluster(world);
    let nc = s.geometric;
    let ac = s.coat_roughness * s.coat_roughness;
    let spec_coat = mix(reflect(-v, nc), nc, ac);
    let coat = material_clear_coat && s.coat > 0.0;
    let e = indirect_lobes(world, n, spec, s.roughness, cluster, s.back_factor > 0.0, nc,
        spec_coat, s.coat_roughness, coat);
    let specular_light = e.specular;
    let diffuse_light = e.diffuse;

    // The split sum scale and bias already integrate the Fresnel over the GGX lobe of the
    // roughness, so the grazing reflection of a rough surface fades without a roughness aware
    // f90. What the lobe reflects is not available to the diffuse term, as 1 - F in brdf().
    let specular_albedo = f0 * ab.x + ab.y;
    var indirect_specular = specular_albedo * specular_light *
        specular_occlusion(ndv, s.occlusion, a);
    var indirect_diffuse = (vec3f(1.0) - specular_albedo) * diffuse_color * diffuse_light *
        s.occlusion;
    var indirect_back = vec3f(0.0);
    if (s.back_factor > 0.0) {
        indirect_back = s.back_color * e.back;
    }

    // A sheen reflects the indirect diffuse light by the fit of its albedo, and the base keeps
    // its compensation of what it reflects without one; the coat below covers both.
    if (material_sheen && any(s.sheen > vec3f(0.0))) {
        let comp = sheen_compensation(s.sheen);
        let indirect_sheen = e.diffuse * s.sheen * sheen_albedo(ndv, s.sheen_roughness) *
            s.occlusion;
        indirect_specular = indirect_specular * comp + indirect_sheen;
        indirect_diffuse *= comp;
        indirect_back *= comp;
    }

    // The clear coat reflects the light along the reflection about its own normal at its own
    // roughness, by the split sum albedo of a dielectric, and the base below it what it does not.
    if (coat) {
        let ndv_coat = max(dot(nc, v), 1e-4);
        let ab_coat = env_brdf(s.coat_roughness, ndv_coat);
        let coat_albedo = (0.04 * ab_coat.x + ab_coat.y) * s.coat;
        indirect_specular = indirect_specular * (1.0 - coat_albedo) +
            coat_albedo * e.coat * specular_occlusion(ndv_coat, s.occlusion, ac);
        indirect_diffuse *= 1.0 - coat_albedo;
        indirect_back *= 1.0 - coat_albedo;
    }

    if (s.back_factor > 0.0) {
        indirect_diffuse += indirect_back;
    }

    let sides = select(1u, 2u, s.back_factor > 0.0);
    let direct = lights_direct(s, v, world, cluster, sides, s.thickness);
    return Lit(direct.diffuse + indirect_diffuse, direct.specular + indirect_specular +
        s.emissive);
}

// Mip of the opaque snapshot for a DiffuseTransmission.blur: the blur radius grows by blur world
// units per world unit between the surface and what is behind it. Contact is sharp, the sky
// (depth 0) takes the top mip, and the roughness blur is the minimum.
fn transmission_blur_lod(world: vec3f, screen_uv: vec2f, blur: f32, rough_lod: f32,
    max_mip: f32) -> f32
{
    let size = vec2i(textureDimensions(opaque_snapshot_depth));
    let depth = textureLoad(opaque_snapshot_depth, min(vec2i(screen_uv * vec2f(size)), size - 1),
        0);
    var blur_lod = max_mip;
    if (depth > 0.0) {
        let h = view.inv_view_proj * vec4f(screen_uv.x * 2.0 - 1.0, 1.0 - screen_uv.y * 2.0,
            depth, 1.0);
        let behind = max(length(h.xyz / h.w - world), 0.0);

        // view.cluster.y is the tangent of half the vertical field of view, or the half height
        // of an orthographic view, where the distance does not matter.
        let radius = behind * blur;
        var eye = 1.0;
        if (view.lights.w == 0u) {
            eye = max(length(world - view.camera.xyz) + behind, 1e-4);
        }
        let pixels = radius / eye / view.cluster.y * view.viewport.y * 0.5;
        blur_lod = log2(max(pixels, 1.0));
    }

    return clamp(max(rough_lod, blur_lod), 0.0, max_mip);
}

// Color of a transmissive fragment, as flecs-engine shades it: the opaque snapshot where the
// refracted ray leaves a volume of the material thickness, reduced by Fresnel and absorbed
// (Beer-Lambert). The transmission factor replaces the diffuse light; specular and emission are
// added on top. It writes no depth, so it takes no ambient occlusion of the view.
fn transmission(f: VertexOut, s: Surface, v: vec3f, reflected: Lit) -> vec4f {
    var ap = vec4f(0.0, 0.0, 0.0, 1.0);
    if (scene_aerial && view.aerial.w > 0.0) {
        ap = aerial_sample(f.pos, f.world);
    }

    let factor = vo_transmission(f).x;
    let ior = vo_transmission(f).y;
    let thickness = vo_transmission(f).z;
    let att_distance = vo_transmission(f).w;
    if (factor <= 0.0) {
        let lit = reflected.diffuse + reflected.specular;
        return vec4f(lit * ap.a + ap.rgb, s.alpha);
    }

    let ndotv = saturate(dot(s.normal, v));

    // Fresnel reflection (Schlick with IOR-derived F0)
    let ior_ratio = (1.0 - ior) / (1.0 + ior);
    let f0_ior = ior_ratio * ior_ratio;
    let fx = 1.0 - ndotv;
    let fx2 = fx * fx;
    let fresnel = f0_ior + (1.0 - f0_ior) * (fx2 * fx2 * fx);

    let refracted = refract(-v, s.normal, 1.0 / max(ior, 0.001));
    let exit_pos = f.world + normalize(refracted) * thickness * vo_model_scale(f);
    let exit_clip = view.view_proj * vec4f(exit_pos, 1.0);
    var screen_uv = exit_clip.xy / exit_clip.w * vec2f(0.5, -0.5) + vec2f(0.5, 0.5);
    screen_uv = clamp(screen_uv, vec2f(0.0), vec2f(1.0));

    let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);
    var bg_lod = s.roughness * max_mip;
    let diffuse_blur = vo_scalars(f).z;
    if (diffuse_blur > 0.0) {
        bg_lod = transmission_blur_lod(f.world, screen_uv, diffuse_blur, bg_lod, max_mip);
    }
    let background = textureSampleLevel(opaque_snapshot, reflection_sampler, screen_uv,
        bg_lod).rgb;

    // Beer-Lambert absorption
    let att_color = max(vo_attenuation(f), vec3f(0.001));
    let att_ratio = thickness / max(att_distance, 0.001);
    let transmittance = pow(att_color, vec3f(att_ratio));
    let transmitted = (1.0 - fresnel) * background * transmittance * s.albedo;

    let tf = saturate(factor);
    let own = reflected.diffuse * (1.0 - tf) + reflected.specular;
    let seen = mix(background * (1.0 - fresnel), transmitted, ap.a);
    let final_color = own * ap.a + ap.rgb * (1.0 - tf) + seen * tf;
    return vec4f(final_color, 1.0);
}

// The artistic rim of a Fresnel (not the physical term of the lobes): its color times its
// strength times fresnel_factor of the shading normal, which back faces flip, adds to the
// emission, untinted; its opacity times the factor adds to the alpha of a blended draw. A
// material without one has a zero record and adds nothing.
fn fresnel_rim(f: VertexOut, shaded: Surface, v: vec3f) -> Surface {
    var s = shaded;
    let strength = f.fresnel.x;
    let opacity = f.fresnel.w;
    if (strength > 0.0 || opacity > 0.0) {
        let rim = fresnel_factor(dot(s.normal, v), f.fresnel.y, f.fresnel.z);
        let color = vec3f(unpack2x16unorm(f.fresnel_color.x), unpack2x16unorm(f.fresnel_color.y).x);
        s.emissive += color * (strength * rim);
        if (kind.blend_mode != 0u) {
            s.alpha = min(s.alpha + opacity * rim, 1.0);
        }
    }
    return s;
}

// Opaque and transmissive fragments share one call of shade_split, as D3D compilers inline every
// call.
fn lit(f: VertexOut, shaded: Surface) -> vec4f {
    var s = shaded;
    if (!transmissive && kind.blend_mode == 0u) {
        s.occlusion = min(s.occlusion, view_occlusion(f.pos));
    }

    let v = normalize(view.camera.xyz - f.world);
    if (material_fresnel) {
        s = fresnel_rim(f, s, v);
    }
    let reflected = shade_split(s, v, f.world);
    if (transmissive) {
        return transmission(f, s, v, reflected);
    }

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
    return lit(f, surface_blend(f, front));
}

@fragment fn fs_blend_cutout(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let geometric = normalize(f.normal);
    var layers: Layers;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        let b = blend_fragment(f);
        let albedo = blend_fragment_albedo(b);
        if (f.color.a * albedo.a < vo_scalars(f).x) {
            discard;
        }
        layers = blend_maps(geometric, b, albedo, front);
    }

    let s = surface_of(f, front, geometric, layers);
    if (s.alpha < vo_scalars(f).x) {
        discard;
    }

    return lit(f, s);
}
