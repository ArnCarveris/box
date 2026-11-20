// Forward pass of the meshes the mesh shader does not draw (box_draws_extended): transmissive
// (Transmission, Distortion), Water, with a triplanar TextureMapping, Sheen, ClearCoat,
// SpecularAnisotropy, DiffuseTransmission or Fresnel, or with a PlanarReflection, with or
// without a TextureBlend.
#include "include/mesh_common.wgsl"
#include "include/mesh_indirect.wgsl"
#include "include/mesh_direct.wgsl"
#include "include/mesh_surface.wgsl"
#include "include/mesh_blend.wgsl"
#include "include/terrain_bake.wgsl"
#ifdef BOX_TEXTURE_LAYERS
#include "include/mesh_layers.wgsl"
#endif

// The material features of the scene (Scene) compile out the code of those it lacks; the alpha
// test, the blend modes and the TextureBlend have no override. transmissive is set for the
// pipelines of Transmission shapes, through which the opaque snapshot of the view shows, and
// water, with transmissive, for those of Water shapes of a scene with Scene.water.
// material_fresnel, scene_planar and material_distortion are in mesh_common.wgsl, the vertex
// stage reads them too.
override material_sheen: bool = true;
override material_anisotropy: bool = true;
override material_clear_coat: bool = true;
override material_diffuse_transmission: bool = true;
override material_triplanar: bool = true;
override transmissive: bool = false;

// Foam texture of a Water (Water.foam_texture), its red as stored; white without one. Only fs
// samples it, so the TextureBlend entry points keep the blend group without it.
@group(1) @binding(12) var foam_map: texture_2d<f32>;

// F0 of a Water surface, of an index of refraction of 1.33.
const WATER_F0: f32 = 0.02;

// One travelling Gerstner wave of the wave table (box_render_water_wave_t): its normalized
// direction in world XZ, wave number k (0 for a disabled wave) and phase in a; its slope
// (amplitude times k) and steepness (Gerstner Q, 0..1) in b.xy.
struct WaterWave {
    a: vec4f,
    b: vec4f,
};

// The WaterWaves of a Water material (box_render_water_waves_t): its waves, the weight of the
// normal map over them and the number of enabled waves.
struct WaterWaveSet {
    waves: array<WaterWave, 4>,
    detail: f32,
    count: u32,
    pad: vec2f,
};

// Sets of the wave table (BOX_RENDER_WATER_WAVE_SETS).
const WATER_WAVE_SETS: u32 = 128u;

// The wave table (BOX_RENDER_BUFFER_WATER_WAVES), by the set a Water record names, set 0
// unused; zero before the main thread sent one.
@group(0) @binding(24) var<uniform> water_wave_sets: array<WaterWaveSet, WATER_WAVE_SETS>;

// The heights of the field of WaterRipples of the scene of the view (ripple_a, ripple_b and
// ripple_c of View) in world units, cell l of the window at texel (ripple_c.yz + l) mod
// ripple_c.x; a 1x1 zero texture without one.
@group(0) @binding(25) var water_ripple_heights: texture_storage_2d<r32float, read>;
// The foam of the same field, 0..1, at the same texels; a 1x1 zero texture without one.
@group(0) @binding(26) var water_ripple_foam: texture_storage_2d<r32float, read>;
// The velocity of the water of the same field at the same texels: the world velocity of a cell
// (xy) and its velocity relative to the frame of the field (zw); a 1x1 zero texture without one.
@group(0) @binding(27) var water_ripple_flow: texture_storage_2d<rgba32float, read>;

// The spectrum of WaterSpectrum of the scene of the view (spectrum_a, spectrum_b and
// spectrum_c of View), a tiling layer per cascade with mips: the slope of the heights along world
// X and Z (xy), the height in world units (z) and the square of the slope (w), so a mip that
// averages texels keeps the slope variance it filtered away as w - dot(xy, xy). Sampled through
// the repeating cookie sampler; a 1x1 zero array without one.
@group(0) @binding(28) var water_spectrum: texture_2d_array<f32>;

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

// Planar reflections of the view (gpu_planar.c): layer k holds the mirrored image of reflection
// slot k in the corner of its share of the array (view.planar[k].yz), its colors multiplied by
// its coverage, which is 0 where the reflection drew nothing; mip m + 1 is downsampled from mip
// m. A view without planar reflections, and the captures, bind a 1x1 transparent array.
@group(0) @binding(21) var planar_images: texture_2d_array<f32>;

// Roughness from which the planar reflection fades to the probes, and to which it has faded.
const PLANAR_FADE_START: f32 = 0.6;
const PLANAR_FADE_END: f32 = 0.8;

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
    shade: f32,
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
    let n = f.object_normal.xyz;
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
        let m = textureSampleGrad(normal_map, material_sampler, normal_uv(f, uv), gx[k],
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
    let n = f.object_normal.xyz;
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
    let offset = blend_offset(f);
    // The mask of a terrain tile is the same for every projection
    let terrain = (blending.flags & BLEND_TERRAIN) != 0u;
    var mask = vec4f(0.0);
    if (terrain) {
        mask = blend_mask_at(f, offset, gx[0], gy[0]);
    }
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
        let uv = planes[k] + offset;
        if (!terrain) {
            mask = textureSampleGrad(blend_mask, material_sampler, uv, gx[k], gy[k]);
        }
        let lw = blend_height_weights(blend_heights, material_sampler, blending,
            blend_weights(mask, blending), uv, gx[k], gy[k]);
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

// The layers of a terrain fragment that takes its bake alone (terrain_bake_material): its baked
// shading normal, the other maps neutral, so surface_of gives the constants of its material.
fn terrain_baked_layers(b: TerrainBaked) -> Layers {
    var layers: Layers;
    layers.albedo = vec4f(1.0);
    layers.emissive = vec3f(1.0);
    layers.orm = vec3f(1.0);
    layers.normal = b.normal;
    layers.frame = Frame(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 0.0, 1.0), true);
    return layers;
}

// The surface of the neutral layers of a terrain fragment that takes its bake alone, with the
// baked albedo, roughness, metallic and occlusion.
fn terrain_baked_apply(near: Surface, b: TerrainBaked) -> Surface {
    var s = near;
    s.albedo = b.albedo;
    s.roughness = clamp(b.roughness, MIN_ROUGHNESS, 1.0);
    s.metallic = b.metallic;
    s.occlusion = b.occlusion;
    return s;
}

// The surface of a terrain fragment shaded in full, faded toward the surface of its bake by how
// far the bake stands in for it (terrain_bake_far), and lit by the sun and the ambient occlusion
// its heights let through, less so on steep faces (TERRAIN_BAKE_STEEP). The bake is read after
// the material, so nothing of it stays live while the material is shaded.
fn terrain_baked_mix(near: Surface, f: VertexOut, front: bool, geometric: vec3f, far: f32)
    -> Surface {
    var s = near;
    let light = terrain_bake_light(f);
    if (light.w == 0.0) {
        return s;
    }
    if (far > 0.0 && front) {
        let baked = terrain_bake_material(f);
        s.albedo = mix(s.albedo, baked.albedo, far);
        s.roughness = mix(s.roughness, clamp(baked.roughness, MIN_ROUGHNESS, 1.0), far);
        s.metallic = mix(s.metallic, baked.metallic, far);
        s.occlusion = mix(s.occlusion, baked.occlusion, far);
        s.normal = normalize(mix(s.normal, baked.normal, far));
    }
    let flat = smoothstep(TERRAIN_BAKE_STEEP, TERRAIN_BAKE_FLAT, geometric.y);
    s.occlusion *= mix(1.0, light.y, flat);
    s.shade = (1.0 - light.x) * flat;
    return s;
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
// diffuse transmission keeps 1 - its factor of the diffuse color. The lobes of the mapped normal
// fade by its shadow terminator (shadow_terminator); the coat follows the geometric normal.
fn brdf(s: Surface, v: vec3f, l: vec3f, radiance: vec3f) -> Lit {
    let n = s.normal;
    let a = s.roughness * s.roughness;
    var f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    if (water) {
        f0 = vec3f(WATER_F0);
    }
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
    let shaded = ndl * shadow_terminator(s.geometric, n, l);
    var lit = Lit(diffuse * radiance * shaded, specular * radiance * shaded);
    if (material_sheen && any(s.sheen > vec3f(0.0))) {
        let alpha = s.sheen_roughness * s.sheen_roughness;
        let comp = sheen_compensation(s.sheen);
        let sheen_specular = s.sheen * distribution_charlie(alpha, ndh) *
            visibility_neubelt(ndv, ndl);
        lit.diffuse *= comp;
        lit.specular = lit.specular * comp + sheen_specular * radiance * shaded;
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
// inverted normal, faded by its shadow terminator (shadow_terminator), without a Fresnel term or
// a specular lobe. The sheen scales it by its compensation and the clear coat by 1 - its Fresnel
// toward the eye, as they do the front diffuse lobe.
fn brdf_back(s: Surface, v: vec3f, l: vec3f, radiance: vec3f) -> vec3f {
    var back = s.back_color / PI * radiance * max(dot(-s.normal, l), 0.0) *
        shadow_terminator(-s.geometric, -s.normal, l);
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

// The planar reflection a fragment samples: its slot plus one, 0 for none, and its point on the
// screen in [0, 1].
struct PlanarLookup {
    slot: u32,
    screen: vec2f,
};

// The planar reflection of a fragment of an instance that is the shape of a reflection plane of
// the view with an image this frame (planar_owner).
fn planar_lookup(f: VertexOut) -> PlanarLookup {
    var slot = 0u;
    let planar = f.planar & 0xFFu;
    if (scene_planar && planar > 0u && planar <= 2u && view.planar[planar - 1u].w > 0.0) {
        slot = planar;
    }
    return PlanarLookup(slot, f.pos.xy * view.viewport.zw);
}

// Radiance the planar reflection of a fragment reflects along its normal n, in place of the
// probe and environment radiance fallback: the image at the screen point moved by the
// distortion times the view space tilt of n off the geometric normal, by a fraction of the image
// height on both axes, clamped to the image, at the mip of the roughness. It fades to fallback
// where the image shows nothing and between PLANAR_FADE_START and PLANAR_FADE_END.
fn planar_radiance(p: PlanarLookup, n: vec3f, geometric: vec3f, roughness: f32,
    fallback: vec3f) -> vec3f {
    if (!scene_planar || p.slot == 0u) {
        return fallback;
    }

    let fade = 1.0 - smoothstep(PLANAR_FADE_START, PLANAR_FADE_END, roughness);
    if (fade <= 0.0) {
        return fallback;
    }

    let info = view.planar[p.slot - 1u];
    let tilt = (view.view * vec4f(n - geometric * dot(n, geometric), 0.0)).xy;
    let aspect = view.viewport.y * view.viewport.z;
    let uv = clamp(p.screen + info.x * vec2f(tilt.x * aspect, -tilt.y), vec2f(0.0),
        vec2f(1.0));
    let top = max(info.w - 1.0, 0.0);
    let lod = clamp(roughness, 0.0, 1.0) * top;
    let level = min(u32(ceil(lod)), u32(top));
    let half_texel = 0.5 / vec2f(textureDimensions(planar_images, level));
    let at = clamp(uv * info.yz, half_texel, max(info.yz - half_texel, half_texel));
    let image = textureSampleLevel(planar_images, reflection_sampler, at, p.slot - 1u, lod);
    let coverage = clamp(image.a, 0.0, 1.0);
    let radiance = max(image.rgb, vec3f(0.0)) / max(coverage, 1e-4);
    return mix(fallback, radiance, coverage * fade);
}

// An unlit view shows the albedo as the diffuse part and the emission as the specular one, a
// view of an impostor frame its channel (impostor_channel). The back lobe of a diffuse
// transmission takes no ambient occlusion, which describes the front.
// The shape of a planar reflection reflects its image (planar_radiance) in place of the probes,
// with the base and the clear coat.
fn shade_split(s: Surface, v: vec3f, world: vec3f, planar: PlanarLookup) -> Lit {
    if (view.lights.z > 1u) {
        return impostor_channel(s.albedo, s.normal, world);
    }

    if (view.lights.z == 0u) {
        return Lit(s.albedo, s.emissive);
    }

    let n = s.normal;
    let a = s.roughness * s.roughness;
    var f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    if (water) {
        f0 = vec3f(WATER_F0);
    }
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
    let specular_light = planar_radiance(planar, n, s.geometric, s.roughness, e.specular);
    let diffuse_light = e.diffuse;
    var coat_light = e.coat;
    if (coat) {
        coat_light = planar_radiance(planar, nc, nc, s.coat_roughness, e.coat);
    }

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
            coat_albedo * coat_light * specular_occlusion(ndv_coat, s.occlusion, ac);
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

// Hash of a lattice point of the distortion noise.
fn distortion_hash(c: vec3i) -> u32 {
    let h = bitcast<vec3u>(c) * vec3u(0x8da6b343u, 0xd8163841u, 0xcb1ab31fu);
    var x = h.x ^ h.y ^ h.z;
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// Next hash of a chain of hashes of the same lattice point.
fn distortion_rehash(h: u32) -> u32 {
    var x = h * 0x9e3779b9u + 0x632be5abu;
    x ^= x >> 15u;
    x *= 0x2c1b3c6du;
    x ^= x >> 12u;
    return x;
}

// Gradient of a lattice point at a phase: it turns once per phase in the plane of two hashed
// vectors, from a hashed angle and in a hashed direction, so the noise evolves in place and
// repeats when the phase wraps to 0.
fn distortion_gradient(a: u32, b: u32, phase: f32) -> vec3f {
    let ha = unpack4x8unorm(a);
    let hb = unpack4x8unorm(b);
    let turn = select(1.0, -1.0, hb.w >= 0.5);
    let angle = 6.283185307 * (ha.w + turn * phase);
    return (ha.xyz * 2.0 - 1.0) * cos(angle) + (hb.xyz * 2.0 - 1.0) * sin(angle);
}

// Two channels of evolving gradient noise (rotating gradients) at a point in features,
// about -1..1.
fn distortion_noise(p: vec3f, phase: f32) -> vec2f {
    let cell = floor(p);
    let x = p - cell;
    let u = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
    let base = vec3i(cell);
    var sum = vec2f(0.0);
    for (var k = 0u; k < 8u; k++) {
        let corner = vec3f(f32(k & 1u), f32((k >> 1u) & 1u), f32((k >> 2u) & 1u));
        let h0 = distortion_hash(base + vec3i(corner));
        let h1 = distortion_rehash(h0);
        let h2 = distortion_rehash(h1);
        let h3 = distortion_rehash(h2);
        let w = mix(vec3f(1.0) - u, u, corner);
        let d = x - corner;
        sum += vec2f(dot(distortion_gradient(h0, h1, phase), d),
            dot(distortion_gradient(h2, h3, phase), d)) * (w.x * w.y * w.z);
    }
    return clamp(sum * 1.6, vec2f(-1.0), vec2f(1.0));
}

// Screen space offset of the snapshot by the Distortion of a fragment of a material_distortion
// pipeline, in UVs: its noise at the object position of the fragment times its strength, a
// fraction of the image height on both axes; 0 without one.
fn distortion_offset(f: VertexOut) -> vec2f {
    var result = vec2f(0.0);
    if (material_distortion && !water) {
        let d = vo_distortion(f);
        if (d.strength > 0.0) {
            let n = distortion_noise(f.object.xyz * d.frequency.xyx, d.phase);
            let aspect = view.viewport.y * view.viewport.z;
            result = d.strength * vec2f(n.x * aspect, -n.y);
        }
    }
    return result;
}

// Whether a fragment of a material_distortion pipeline has a Distortion without Transmission
// (DISTORTION_SHARP).
fn distortion_sharp(f: VertexOut) -> bool {
    return material_distortion && !water && (vo_distortion(f).flags & DISTORTION_SHARP) != 0u;
}

// Color of a transmissive fragment, as flecs-engine shades it: the opaque snapshot where the
// refracted ray leaves a volume of the material thickness, reduced by Fresnel and absorbed
// (Beer-Lambert). The transmission factor replaces the diffuse light; specular and emission are
// added on top. It writes no depth, so it takes no ambient occlusion of the view.
//
// A Distortion offsets the point of the snapshot by its noise (distortion_offset), clamped to
// the image. Without Transmission (distortion_sharp) the snapshot shows at its sharp level,
// without Fresnel, the albedo or absorption.
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
    let sharp = distortion_sharp(f);

    // Fresnel reflection (Schlick with IOR-derived F0)
    let ior_ratio = (1.0 - ior) / (1.0 + ior);
    let f0_ior = ior_ratio * ior_ratio;
    let fx = 1.0 - ndotv;
    let fx2 = fx * fx;
    let fresnel = select(f0_ior + (1.0 - f0_ior) * (fx2 * fx2 * fx), 0.0, sharp);

    let refracted = refract(-v, s.normal, 1.0 / max(ior, 0.001));
    let exit_pos = f.world + normalize(refracted) * thickness * vo_model_scale(f);
    let exit_clip = view.view_proj * vec4f(exit_pos, 1.0);
    var screen_uv = exit_clip.xy / exit_clip.w * vec2f(0.5, -0.5) + vec2f(0.5, 0.5);
    screen_uv = clamp(screen_uv + distortion_offset(f), vec2f(0.0), vec2f(1.0));

    let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);
    var bg_lod = s.roughness * max_mip;
    let diffuse_blur = vo_scalars(f).z;
    if (diffuse_blur > 0.0) {
        bg_lod = transmission_blur_lod(f.world, screen_uv, diffuse_blur, bg_lod, max_mip);
    }
    if (sharp) {
        bg_lod = 0.0;
    }
    let background = textureSampleLevel(opaque_snapshot, reflection_sampler, screen_uv,
        bg_lod).rgb;

    // Beer-Lambert absorption
    let att_color = max(vo_attenuation(f), vec3f(0.001));
    let att_ratio = thickness / max(att_distance, 0.001);
    let transmittance = pow(att_color, vec3f(att_ratio));
    let albedo = select(s.albedo, vec3f(1.0), sharp);
    let transmitted = (1.0 - fresnel) * background * transmittance * albedo;

    let tf = saturate(factor);
    let own = reflected.diffuse * (1.0 - tf) + reflected.specular;
    let seen = mix(background * (1.0 - fresnel), transmitted, ap.a);
    let final_color = own * ap.a + ap.rgb * (1.0 - tf) + seen * tf;
    return vec4f(final_color, 1.0);
}

// Texture coordinates of the layers of a fragment without a triplanar blend, as hard_layers
// takes them.
fn water_uv(f: VertexOut) -> vec2f {
    var uv = f.uv.xy;
    if (vo_scalars(f).w != 0.0) {
        uv = mapping_uv(f.object, f.object_normal.xyz, f.uv.xy);
    }
    return uv;
}

// World units of a cell of the noise that offsets the phase of the flow mapping per pixel.
const WATER_FLOW_NOISE_CELL: f32 = 4.0;

// Hash of a cell of the phase noise of the flow mapping, 0..1.
fn water_flow_hash(c: vec2i) -> f32 {
    var h = (bitcast<u32>(c.x) * 0x8da6b343u) ^ (bitcast<u32>(c.y) * 0xd8163841u);
    h = (h ^ (h >> 16u)) * 0x7feb352du;
    h = (h ^ (h >> 15u)) * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return f32(h & 0xFFFFu) / 65535.0;
}

// Smooth value noise over the world XZ, 0..1, the phase offset of the flow mapping: it changes
// slowly across the surface, so neighbouring pixels reset at different times without tearing
// the pattern apart. The noise is that of the absolute position: the cells of the local origin
// (world_origin of View.origin, its whole units over the 4 of a cell, and the rest) join the
// local ones as a whole number and a rest, so it is the same across a shift of the origin and
// precise far from it.
fn water_flow_noise(xz: vec2f) -> f32 {
    let o = world_origin(view.origin);
    let whole = o.whole.xz;
    let rest = (vec2f(whole & vec2i(3)) + o.rest.xz) / WATER_FLOW_NOISE_CELL;
    let p = xz / WATER_FLOW_NOISE_CELL + rest;
    let c = vec2i(floor(p)) + (whole >> vec2u(2u));
    let t = p - floor(p);
    let w = t * t * (3.0 - 2.0 * t);
    return mix(
        mix(water_flow_hash(c), water_flow_hash(c + vec2i(1, 0)), w.x),
        mix(water_flow_hash(c + vec2i(0, 1)), water_flow_hash(c + vec2i(1, 1)), w.x),
        w.y);
}

// World units of a cell of the noise that offsets the phase of the shore waves of a Water, a
// power of two, and the share of a period the offset spans.
const WATER_SHORE_NOISE_CELL: f32 = 16.0;
const WATER_SHORE_NOISE_SHIFT: u32 = 4u;
const WATER_SHORE_SPREAD: f32 = 0.6;

// Smooth value noise over the world XZ in cells of WATER_SHORE_NOISE_CELL, 0..1, of the absolute
// position like water_flow_noise, so it stays put across a shift of the origin.
fn water_shore_noise(xz: vec2f) -> f32 {
    let o = world_origin(view.origin);
    let whole = o.whole.xz;
    let mask = vec2i(i32(WATER_SHORE_NOISE_CELL) - 1);
    let rest = (vec2f(whole & mask) + o.rest.xz) / WATER_SHORE_NOISE_CELL;
    let p = xz / WATER_SHORE_NOISE_CELL + rest;
    let c = vec2i(floor(p)) + (whole >> vec2u(WATER_SHORE_NOISE_SHIFT));
    let t = p - floor(p);
    let w = t * t * (3.0 - 2.0 * t);
    return mix(
        mix(water_flow_hash(c), water_flow_hash(c + vec2i(1, 0)), w.x),
        mix(water_flow_hash(c + vec2i(0, 1)), water_flow_hash(c + vec2i(1, 1)), w.x),
        w.y);
}

// Foam of the shore waves of the material of a Water fragment at the water depth d
// (water_shore): their phase moved back by up to WATER_SHORE_SPREAD of a period by a noise that
// changes slowly along the shore, so the waves do not arrive everywhere at once and their lines
// are not parallel to the depth contours. 0 without shore waves.
fn water_shore_foam(world: vec3f, m: Material, d: f32, deep: bool) -> f32 {
    let reach = water_shore_reach(m);
    if (reach <= 0.0) {
        return 0.0;
    }
    let phase = fract(water_shore_phase(m) - WATER_SHORE_SPREAD * water_shore_noise(world.xz));
    return water_shore(d, reach, phase, deep);
}

// World velocity of a cell of the window of the field of WaterRipples, clamped to the window.
fn water_ripple_flow_cell(cell: vec2i) -> vec2f {
    let r = view.ripple_c.x;
    let c = vec2u(clamp(cell, vec2i(0), vec2i(i32(r) - 1)));
    return textureLoad(water_ripple_flow, (c + view.ripple_c.yz) % r).xy;
}

// World units between the samples over which the textures of a Water average the velocity of
// the field of WaterRipples. The field switches between abutting Water shapes within a cell,
// which the offsets of the flow mapping would show as a seam; averaged they shear instead.
const WATER_FLOW_SMOOTH: f32 = 0.75;

// World velocity of the field of WaterRipples at a point of its window in cells (bilinear
// between the four nearest cells).
fn water_ripple_flow_at(p: vec2f) -> vec2f {
    let base = vec2i(floor(p));
    let t = p - floor(p);
    return mix(
        mix(water_ripple_flow_cell(base), water_ripple_flow_cell(base + vec2i(1, 0)), t.x),
        mix(water_ripple_flow_cell(base + vec2i(0, 1)),
            water_ripple_flow_cell(base + vec2i(1, 1)), t.x),
        t.y);
}

// World XZ velocity the textures of a Water fragment flow with: inside the window of the field
// of WaterRipples of the view, within its height band, the world velocity of the field there
// averaged over four bilinear samples WATER_FLOW_SMOOTH apart, so it is continuous across the seams
// between Water shapes; own (the WaterFlow of the shape) elsewhere, faded between the two over
// the border band of the window like the ripple normals.
fn water_flow_velocity(world: vec3f, own: vec2f) -> vec2f {
    if (view.ripple_c.w == 0u || abs(world.y - view.ripple_b.x) > view.ripple_b.y) {
        return own;
    }
    let r = f32(view.ripple_c.x);
    let p = (world.xz - view.ripple_a.xy) * view.ripple_a.z - 0.5;
    if (any(p < vec2f(0.0)) || any(p > vec2f(r - 1.0))) {
        return own;
    }
    let s = 0.5 * WATER_FLOW_SMOOTH * view.ripple_a.z;
    let v = 0.25 * (water_ripple_flow_at(p + vec2f(-s, -s)) +
        water_ripple_flow_at(p + vec2f(s, -s)) + water_ripple_flow_at(p + vec2f(-s, s)) +
        water_ripple_flow_at(p + vec2f(s, s)));
    let edge = min(min(p.x, p.y), min(r - 1.0 - p.x, r - 1.0 - p.y));
    return mix(own, v, smoothstep(0.0, max(view.ripple_b.z, 1.0), edge));
}

// The two phase flow mapping of the textures of a Water fragment: the UV offsets of its two
// samples (subtracted from the UVs, so the pattern travels along the flow), the weight of the
// first (the second weighs 1 - weight), and the screen derivatives of the UVs the samples take
// their level of detail from. Without WaterFlow.textures both offsets are 0 and the weight 1.
struct WaterFlowMap {
    a: vec2f,
    b: vec2f,
    weight: f32,
    dx: vec2f,
    dy: vec2f,
};

// The flow mapping at the UVs of a Water fragment: its velocity (water_flow_velocity) is taken
// to the texture space of the shape with the screen derivatives of the UVs and the world XZ
// (d uv / d xz), and moved by two sawtooth phases half a period apart, each weighted by a
// triangle that is 0 where it resets; the phase is offset per pixel by water_flow_noise. Water is
// a flat surface, so the UVs are affine in the world XZ over a triangle.
@diagnostic(off, derivative_uniformity)
fn water_flow_map(f: VertexOut, uv: vec2f) -> WaterFlowMap {
    var o: WaterFlowMap;
    o.a = vec2f(0.0);
    o.b = vec2f(0.0);
    o.weight = 1.0;
    o.dx = dpdx(uv);
    o.dy = dpdy(uv);
    let wx = dpdx(f.world.xz);
    let wy = dpdy(f.world.xz);
    let flow = vo_water_flow(f);
    if (!flow.on) {
        return o;
    }
    // d uv / d xz = [dx dy] inverse([wx wy])
    let det = wx.x * wy.y - wy.x * wx.y;
    if (abs(det) < 1e-20) {
        return o;
    }
    let world = water_flow_velocity(f.world, flow.velocity);
    let inv_x = vec2f(wy.y, -wx.y) / det;
    let inv_y = vec2f(-wy.x, wx.x) / det;
    let along = world.x * inv_x + world.y * inv_y;
    let velocity = o.dx * along.x + o.dy * along.y;
    let p0 = fract(flow.phase + water_flow_noise(f.world.xz));
    let p1 = fract(p0 + 0.5);
    o.a = velocity * ((p0 - 0.5) * WATER_FLOW_PERIOD);
    o.b = velocity * ((p1 - 0.5) * WATER_FLOW_PERIOD);
    o.weight = 1.0 - abs(2.0 * p0 - 1.0);
    return o;
}

// Shortening of a normal map normal that Toksvig ignores: that of the 8 bit quantization of an
// unfiltered one, so water whose normal map is not minified keeps its roughness.
const WATER_TOKSVIG_DEAD: f32 = 0.01;

// Largest roughness the slope variance lost to filtering widens a Water to, so far water does
// not look milky; a rougher material keeps its roughness.
const WATER_MAX_ROUGHNESS: f32 = 0.5;

// Slope variance a filtered (mip mapped) normal map normal n in tangent space lost, from how
// much shorter than 1 it is (Toksvig): (1 - |n|) / |n|, less the quantization of an unfiltered
// one.
fn water_normal_variance(n: vec3f) -> f32 {
    let len = length(n);
    return max(1.0 - WATER_TOKSVIG_DEAD - len, 0.0) / max(len, 1e-3);
}

// Roughness of a Water widened by the slope variance lost to filtering (specular
// antialiasing): alpha^2 grows by twice the variance (Toksvig, LEAN mapping), alpha the square
// of the roughness, up to WATER_MAX_ROUGHNESS; without variance the roughness is unchanged.
fn water_roughness(roughness: f32, variance: f32) -> f32 {
    if (variance <= 0.0) {
        return roughness;
    }
    let a = roughness * roughness;
    let widened = sqrt(sqrt(a * a + 2.0 * variance));
    return max(roughness, min(widened, WATER_MAX_ROUGHNESS));
}

// A sample of the normal map of a Water in tangent space at UVs, with the level of detail of
// the derivatives.
fn water_normal_sample(uv: vec2f, dx: vec2f, dy: vec2f) -> vec3f {
    return textureSampleGrad(normal_map, material_sampler, uv, dx, dy).xyz * 2.0 - 1.0;
}

// The normal map of a Water at its UVs moved by the flow mapping (scaled by scale, then moved by
// offset): the two phase samples cross faded (xyz), and their slope variance lost to filtering
// (water_normal_variance) cross faded the same way (w).
fn water_normal_flowed(flow: WaterFlowMap, uv: vec2f, scale: f32, offset: vec2f) -> vec4f {
    let dx = flow.dx * scale;
    let dy = flow.dy * scale;
    let a = water_normal_sample((uv - flow.a) * scale + offset, dx, dy);
    let b = water_normal_sample((uv - flow.b) * scale + offset, dx, dy);
    return vec4f(mix(b, a, flow.weight),
        mix(water_normal_variance(b), water_normal_variance(a), flow.weight));
}

// The layers of a Water fragment and the slope variance its normal map samples lost to
// filtering (water_normal_variance), which widens its roughness (water_roughness).
struct WaterLayers {
    layers: Layers,
    variance: f32,
};

// The layers of a Water fragment: those of hard_layers, its normal map blended with a second
// sample at ripple_scale times the UVs moved by the ripple offset (whiteout blend). With
// WaterFlow.textures both samples move with the flow (water_flow_map), the TextureScroll offset
// on top. Water draws with the hard projection of a TextureMapping. ripple_scale is flat, the
// same for every fragment of a quad, so the implicit derivatives hold. The variance is the sum
// of that of the samples, as the whiteout blend adds their slopes.
@diagnostic(off, derivative_uniformity)
fn water_layers(f: VertexOut, geometric: vec3f, m: Material) -> WaterLayers {
    var o: WaterLayers;
    o.layers = hard_layers(f, geometric);
    let uv = water_uv(f);
    let flow = water_flow_map(f, uv);
    let flows = vo_water_flow(f).on;
    if (m.water_ripple_scale > 0.0) {
        var n1: vec4f;
        var n2: vec4f;
        if (flows) {
            n1 = water_normal_flowed(flow, uv, 1.0, normal_uv(f, vec2f(0.0)));
            n2 = water_normal_flowed(flow, uv, m.water_ripple_scale, m.water_ripple_offset);
        } else {
            let a = textureSample(normal_map, material_sampler, normal_uv(f, uv)).xyz * 2.0 -
                1.0;
            let b = textureSample(normal_map, material_sampler,
                uv * m.water_ripple_scale + m.water_ripple_offset).xyz * 2.0 - 1.0;
            n1 = vec4f(a, water_normal_variance(a));
            n2 = vec4f(b, water_normal_variance(b));
        }
        o.layers.normal = perturb(geometric, o.layers.frame,
            vec3f(n1.xy + n2.xy, n1.z * n2.z));
        o.variance = n1.w + n2.w;
    } else if (flows) {
        let n1 = water_normal_flowed(flow, uv, 1.0, normal_uv(f, vec2f(0.0)));
        o.layers.normal = perturb(geometric, o.layers.frame, n1.xyz);
        o.variance = n1.w;
    } else {
        // The sample of hard_layers again
        let n1 = textureSample(normal_map, material_sampler, normal_uv(f, uv)).xyz * 2.0 - 1.0;
        o.variance = water_normal_variance(n1);
    }
    return o;
}

// The normal of a Water fragment under the travelling waves of a set of the wave table: the
// analytic normal of the sum of its Gerstner waves at the world XZ of the fragment, with the
// normal map normal blended over it (whiteout), its tilt scaled by detail. The surface is taken
// to face +Y, as Water is seen from above. Each wave fades out as its wavelength shrinks from 4
// to 2 pixels along the screen axes (dx and dy, the world XZ derivatives of the fragment), so
// distant water does not alias; the steepness lifts by Q / count of the waves (Gerstner).
// water_waves_variance gives the slope variance the fading takes away.
fn water_waves_normal(index: u32, xz: vec2f, dx: vec2f, dy: vec2f, mapped: vec3f) -> vec3f {
    let s = water_wave_sets[min(index, WATER_WAVE_SETS - 1u)];
    return water_slope_normal(water_waves_slope(index, xz, dx, dy), mapped, s.detail);
}

// The slope of the travelling waves of a set of the wave table at the world XZ of a fragment
// (xy, faded like water_waves_normal) and the lift of their Gerstner steepness (z).
fn water_waves_slope(index: u32, xz: vec2f, dx: vec2f, dy: vec2f) -> vec3f {
    let s = water_wave_sets[min(index, WATER_WAVE_SETS - 1u)];
    let count = f32(max(s.count, 1u));
    var slope = vec2f(0.0);
    var lift = 0.0;
    for (var i = 0u; i < 4u; i++) {
        let w = s.waves[i];
        let k = w.a.z;
        if (k <= 0.0 || w.b.x <= 0.0) {
            continue;
        }
        let fade = water_wave_fade(w, dx, dy);
        let theta = k * dot(w.a.xy, xz) - w.a.w;
        slope += w.a.xy * (w.b.x * fade * cos(theta));
        lift += w.b.y * fade * sin(theta) / count;
    }
    return vec3f(slope, lift);
}

// The normal of waves of a slope along world X and Z (slope.xy) and a Gerstner lift (slope.z)
// with the normal map normal blended over it (whiteout), its tilt scaled by detail.
fn water_slope_normal(slope: vec3f, mapped: vec3f, detail: f32) -> vec3f {
    let waves = normalize(vec3f(-slope.x, max(1.0 - slope.z, 1e-3), -slope.y));
    let tilt = normalize(vec3f(mapped.x * detail, max(mapped.y, 1e-3), mapped.z * detail));
    return normalize(vec3f(waves.x + tilt.x, waves.y * tilt.y, waves.z + tilt.z));
}

// The spectrum of WaterSpectrum at the world XZ of a fragment: the sum of the slopes of its
// cascades (slope), of their heights over twice the standard deviation of the height of the
// spectrum (height, about -1..1) and of the slope variance their mips filtered away
// (variance), each cascade sampled at the level of detail of the world XZ derivatives dx and dy
// of the fragment over its tile. The variance fades in as the footprint grows from one texel to
// two: where a texel covers pixels, the bilinear blend of the squared slopes exceeds the square
// of the blended slopes between texel centers, which would roughen the water in a grid.
struct WaterSpectrumSample {
    slope: vec2f,
    height: f32,
    variance: f32,
};

fn water_spectrum_sample(xz: vec2f, dx: vec2f, dy: vec2f) -> WaterSpectrumSample {
    var o: WaterSpectrumSample;
    let p = xz + view.spectrum_a.xy;
    let cascades = min(view.spectrum_c.x, 3u);
    let texels = f32(textureDimensions(water_spectrum).x);
    let footprint = max(length(dx), length(dy)) * texels;
    for (var c = 0u; c < cascades; c++) {
        let inv = view.spectrum_b[c];
        let t = textureSampleGrad(water_spectrum, cookie_sampler, p * inv, c, dx * inv,
            dy * inv);
        o.slope += t.xy;
        o.height += t.z;
        o.variance += max(t.w - dot(t.xy, t.xy), 0.0) * saturate(footprint * inv - 1.0);
    }
    o.height *= view.spectrum_a.w;
    return o;
}

// Height of the travelling waves of a set of the wave table at the world XZ of a fragment over
// the sum of their amplitudes, -1 in the troughs to 1 on the crests: the sum of the heights
// (amplitude times sin theta, whose slope water_waves_normal follows) of the waves faded like
// their normals (water_wave_fade), so distant water flattens toward 0. 0 without waves.
fn water_waves_height(index: u32, xz: vec2f, dx: vec2f, dy: vec2f) -> f32 {
    let s = water_wave_sets[min(index, WATER_WAVE_SETS - 1u)];
    var height = 0.0;
    var total = 0.0;
    for (var i = 0u; i < 4u; i++) {
        let w = s.waves[i];
        let k = w.a.z;
        if (k <= 0.0 || w.b.x <= 0.0) {
            continue;
        }
        let amplitude = w.b.x / k;
        let theta = k * dot(w.a.xy, xz) - w.a.w;
        height += amplitude * water_wave_fade(w, dx, dy) * sin(theta);
        total += amplitude;
    }
    return select(0.0, clamp(height / total, -1.0, 1.0), total > 0.0);
}

// Weight of a wave of the wave table at a fragment of world XZ derivatives dx and dy: 1 where its
// wavelength is at least 4 pixels along the screen axes, fading to 0 at 2 pixels.
fn water_wave_fade(w: WaterWave, dx: vec2f, dy: vec2f) -> f32 {
    let per_pixel = w.a.z * max(abs(dot(w.a.xy, dx)), abs(dot(w.a.xy, dy)));
    return smoothstep(2.0, 4.0, 6.2831853 / max(per_pixel, 1e-6));
}

// Slope variance of a Water fragment under the travelling waves of a set of the wave table that
// filtering took away: for every wave the variance of its slope, (amplitude times k)^2 / 2,
// times how much it faded (water_wave_fade), plus the variance the normal map lost (mapped),
// scaled by the square of the detail that scales its slopes. Close water, whose waves are all
// resolved, adds nothing for the waves.
fn water_waves_variance(index: u32, dx: vec2f, dy: vec2f, mapped: f32) -> f32 {
    let s = water_wave_sets[min(index, WATER_WAVE_SETS - 1u)];
    return mapped * s.detail * s.detail + water_waves_faded(index, dx, dy);
}

// Slope variance the fading of the travelling waves of a set of the wave table took away, the
// part of water_waves_variance without the normal map.
fn water_waves_faded(index: u32, dx: vec2f, dy: vec2f) -> f32 {
    let s = water_wave_sets[min(index, WATER_WAVE_SETS - 1u)];
    var variance = 0.0;
    for (var i = 0u; i < 4u; i++) {
        let w = s.waves[i];
        if (w.a.z <= 0.0 || w.b.x <= 0.0) {
            continue;
        }
        variance += 0.5 * w.b.x * w.b.x * (1.0 - water_wave_fade(w, dx, dy));
    }
    return variance;
}

// Height of a cell of the window of the field of WaterRipples, 0 outside the window.
fn water_ripple_height(cell: vec2i) -> f32 {
    let r = view.ripple_c.x;
    if (any(cell < vec2i(0)) || any(cell >= vec2i(i32(r)))) {
        return 0.0;
    }
    return textureLoad(water_ripple_heights, (vec2u(cell) + view.ripple_c.yz) % r).r;
}

// Change of the height of the field across the two neighbours of a cell along X and Z (central
// differences, in world units per two cells).
fn water_ripple_difference(cell: vec2i) -> vec2f {
    return vec2f(
        water_ripple_height(cell + vec2i(1, 0)) - water_ripple_height(cell - vec2i(1, 0)),
        water_ripple_height(cell + vec2i(0, 1)) - water_ripple_height(cell - vec2i(0, 1)));
}

// The normal of a Water fragment with the ripples of the field of WaterRipples of the view: the
// slope of the field at its world XZ (bilinear between the central differences of the four
// nearest cells) tilts a normal that is whiteout blended over n, scaled by the strength of the
// field and faded out toward its border. Fragments outside the window, away from the height of
// the Water of the field or in a view without a field keep n.
fn water_ripples_normal(world: vec3f, n: vec3f) -> vec3f {
    if (view.ripple_c.w == 0u || abs(world.y - view.ripple_b.x) > view.ripple_b.y) {
        return n;
    }
    let r = f32(view.ripple_c.x);
    let p = (world.xz - view.ripple_a.xy) * view.ripple_a.z - 0.5;
    if (any(p < vec2f(0.0)) || any(p > vec2f(r - 1.0))) {
        return n;
    }
    let base = vec2i(floor(p));
    let t = p - floor(p);
    let d = mix(
        mix(water_ripple_difference(base), water_ripple_difference(base + vec2i(1, 0)), t.x),
        mix(water_ripple_difference(base + vec2i(0, 1)),
            water_ripple_difference(base + vec2i(1, 1)), t.x),
        t.y);
    let edge = min(min(p.x, p.y), min(r - 1.0 - p.x, r - 1.0 - p.y));
    let fade = smoothstep(0.0, max(view.ripple_b.z, 1.0), edge);
    let tilt = d * (0.5 * view.ripple_a.z * view.ripple_a.w * fade);
    let ripple = normalize(vec3f(-tilt.x, 1.0, -tilt.y));
    return normalize(vec3f(n.x + ripple.x, n.y * ripple.y, n.z + ripple.z));
}

// Foam of a cell of the window of the field of WaterRipples, 0 outside the window.
fn water_ripple_foam_cell(cell: vec2i) -> f32 {
    let r = view.ripple_c.x;
    if (any(cell < vec2i(0)) || any(cell >= vec2i(i32(r)))) {
        return 0.0;
    }
    return textureLoad(water_ripple_foam, (vec2u(cell) + view.ripple_c.yz) % r).r;
}

// The foam of the field of WaterRipples of the view at a Water fragment: bilinear between the
// four nearest cells, faded out toward the border of the window like the normals. Fragments
// outside the window, away from the height of the Water of the field or in a view without a
// field have none.
fn water_ripples_foam(world: vec3f) -> f32 {
    if (view.ripple_c.w == 0u || abs(world.y - view.ripple_b.x) > view.ripple_b.y) {
        return 0.0;
    }
    let r = f32(view.ripple_c.x);
    let p = (world.xz - view.ripple_a.xy) * view.ripple_a.z - 0.5;
    if (any(p < vec2f(0.0)) || any(p > vec2f(r - 1.0))) {
        return 0.0;
    }
    let base = vec2i(floor(p));
    let t = p - floor(p);
    let foam = mix(
        mix(water_ripple_foam_cell(base), water_ripple_foam_cell(base + vec2i(1, 0)), t.x),
        mix(water_ripple_foam_cell(base + vec2i(0, 1)),
            water_ripple_foam_cell(base + vec2i(1, 1)), t.x),
        t.y);
    let edge = min(min(p.x, p.y), min(r - 1.0 - p.x, r - 1.0 - p.y));
    return saturate(foam) * smoothstep(0.0, max(view.ripple_b.z, 1.0), edge);
}

// Foam of a Water fragment: the shore band of water_foam, the foam of the field and that of the
// shore waves (water_shore_foam), the larger covering, cut by the foam texture like the band
// alone. The field covers the square root of its foam, so thin foam still shows as lace the
// texture breaks up instead of being cut away whole. Without field foam and shore waves it is
// water_foam.
fn water_foam_mixed(d: f32, foam_width: f32, deep: bool, textured: bool, tex: f32,
    field: f32, shore: f32) -> f32 {
    if (field <= 0.0 && shore <= 0.0) {
        return water_foam(d, foam_width, deep, textured, tex);
    }
    let band = max(max(water_foam(d, foam_width, deep, false, 1.0), sqrt(max(field, 0.0))),
        shore);
    if (textured) {
        let t = 1.0 - band;
        return smoothstep(t, t + 0.1, tex);
    }
    return band;
}

// Scale and rotation of the second sample of the foam texture relative to the first, and its
// offset in repeats of the texture: finer, turned by about 37 degrees, so the two do not
// repeat together.
const WATER_FOAM_SCALE2: f32 = 1.63;
const WATER_FOAM_ROTATION2: mat2x2f = mat2x2f(0.8, 0.6, -0.6, 0.8);
const WATER_FOAM_OFFSET2: vec2f = vec2f(0.37, 0.71);

// Red of the foam texture of a Water at UVs: the sample at foam_scale times them combined with a
// second one at a different scale and rotation (WATER_FOAM_SCALE2, WATER_FOAM_ROTATION2): their
// mean, its contrast raised back so the lace keeps its coverage.
fn water_foam_sample(uv: vec2f, dx: vec2f, dy: vec2f, scale: f32) -> f32 {
    let r = WATER_FOAM_ROTATION2 * WATER_FOAM_SCALE2;
    let a = textureSampleGrad(foam_map, material_sampler, uv * scale, dx * scale, dy * scale).r;
    let b = textureSampleGrad(foam_map, material_sampler, r * (uv * scale) + WATER_FOAM_OFFSET2,
        r * (dx * scale), r * (dy * scale)).r;
    return saturate(0.5 + (0.5 * (a + b) - 0.5) * 1.4);
}

// Red of the foam texture of a Water at foam_scale times the UVs (water_foam_sample), 1 without
// one. With WaterFlow.textures it moves with the flow (water_flow_map). The record is flat, so
// the implicit derivatives hold.
@diagnostic(off, derivative_uniformity)
fn water_foam_texture(f: VertexOut, m: Material) -> f32 {
    let uv = water_uv(f);
    let flow = water_flow_map(f, uv);
    var tex = 1.0;
    if ((m.water_flags & WATER_FOAM_TEXTURE) != 0u) {
        let a = water_foam_sample(uv - flow.a, flow.dx, flow.dy, m.water_foam_scale);
        if (flow.weight < 1.0) {
            let b = water_foam_sample(uv - flow.b, flow.dx, flow.dy, m.water_foam_scale);
            tex = mix(b, a, flow.weight);
        } else {
            tex = a;
        }
    }
    return tex;
}

// What lies behind a Water fragment at a point of the screen in the depth copy of the opaque
// snapshot: its world position, and whether anything is there (the sky has a depth of 0).
struct WaterBehind {
    world: vec3f,
    depth: f32,
    found: bool,
};

fn water_depth(screen_uv: vec2f) -> f32 {
    let size = vec2i(textureDimensions(opaque_snapshot_depth));
    return textureLoad(opaque_snapshot_depth, clamp(vec2i(screen_uv * vec2f(size)), vec2i(0),
        size - 1), 0);
}

fn water_behind(screen_uv: vec2f) -> WaterBehind {
    let depth = water_depth(screen_uv);
    var o: WaterBehind;
    o.depth = depth;
    o.found = depth > 0.0;
    o.world = vec3f(0.0);
    if (o.found) {
        let h = view.inv_view_proj * vec4f(screen_uv.x * 2.0 - 1.0, 1.0 - screen_uv.y * 2.0,
            depth, 1.0);
        o.world = h.xyz / h.w;
    }
    return o;
}

// Whether the depth copy shows something in front of a Water fragment of a depth at a point of
// the screen or a radius around it, the footprint of the blurred snapshot sample there.
fn water_in_front(screen_uv: vec2f, radius: vec2f, depth: f32) -> bool {
    let nearest = max(max(water_depth(screen_uv),
        max(water_depth(screen_uv + vec2f(radius.x, 0.0)),
            water_depth(screen_uv - vec2f(radius.x, 0.0)))),
        max(water_depth(screen_uv + vec2f(0.0, radius.y)),
            water_depth(screen_uv - vec2f(0.0, radius.y))));
    return nearest > depth;
}

// Cells after which the first layer of the pattern of WaterCaustics repeats
// (BOX_RENDER_CAUSTICS_PERIOD), and the second, CAUSTICS_RATIO as fine: the offset of the water
// frame wraps at CAUSTICS_PERIOD, a whole number of periods of both.
const CAUSTICS_PERIOD: i32 = 64;
const CAUSTICS_PERIOD_FINE: i32 = 88;
const CAUSTICS_RATIO: f32 = 1.375;

// Width in cells of the bright lines of WaterCaustics just below the surface; they widen to
// (1 + CAUSTICS_SPREAD) times it at their depth.
const CAUSTICS_WIDTH: f32 = 0.08;
const CAUSTICS_SPREAD: f32 = 3.0;

// Cells the swirl of WaterCaustics moves a point by at most, which curves their lines.
const CAUSTICS_WARP: f32 = 0.2;

// Brightness of a line of WaterCaustics of CAUSTICS_WIDTH over the floor, per layer.
const CAUSTICS_GAIN: f32 = 1.4;

// Indices of refraction of water for red, green and blue: the colors of the caustics land
// slightly apart.
const CAUSTICS_IOR: vec3f = vec3f(1.329, 1.333, 1.338);

// Feature point of a cell of a layer of WaterCaustics at a phase, in cell units from the corner
// of the cell: hashed from the cell wrapped to the period of the layer, circling a hashed center
// a whole number of times (2 to 5, either way) per phase, so the layer repeats when the phase
// or the offset of its frame wraps.
fn caustics_point(cell: vec2i, period: i32, phase: f32) -> vec2f {
    let c = ((cell % vec2i(period)) + vec2i(period)) % vec2i(period);
    let h = distortion_hash(vec3i(c, period));
    let r = distortion_rehash(h);
    let a = unpack4x8unorm(h);
    let turns = f32(2u + (r & 3u)) * select(1.0, -1.0, (r & 4u) != 0u);
    let angle = 6.283185307 * (a.z + turns * phase);
    return vec2f(0.2) + a.xy * 0.6 + 0.25 * vec2f(cos(angle), sin(angle));
}

// Point in cells of a layer of WaterCaustics moved by a small swirl, so its lines curve: two
// waves of about 2.5 cells across the axes, a whole number of them per period of the layer and
// turning a whole number of times per phase, so the layer still repeats.
fn caustics_warp(p: vec2f, period: i32, phase: f32) -> vec2f {
    let k = 6.283185307 / f32(period);
    let waves = round(f32(period) * 0.4);
    let a = 6.283185307 * phase;
    return p + CAUSTICS_WARP * vec2f(sin(k * waves * p.y + 3.0 * a),
        sin(k * (waves - 3.0) * p.x - 2.0 * a));
}

// Bright lines of a layer of WaterCaustics at a point in cells: the edges between the cells of
// its moving feature points (the distance to the second nearest minus that to the nearest), 1 on
// an edge falling to 0 at width, squared.
fn caustics_layer(p: vec2f, period: i32, phase: f32, width: f32) -> f32 {
    let w = caustics_warp(p, period, phase);
    let base = floor(w);
    let x = w - base;
    let cell = vec2i(base);
    var f1 = 64.0;
    var f2 = 64.0;
    for (var k = 0; k < 9; k++) {
        let o = vec2i(k % 3 - 1, k / 3 - 1);
        let q = vec2f(o) + caustics_point(cell + o, period, phase) - x;
        let dd = dot(q, q);
        if (dd < f1) {
            f2 = f1;
            f1 = dd;
        } else if (dd < f2) {
            f2 = dd;
        }
    }
    let line = saturate(1.0 - (sqrt(f2) - sqrt(f1)) / width);
    return line * line;
}

// Pattern of WaterCaustics at a point in cells, a mean of about 1: the lines of two layers of
// different scales over a floor darkened by their mean (about 0.65 width each). Wider lines dim
// by CAUSTICS_WIDTH / width, so the light they gather stays the same as they blur.
fn caustics_pattern(p: vec2f, phase: f32, width: f32) -> f32 {
    let a = caustics_layer(p, CAUSTICS_PERIOD, phase, width);
    let b = caustics_layer(p * CAUSTICS_RATIO + vec2f(17.3, 41.7), CAUSTICS_PERIOD_FINE, phase,
        width);
    return max(1.0 + CAUSTICS_GAIN * CAUSTICS_WIDTH / width * (a + b - 1.3 * width), 0.0);
}

// Point in the cells of the frame of WaterCaustics whose sunlight reaches a world position at a
// water depth d below a surface of normal n: the position moved up along the direction toward
// the light l refracted by an index of refraction, by the depth.
fn caustics_entry(world: vec3f, d: f32, n: vec3f, l: vec3f, ior: f32, c: WaterCausticsOut)
    -> vec2f
{
    let t = refract(-l, n, 1.0 / ior);
    let entry = world - t * (d / max(-dot(t, n), 1e-3));
    return entry.xz * c.frequency - c.offset;
}

// Factor per channel of the scene seen through a Water with WaterCaustics, at the world position
// of what lies behind at a water depth d along the geometric normal n:
// 1 + strength * (c - 1) * k, with c the pattern sampled where the sunlight entered (red, green
// and blue a little apart), its lines widened and dimmed and its contrast faded with d / depth,
// and k the shadow (and cookie) of the first directional light there times how far it is above
// the horizon. The lines also widen to 1.5 pixels and fade where a cell is smaller than about a
// pixel. 1 without caustics, a directional light, at the depth or deeper.
fn water_caustics(f: VertexOut, world: vec3f, d: f32, n: vec3f) -> vec3f {
    let c = vo_water_caustics(f);
    if (c.strength <= 0.0 || view.lights.x == 0u || d >= c.depth) {
        return vec3f(1.0);
    }

    let l = normalize(lights[0].position);
    let above = smoothstep(0.0, 0.2, dot(l, n));
    if (above <= 0.0) {
        return vec3f(1.0);
    }

    var k = above * shadow_factor(world, n);
    if (scene_cookies) {
        k *= light_cookie(world, l, n);
    }
    if (k <= 0.0) {
        return vec3f(1.0);
    }

    let t = saturate(d / c.depth);
    let toward = world - view.camera.xyz;
    var eye = 1.0;
    if (view.lights.w == 0u) {
        eye = max(length(toward), 1e-4);
    }
    let grazing = max(abs(dot(normalize(toward), n)), 0.25);
    let pixel = eye * view.cluster.y * 2.0 * view.viewport.w / grazing * c.frequency;
    let width = CAUSTICS_WIDTH * (1.0 + CAUSTICS_SPREAD * t) + 1.5 * pixel;
    let fade = (1.0 - smoothstep(0.0, 1.0, t)) * saturate(2.0 - 2.0 * pixel);
    if (fade <= 0.0) {
        return vec3f(1.0);
    }

    let g = caustics_entry(world, d, n, l, CAUSTICS_IOR.y, c);
    let green = caustics_pattern(g, c.phase, width);
    var pattern = vec3f(green);
    let r = caustics_entry(world, d, n, l, CAUSTICS_IOR.x, c);
    let b = caustics_entry(world, d, n, l, CAUSTICS_IOR.z, c);
    if (length(r - b) > 0.02 * width) {
        pattern = vec3f(caustics_pattern(r, c.phase, width), green,
            caustics_pattern(b, c.phase, width));
    }

    return max(vec3f(1.0) + c.strength * (pattern - vec3f(1.0)) * (fade * k), vec3f(0.0));
}

// Color of a Water fragment seen from above. The scene behind, the opaque snapshot at the screen
// point moved by refraction times the view space tilt of the normal map (back at the point
// itself where the moved one shows something in front of the water), blurred by the roughness,
// shows through exp(-3 ray / visibility) of the body, tinted per channel by the absorption to
// the power of ray / visibility; the body, shallow_color mixed toward
// deep_color by the water depth along the geometric normal, covers the rest. The foam band
// replaces both, and so does the foam of the field of WaterRipples and of the waves rolling in
// at the shore, drawn the same way (the larger covers, see water_foam_mixed). The
// WaterCaustics of the material light the scene behind before it is tinted (water_caustics). Body and foam are lit diffusely with the
// light of the surface; the specular of an F0 of 0.02, direct and indirect (indirect_lobes, or the planar reflection of its entity,
// as every shape), adds on top. The scatter of the material, the sunlight scattered inside the
// body (water_scatter_light) at the height of its waves (water_waves_height, 0 without), adds to
// the body and is covered like it. Nothing
// behind (the sky) is infinitely deep. It writes no depth and takes no ambient occlusion.
fn water_lit(f: VertexOut, shaded: Surface, height: f32) -> vec4f {
    range_fragment(f);
    planet_fragment(f);
    let m = vo_water(f);
    var s = shaded;
    s.metallic = 0.0;
    s.back_factor = 0.0;
    s.back_color = vec3f(0.0);
    s.coat = 0.0;
    s.sheen = vec3f(0.0);
    s.anisotropy = 0.0;
    s.albedo = vec3f(1.0);
    let v = normalize(view.camera.xyz - f.world);
    let reflected = shade_split(s, v, f.world, planar_lookup(f));

    var ap = vec4f(0.0, 0.0, 0.0, 1.0);
    if (scene_aerial && view.aerial.w > 0.0) {
        ap = aerial_sample(f.pos, f.world);
    }

    // The tilt of the normal map in view space moves the screen point, by a fraction of the
    // image height on both axes.
    let pixel_uv = f.pos.xy * view.viewport.zw;
    let tilt = (view.view * vec4f(s.normal - s.geometric, 0.0)).xy;
    let aspect = view.viewport.y * view.viewport.z;
    var screen_uv = clamp(pixel_uv + m.water_refraction * vec2f(tilt.x * aspect, -tilt.y),
        vec2f(0.0), vec2f(1.0));
    let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);
    let lod = s.roughness * max_mip;
    var behind = water_behind(screen_uv);
    if (water_in_front(screen_uv, exp2(lod + 1.0) * view.viewport.zw, f.pos.z)) {
        screen_uv = pixel_uv;
        behind = water_behind(screen_uv);
    }

    let deep = !behind.found;
    var d = 0.0;
    var ray = 0.0;
    if (!deep) {
        d = max(dot(f.world - behind.world, s.geometric), 0.0);
        ray = length(behind.world - f.world);
    }

    var background = textureSampleLevel(opaque_snapshot, reflection_sampler, screen_uv,
        lod).rgb;
    if (!deep) {
        background *= water_caustics(f, behind.world, d, s.geometric);
    }

    let shallow = material_linear(unpack4x8unorm(m.water_shallow).rgb);
    let deep_color = material_linear(unpack4x8unorm(m.water_deep).rgb);
    let foam_color = material_linear(unpack4x8unorm(m.water_foam).rgb);
    let body = mix(shallow, deep_color, water_depth_mix(d, m.water_depth, deep)) *
        reflected.diffuse;
    let through = water_transmittance(ray, m.water_visibility, deep);
    let foam = water_foam_mixed(d, m.water_foam_width, deep,
        (m.water_flags & WATER_FOAM_TEXTURE) != 0u, water_foam_texture(f, m),
        water_ripples_foam(f.world), water_shore_foam(f.world, m, d, deep));

    let ndv = saturate(dot(s.normal, v));
    let fresnel = fresnel_schlick(ndv, vec3f(WATER_F0)).x;
    let tint = water_absorbed(water_absorption(m), ray, m.water_visibility, deep);
    let seen = background * tint * (1.0 - fresnel) * through * (1.0 - foam);
    var glow = body;
    let scatter = water_scatter(m);
    if (scatter > 0.0 && view.lights.z != 0u) {
        let scatter_color = material_linear(unpack4x8unorm(water_scatter_color(m)).rgb);
        glow += scatter_color * (scatter * water_scatter_light(f.world, s, v, height,
            reflected.diffuse));
    }
    let own = glow * (1.0 - through) * (1.0 - foam) + foam_color * reflected.diffuse * foam +
        reflected.specular;
    let veil = 1.0 - through * (1.0 - foam);
    return vec4f(own * ap.a + ap.rgb * veil + seen, 1.0);
}

// Weights of the scatter of a Water (water_scatter_light): the crests seen against the sun, the
// faces turned to the viewer in the sunlight and the ambient light.
const WATER_SCATTER_CREST: f32 = 1.0;
const WATER_SCATTER_FACE: f32 = 0.001;
const WATER_SCATTER_AMBIENT: f32 = 0.006;

// Sunlight scattered inside the body of a Water toward the viewer, before its color and
// strength: the light of the first directional light, times its shadow (and cookie) at the
// fragment, times WATER_SCATTER_CREST max(height, 0) saturate(v . -l)^4
// saturate(0.5 - 0.5 l . n)^3, which glows through the crests seen against the sun where they
// turn away from it, plus WATER_SCATTER_FACE saturate(v . n)^2 on the faces turned to the
// viewer, and WATER_SCATTER_AMBIENT saturate(v . n)^2 of the diffuse light of the surface
// (ambient). height is that of the waves, -1..1 (water_waves_height).
fn water_scatter_light(world: vec3f, s: Surface, v: vec3f, height: f32, ambient: vec3f)
    -> vec3f
{
    let ndv = saturate(dot(s.normal, v));
    var light = ambient * (WATER_SCATTER_AMBIENT * ndv * ndv);
    if (view.lights.x == 0u) {
        return light;
    }

    let l = normalize(lights[0].position);
    var visible = shadow_factor(world, s.geometric);
    if (scene_cookies) {
        visible *= light_cookie(world, l, s.geometric);
    }
    let toward = pow(saturate(dot(v, -l)), 4.0);
    let away = pow(saturate(0.5 - 0.5 * dot(l, s.normal)), 3.0);
    let k = WATER_SCATTER_CREST * max(height, 0.0) * toward * away +
        WATER_SCATTER_FACE * ndv * ndv;
    return light + lights[0].color * (visible * k);
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
    range_fragment(f);
    planet_fragment(f);
    var s = shaded;
    s.occlusion *= vo_occlusion(f);
    if (!transmissive && kind.blend_mode == 0u) {
        s.occlusion = min(s.occlusion, view_occlusion(f.pos));
    }

    let v = normalize(view.camera.xyz - f.world);
    if (material_fresnel) {
        s = fresnel_rim(f, s, v);
    }
    let reflected = shade_split(s, v, f.world, planar_lookup(f));
    if (transmissive) {
        return transmission(f, s, v, reflected);
    }

    var color = reflected.diffuse + reflected.specular;
    if (scene_aerial && view.aerial.w > 0.0) {
        color = aerial_perspective(color, s.alpha, f.pos, f.world);
    }

    if (view.fog_grid.w > 0.0) {
        color = fog_blended(color, s.alpha, f.pos, f.world);
    }

    return output(color, s.alpha);
}

@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    if (water) {
        let geometric = normalize(f.normal);
        let dx = dpdx(f.world.xz);
        let dy = dpdy(f.world.xz);
        let w = water_layers(f, geometric, vo_water(f));
        var layers = w.layers;
        var variance = w.variance;
        let waves = vo_water_waves(f);
        var height = 0.0;
        if (vo_water_spectrum(f) && view.spectrum_c.x != 0u) {
            // The slopes of the spectrum add to those of the waves, the normal map blends over
            // both by the detail of the spectrum
            let detail = view.spectrum_a.z;
            let spectrum = water_spectrum_sample(f.world.xz, dx, dy);
            var slope = vec3f(spectrum.slope, 0.0);
            variance = variance * detail * detail + spectrum.variance;
            height = spectrum.height;
            if (waves != 0u) {
                slope += water_waves_slope(waves, f.world.xz, dx, dy);
                variance += water_waves_faded(waves, dx, dy);
                height += water_waves_height(waves, f.world.xz, dx, dy);
            }
            layers.normal = water_slope_normal(slope, layers.normal, detail);
            height = clamp(height, -1.0, 1.0);
        } else if (waves != 0u) {
            variance = water_waves_variance(waves, dx, dy, variance);
            layers.normal = water_waves_normal(waves, f.world.xz, dx, dy, layers.normal);
            height = water_waves_height(waves, f.world.xz, dx, dy);
        }
        layers.normal = water_ripples_normal(f.world, layers.normal);
        var s = surface_of(f, front, geometric, layers);
        s.roughness = water_roughness(s.roughness, variance);
        return water_lit(f, s, height);
    }

    return lit(f, surface(f, front));
}

@fragment fn fs_cutout(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let s = surface(f, front);
    if (cut_out(f, s.alpha)) {
        discard;
    }

    return lit(f, s);
}

// A terrain tile with a bake atlas shades from its bake alone where a bake texel is no smaller
// than a pixel, in full and faded into the bake below that, and lit by the sun and the ambient
// occlusion of its heights either way (terrain_baked_mix); one surface_of and one lit serve all.
@fragment fn fs_blend(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let geometric = normalize(f.normal);
    let far = terrain_bake_far(f);
    let alone = far >= 1.0 && terrain_bake_light(f).w != 0.0;
    var layers: Layers;
    var baked: TerrainBaked;
    if (alone) {
        baked = terrain_bake_material(f);
        layers = terrain_baked_layers(baked);
    } else if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        layers = blend_layers(f, geometric, front);
    }
    var s = surface_of(f, front, geometric, layers);
    if (alone && front) {
        s = terrain_baked_apply(s, baked);
    }
    return lit(f, terrain_baked_mix(s, f, front, geometric, select(far, 0.0, alone)));
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
        if (cut_out(f, f.color.a * albedo.a)) {
            discard;
        }
        layers = blend_maps(geometric, b, albedo, front);
    }

    let s = surface_of(f, front, geometric, layers);
    if (cut_out(f, s.alpha)) {
        discard;
    }

    return lit(f, s);
}

#ifdef BOX_TEXTURE_LAYERS
// The surface after the TextureLayers of the batch, whose maps are slices of maps from first. A
// back face keeps its surface; the layers do not change the alpha. The diffuse transmission
// color follows the new albedo.
fn layered(f: VertexOut, front: bool, s: Surface, maps: texture_2d_array<f32>, first: u32,
    low: f32) -> Surface {
    var o = s;
    let l = apply_layers(maps, first, f, LayerSurface(s.albedo, s.metallic, s.roughness,
        s.occlusion, s.normal, s.geometric, low));
    if (front) {
        o.albedo = l.albedo;
        o.metallic = l.metallic;
        o.roughness = l.roughness;
        o.occlusion = l.occlusion;
        o.normal = l.normal;
        if (o.back_factor > 0.0) {
            o.back_color = max(vo_diffuse_transmission(f).rgb, vec3f(0.0)) * o.albedo *
                (1.0 - o.metallic) * o.back_factor;
        }
        if (o.anisotropy > 0.0) {
            o.tangent = layer_along(o.tangent, o.normal);
            o.bitangent = cross(o.normal, o.tangent);
        }
    }
    return o;
}

// fs of a Water batch with TextureLayers: the normals of the layers blend into the ripple normal
// by their coverage before the waves bend it; the other channels of the layers do not change
// Water. The coverage sees an unoccluded surface, so the cavity of a layer does nothing here.
@fragment fn fs_water_layers(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let geometric = normalize(f.normal);
    let dx = dpdx(f.world.xz);
    let dy = dpdy(f.world.xz);
    let w = water_layers(f, geometric, vo_water(f));
    var layers = w.layers;
    var variance = w.variance;
    let l = apply_layers(layer_maps, 0u, f, LayerSurface(layers.albedo.rgb, 0.0, layers.orm.g,
        1.0, layers.normal, geometric, 0.0));
    if (front) {
        layers.normal = l.normal;
    }
    let waves = vo_water_waves(f);
    var height = 0.0;
    if (waves != 0u) {
        variance = water_waves_variance(waves, dx, dy, variance);
        layers.normal = water_waves_normal(waves, f.world.xz, dx, dy, layers.normal);
        height = water_waves_height(waves, f.world.xz, dx, dy);
    }
    layers.normal = water_ripples_normal(f.world, layers.normal);
    var s = surface_of(f, front, geometric, layers);
    s.roughness = water_roughness(s.roughness, variance);
    return water_lit(f, s, height);
}

@fragment fn fs_layers(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    return lit(f, layered(f, front, surface(f, front), layer_maps, 0u, 0.0));
}

@fragment fn fs_cutout_layers(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let s = surface(f, front);
    if (cut_out(f, s.alpha)) {
        discard;
    }

    return lit(f, layered(f, front, s, layer_maps, 0u, 0.0));
}

// The surface of a fragment of a TextureBlend with TextureLayers.
fn surface_blend_layers(f: VertexOut, front: bool, geometric: vec3f) -> Surface {
    var layers: Layers;
    var low = 0.0;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        let b = blend_fragment(f);
        var albedo = vec4f(0.0);
        if (front || kind.alpha_cutout != 0u) {
            albedo = blend_fragment_albedo(b);
        }
        layers = blend_maps(geometric, b, albedo, front);
        low = blend_low(b);
    }
    let s = surface_of(f, front, geometric, layers);
    return layered(f, front, s, blend_heights, blending.count, low);
}

@fragment fn fs_blend_layers(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let geometric = normalize(f.normal);
    let far = terrain_bake_far(f);
    let alone = far >= 1.0 && terrain_bake_light(f).w != 0.0;
    var layers: Layers;
    var baked: TerrainBaked;
    var low = 0.0;
    if (alone) {
        baked = terrain_bake_material(f);
        layers = terrain_baked_layers(baked);
    } else if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        let b = blend_fragment(f);
        var albedo = vec4f(0.0);
        if (front || kind.alpha_cutout != 0u) {
            albedo = blend_fragment_albedo(b);
        }
        layers = blend_maps(geometric, b, albedo, front);
        low = blend_low(b);
    }
    var s = surface_of(f, front, geometric, layers);
    if (alone) {
        if (front) {
            s = terrain_baked_apply(s, baked);
        }
    } else {
        s = layered(f, front, s, blend_heights, blending.count, low);
    }
    return lit(f, terrain_baked_mix(s, f, front, geometric, select(far, 0.0, alone)));
}

@fragment fn fs_blend_cutout_layers(f: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4f
{
    let geometric = normalize(f.normal);
    var layers: Layers;
    var low = 0.0;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, front);
    } else {
        let b = blend_fragment(f);
        let albedo = blend_fragment_albedo(b);
        if (cut_out(f, f.color.a * albedo.a)) {
            discard;
        }
        layers = blend_maps(geometric, b, albedo, front);
        low = blend_low(b);
    }

    let s = surface_of(f, front, geometric, layers);
    if (cut_out(f, s.alpha)) {
        discard;
    }

    return lit(f, layered(f, front, s, blend_heights, blending.count, low));
}
#endif

// What a bake writes into the layers of the bake atlas of a terrain grid mesh (terrain_bake of
// mesh_blend.wgsl).
struct TerrainBakeOut {
    @location(0) albedo: vec4f,
    @location(1) normal: vec4f,
    @location(2) light: vec4f,
};

// The vertex stage of a bake: the tile placed as vs_surface places it, at the morph of its cell,
// drawn top down into the cell with the texels at the ends of a row on its edges, the vertices on
// its edges moved out by half a texel so those texels are covered.
@vertex fn vs_terrain_bake(v: VertexIn) -> VertexOut {
    let bake = terrain_bake_params;
    terrain_forced_morph = bake.cell.w;
    terrain_pad = bake.tile.w;
    var o = mesh_vertex_surface(v);
    let texels = bake.cell.z;
    let px = vec2f(0.5) + terrain_grid / (surface_data.p[1].w - 1.0) * (texels - 1.0);
    o.pos = vec4f(px.x / texels * 2.0 - 1.0, 1.0 - px.y / texels * 2.0, 0.5, 1.0);
    o.flat4.y = pack2x16float(vec2f(0.0));
    o.flat4.z = pack2x16float(select(vec2f(60000.0, 65000.0), vec2f(-2.0, -1.0),
        bake.cell.w > 0.5));
    return o;
}

// The fragment stage of a bake: the material of the texel as fs_blend_layers shades it, on the
// normal of the Catmull-Rom heights of the tile, with the light of the sun and the ambient
// occlusion the heights of the terrain let through.
@fragment fn fs_terrain_bake(f: VertexOut) -> TerrainBakeOut {
    let bake = terrain_bake_params;
    let texels = bake.cell.z;
    let rough = bake.cell.w > 0.5;
    let uv = clamp((f.pos.xy - bake.cell.xy - vec2f(0.5)) / (texels - 1.0), vec2f(0.0),
        vec2f(1.0));
    let g = uv * terrain_bake_steps();
    let geometric = terrain_smooth_normal(g, rough);
#ifdef BOX_TEXTURE_LAYERS
    let s = surface_blend_layers(f, true, geometric);
#else
    var layers: Layers;
    if (material_triplanar && vo_scalars(f).w > 1.0) {
        layers = blend_triplanar(f, geometric, true);
    } else {
        let b = blend_fragment(f);
        layers = blend_maps(geometric, b, blend_fragment_albedo(b), true);
    }
    let s = surface_of(f, true, geometric, layers);
#endif
    let h = terrain_smooth(g, rough);
    let p = bake.tile.xy + uv * bake.tile.z;
    let texel = terrain_bake_meters() / (texels - 1.0);
    var o: TerrainBakeOut;
    o.albedo = vec4f(sqrt(clamp(s.albedo, vec3f(0.0), vec3f(1.0))),
        clamp(s.occlusion, 2.0 / 255.0, 1.0));
    o.normal = vec4f(s.normal.x * 0.5 + 0.5, s.normal.z * 0.5 + 0.5, s.roughness, s.metallic);
    o.light = vec4f(terrain_bake_sun(p, h, texel), terrain_bake_ao(p, h, texel), 0.0, 1.0);
    return o;
}
