// Forward pass: draws shape meshes with a physically based surface, a GGX distribution with
// height correlated Smith visibility and Schlick Fresnel, lit by the lights of the scene; the
// first directional light is shadowed by its cascades, a spot or point light with a tile by the
// shadow atlas of the scene (spot_shadow.wgsl, point_shadow.wgsl), and the first directional
// light is scaled by its light cookie (cookie.wgsl).
// Instance normals use the cofactor matrix of the model rotation, the inverse transpose up to
// a scale, so non-uniform scale keeps them perpendicular. Texture layers (group 1) are sampled
// with the instance UV transform; the normal map uses a tangent frame from screen derivatives.
// The ORM layer holds the glTF occlusion (red, of the indirect light only), roughness (green)
// and metallic (blue) multipliers.
// The cull pass writes the slots of the visible instances, which step per instance; the
// instance data is read from the transform, shading and surface streams of the instance page of
// the batch (group 2). The vertex stage fetches the material of its instance from the material
// table by the id of its shading and mixes the base color toward the tint of the instance in
// display encoding before converting it to linear light. Indirect light comes
// from the hemisphere ambient, or for a view that samples baked lighting from the irradiance
// probe volume of its scene (group 3). The indirect specular light of a view that samples
// reflection probes comes from the prefiltered cubemaps of the probes its clusters list. A view
// lit by an environment takes its SH in place of the hemisphere ambient and its prefiltered
// cubemap as an infinitely distant probe below the scene probe. A view
// that renders the atmosphere of its scene attenuates the shaded color and adds the in scattered
// light from one trilinear tap of its aerial perspective volume.
// The occlusion of opaque and alpha tested fragments is the smaller of the ambient occlusion of
// the view (Gtao, white without one) at their pixel and the occlusion of their ORM layer; it
// scales the indirect diffuse light, and the indirect specular light by the specular occlusion
// of Lagarde that follows from it. Blended draws take the ORM occlusion alone.
// An alpha tested pipeline (alpha_cutout) discards the fragments whose alpha, the instance alpha
// times the albedo alpha, is below the cutoff of their material, once every texture is sampled.
// A lit view draws its opaque meshes depth only first (vs_depth, vs_depth_cutout and
// fs_depth_cutout), then shades them with depth compare Equal and without the alpha test.
// A transmissive pipeline (transmissive, Transmission) draws in the sorted blended stream
// without blending: it refracts the opaque snapshot of the view (opaque_snapshot), reduced by
// its Fresnel reflection, absorbed over its volume and tinted by its albedo, and adds its own
// specular light and emission on top.
// A clear coated material (ClearCoat) has a dielectric layer of f0 0.04 over its base, on the
// geometric normal and of its own roughness: a GGX lobe in direct light and a second specular
// lookup in indirect light, which take what they reflect from the base. Without a coat (strength
// 0) a fragment skips both.
// An anisotropic material (SpecularAnisotropy, KHR_materials_anisotropy) stretches the base lobe
// along a tangent of the cotangent frame of its UVs turned by its rotation: an anisotropic GGX
// distribution and Smith visibility in direct light and a reflection about a bent normal in
// indirect light. The clear coat stays isotropic. Without it (strength 0), and on a triangle whose
// UVs are degenerate, a fragment skips both.
// A material with a sheen (Sheen, KHR_materials_sheen) adds a soft rim at grazing angles over its
// base, as three.js does: a Charlie distribution with the Neubelt visibility in direct light, the
// indirect diffuse light times an analytic fit of its albedo in indirect light, and a constant
// energy compensation of the base lobes, 1 - 0.157 times the largest channel of its color. It
// lies under the clear coat. Without it (a black color) a fragment skips all of it.
// A material with diffuse transmission (DiffuseTransmission, KHR_materials_diffuse_transmission)
// adds a second Lambert lobe on the inverted normal, of its linear color times the diffuse color
// times its factor, and keeps 1 - factor of its front diffuse lobe. Direct light reaches it
// through the shadow at the position thickness behind the surface along the view ray, looked up
// with the inverted geometric normal, so a thin sheet does not shadow itself; indirect light from
// the far side reaches it without ambient occlusion. The clear coat and the sheen attenuate it as
// they do the front diffuse lobe. It stays in the opaque pass. Without it (factor 0) a fragment
// skips all of it, and every texture it reads is read without implicit derivatives.
#include "include/instance.wgsl"
#include "include/material.wgsl"
#include "include/lights.wgsl"
#include "include/attenuation.wgsl"
#include "include/shadow.wgsl"
#include "include/spot_shadow.wgsl"
#include "include/point_shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"

// BlendMode of the pipeline: the output is shaped for the blend equation it is drawn with.
override blend_mode: u32 = 0u;

// The pipeline alpha tests its fragments against the cutoff of their material (AlphaCutoff).
override alpha_cutout: bool = false;

// The pipeline draws Transmission shapes: no alpha test, no blending, no depth writes, and the
// opaque snapshot of the view shows through them.
override transmissive: bool = false;

const PI: f32 = 3.141592653589793;
const MIN_ROUGHNESS: f32 = 0.045;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;

@group(1) @binding(0) var albedo_map: texture_2d<f32>;
@group(1) @binding(1) var emissive_map: texture_2d<f32>;
@group(1) @binding(2) var orm_map: texture_2d<f32>;
@group(1) @binding(3) var normal_map: texture_2d<f32>;
@group(1) @binding(4) var material_sampler: sampler;

// Reflection probes of the scene in blend order (box_render_reflection_probe_t), the cubemap
// array with a slot per probe, mip k of N prefiltered for the perceptual roughness k / (N - 1)
// (bake_sh.wgsl), and the L2 SH irradiance of each slot, as a probe of the volume holds it.
@group(0) @binding(5) var<storage, read> reflection_probes: array<ReflectionProbe>;
@group(0) @binding(6) var reflection_cubes: texture_cube_array<f32>;
@group(0) @binding(7) var reflection_sampler: sampler;
@group(0) @binding(8) var<storage, read> reflection_sh: array<vec4f>;

// Environment of the view (LightSettings.environment): a cubemap whose mip k of N is
// prefiltered for the perceptual roughness k / (N - 1), as a reflection probe slot, and its L2
// SH irradiance (ambient.wgsl). Views without one bind the black reflection slot and a zero SH,
// and their view.environment.w is 0.
@group(0) @binding(12) var environment_cube: texture_cube<f32>;

// Ambient occlusion of the view (Gtao) at the render size, read at the pixel of a fragment; a
// view without Gtao, and the probe and reflection captures, bind a 1x1 white image.
@group(0) @binding(14) var gtao_image: texture_2d<f32>;

// Opaque snapshot of the view for transmission (gpu_transmission.c): its image before the
// blended draws and a blur pyramid, mip k + 1 downsampled from mip k. A view without
// transmissive draws, and the captures, bind a 1x1 white image.
@group(0) @binding(19) var opaque_snapshot: texture_2d<f32>;

// Depth of the target copied with the opaque snapshot (gpu_transmission.c), reversed Z with 0 for
// the sky, which the blur of a DiffuseTransmission reads with textureLoad. A view without
// transmissive draws, and the captures, bind a 1x1 placeholder.
@group(0) @binding(20) var opaque_snapshot_depth: texture_depth_2d;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(2) var<storage, read> shadings: array<MeshShading>;
@group(2) @binding(3) var<storage, read> surfaces: array<SurfaceData>;
@group(2) @binding(4) var<storage, read> materials: array<Material>;

// Grid of the probe volume: probe (x, y, z) sits at origin.xyz + (x, y, z) * origin.w, x
// fastest in the probe buffer. size.w is 0 for the 1x1x1 volume of views that keep the
// hemisphere ambient, 1 when the view samples the probes.
struct Volume {
    origin: vec4f,
    size: vec4u,
};

// Every probe is SH_COEFFICIENTS rgb irradiance coefficients E_lm (Ramamoorthi and Hanrahan),
// in Sloan's real SH basis; cell_valid has a byte per cell of the grid, four to a word, whose
// bit x + 2y + 4z is set when that corner probe of the cell is valid: clear inside static
// geometry. A cell is a probe with a neighbour after it on every axis with more than one probe.
// probe_visibility holds an octahedral map per probe of VISIBILITY_SIZE texels on each side,
// each the mean and mean square distance to the surfaces its faces saw in probe spacings, packed
// as two halves (bake_sh.wgsl).
@group(3) @binding(0) var<uniform> volume: Volume;
@group(3) @binding(1) var<storage, read> probes: array<vec4f>;
@group(3) @binding(2) var<storage, read> cell_valid: array<u32>;
@group(3) @binding(3) var<storage, read> probe_visibility: array<u32>;

const SH_COEFFICIENTS: u32 = 9u;

// Texels per edge of the octahedral distance map of a probe.
const VISIBILITY_SIZE: i32 = 8;

// Offset along the normal of the point whose visibility from a probe is tested, in spacings.
const VISIBILITY_BIAS: f32 = 0.25;

// Smallest variance of the distance in a direction, in square spacings.
const VISIBILITY_VARIANCE: f32 = 0.001;

// Share of the weight of the probes around a point that the ambient makes up for when the
// probes the point sees carry less.
const VISIBILITY_FALLBACK: f32 = 0.1;

// Weight left over by the reflection probes of a fragment below which the next ones are
// skipped.
const REFLECTION_EPSILON: f32 = 1e-3;

// Range of the scale the irradiance grid normalizes the radiance of a reflection probe by: down
// to black for a fragment the grid sees no light at, up to twice as bright for one that is lit
// more than the capture point of the probe.
const REFLECTION_NORMALIZE_MIN: f32 = 0.0;
const REFLECTION_NORMALIZE_MAX: f32 = 2.0;

struct VertexIn {
    @builtin(vertex_index) vertex: u32,
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) slot: u32,
};

struct VertexOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) emissive: vec3f,
    @location(4) material: vec2f,
    @location(5) uv: vec2f,
    // Flat scalars of the material: the AlphaCutoff in x, the thickness of the
    // DiffuseTransmission in y and its blur in z; w is 0
    @location(6) @interpolate(flat) scalars: vec4f,
    // ClearCoat of the material: strength and roughness, 0 strength without one
    @location(10) @interpolate(flat) clear_coat: vec2f,
    // SpecularAnisotropy of the material: strength and rotation over 2 pi, 0 strength without one
    @location(11) @interpolate(flat) anisotropy: vec2f,
    // Sheen of the material: linear color and roughness, a black color without one
    @location(12) @interpolate(flat) sheen: vec4f,
    // DiffuseTransmission of the material: linear color and factor, 0 factor without one
    @location(13) @interpolate(flat) diffuse_transmission: vec4f,
    // Transmission of the material: factor, index of refraction, thickness and attenuation
    // distance, its linear attenuation color and the scale of the instance along its axes
    @location(7) @interpolate(flat) transmission: vec4f,
    @location(8) @interpolate(flat) attenuation: vec3f,
    @location(9) @interpolate(flat) model_scale: vec3f,
};

// A vertex placed in the world: the model matrix of its instance, the normal of its mesh or
// parametric surface in the frame of the instance, its world and its clip position. vs and the
// depth prepass entry points all place their vertices with it, and the clip position is
// invariant, so the forward pass of a view with a depth prepass rasterizes the depths the
// prepass wrote and tests them with Equal. It reads the transform and surface streams and the
// surface assets only, which the plain prepass binds, and sets surface_data. A plain mesh page
// binds one zero record as its surface stream, so the record of a slot is loaded only when it is
// inside the buffer, without relying on robust buffer access; surface_data stays zero otherwise,
// which is the plain mesh path.
struct Placed {
    model: mat4x4f,
    normal: vec3f,
    world: vec4f,
    pos: vec4f,
};

fn place(v: VertexIn) -> Placed {
    if (v.slot < arrayLength(&surfaces)) {
        surface_data = surfaces[v.slot];
    }
    let model = instance_model(transforms[v.slot]);
    var position = v.position;
    var normal = v.normal;
    if (surface_data.p[19].x != 0.0) {
        position = v.position;
        normal = v.normal;
    } else if (surface_data.p[0].x == 7.0) {
        position.y *= surface_data.p[1].y;
    } else if (surface_data.p[0].x == 1.0 && v.position.z < 2.0) {
        let sample = surface_sweep_sample(i32(v.position.z), v.position.x, v.position.y);
        position = sample.position;
        normal = sv_unit(sample.normal);
        if (sv_dot(sample.normal, sample.normal) < 0.000000000001) {
            normal = surface_normal(i32(v.position.z), v.position.x, v.position.y);
        }
    } else if (surface_data.p[0].x != 0.0) {
        position = surface_position(i32(v.position.z), v.position.x, v.position.y);
        normal = surface_normal(i32(v.position.z), v.position.x, v.position.y);
    }
    let world = model * vec4f(position, 1.0);
    return Placed(model, normal, world, view.view_proj * world);
}

// Color, metallic and roughness, and UV of a vertex from the shading of its instance and its
// material, with the tint and material of its region on a parametric surface. Reads the
// surface_data place set.
struct VertexShading {
    color: vec4f,
    material: vec2f,
    uv: vec2f,
};

fn vertex_shading(v: VertexIn, shading: MeshShading, material: Material) -> VertexShading {
    var o: VertexShading;
    let tint = select(mesh_shading_tint(shading), vec4f(0.0, 0.0, 0.0, 1.0),
        view.reflection.y != 0u);
    o.color = vec4f(material_color(material, tint), shading.alpha);
    o.material = vec2f(material.metallic, material.roughness);
    o.uv = v.uv * material.uv.xy + material.uv.zw;
    if (surface_data.p[0].x != 0.0) {
        var region = i32(v.position.z);
        if (surface_data.p[0].x == 7.0) {
            region = select(0, 1, v.uv.x >= 2.0);
            if (abs(v.normal.y) > 0.5) {
                region = 2;
            } else if (region == 1) {
                o.uv.x -= 2.0 * material.uv.x;
            }
        }
        var material_region = 2;
        if (region == 0 || region == 1) {
            material_region = region;
        } else if (region == 2 || region == 3 || region == 8 || region == 9) {
            material_region = 3;
        }
        if (surface_data.p[19].x != 0.0) {
            material_region = i32(surface_assets[v.vertex / 4u][v.vertex % 4u]);
        }
        let packed = bitcast<u32>(surface_data.p[21][material_region]);
        if ((packed & 0x80000000u) != 0u) {
            let tint = unpack4x8unorm(bitcast<u32>(surface_data.p[20][material_region]));
            let linear = select(pow((tint.rgb + vec3f(0.055)) / 1.055, vec3f(2.4)),
                tint.rgb / 12.92, tint.rgb <= vec3f(0.04045));
            o.color *= vec4f(linear, tint.a);
            o.material = vec2f(f32((packed >> 16u) & 32767u) / 32767.0,
                f32(packed & 65535u) / 65535.0);
        }
    }
    return o;
}

@vertex fn vs(v: VertexIn) -> VertexOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material);
    let p = place(v);
    let c0 = p.model[0].xyz;
    let c1 = p.model[1].xyz;
    let c2 = p.model[2].xyz;
    let cofactor = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let s = vertex_shading(v, shading, material);
    var o: VertexOut;
    o.pos = p.pos;
    o.world = p.world.xyz;
    o.normal = cofactor * p.normal * flip;
    o.color = s.color;
    o.emissive = material.emissive;
    o.material = s.material;
    o.scalars = vec4f(material.cutoff, material.thickness, material.diffuse_blur, 0.0);
    o.clear_coat = unpack2x16unorm(material.clear_coat);
    o.anisotropy = unpack2x16unorm(material.anisotropy);
    let sheen = unpack4x8unorm(material.sheen);
    o.sheen = vec4f(material_linear(sheen.rgb), sheen.a);
    let diffuse_transmission = unpack4x8unorm(material.diffuse_transmission);
    o.diffuse_transmission = vec4f(material_linear(diffuse_transmission.rgb),
        diffuse_transmission.a);
    o.uv = s.uv;
    o.transmission = vec4f(material.transmission_factor, material.ior,
        material.thickness_factor, material.attenuation_distance);
    o.attenuation = material_linear(unpack4x8unorm(material.attenuation_color).rgb);
    o.model_scale = vec3f(length(c0), length(c1), length(c2));
    return o;
}

// Depth prepass of a lit view (box_gpu_draw_prepass): the opaque meshes draw depth only with
// vs_depth, which binds the transform stream of the instance page without the shading stream
// (group 2) and nothing in group 1, and has no fragment stage.
struct DepthOut {
    @builtin(position) @invariant pos: vec4f,
};

@vertex fn vs_depth(v: VertexIn) -> DepthOut {
    return DepthOut(place(v).pos);
}

// The alpha tested meshes of the prepass draw with vs_depth_cutout and fs_depth_cutout, which
// also bind the shading stream, the material table and the texture layers and discard what fs
// discards: the alpha of the vertex as vs makes it times the albedo alpha, below the cutoff of
// the material. The forward pass after the prepass does not alpha test, its Equal depth test
// rejects those.
struct DepthCutoutOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) alpha: f32,
    @location(2) @interpolate(flat) cutoff: f32,
};

@vertex fn vs_depth_cutout(v: VertexIn) -> DepthCutoutOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material);
    let p = place(v);
    let s = vertex_shading(v, shading, material);
    return DepthCutoutOut(p.pos, s.uv, s.color.a, material.cutoff);
}

@fragment fn fs_depth_cutout(f: DepthCutoutOut) {
    let alpha = f.alpha * textureSample(albedo_map, material_sampler, f.uv).a;
    if (alpha < f.cutoff) {
        discard;
    }
}

// Indirect light of a fragment, irradiance over pi: in the direction of its normal for the
// diffuse term and in the direction its specular term samples.
struct Indirect {
    diffuse: vec3f,
    specular: vec3f,
};

// Point of the octahedral map of a direction, in [0, 1] on each axis.
fn octahedral_uv(d: vec3f) -> vec2f {
    var p = d.xy / (abs(d.x) + abs(d.y) + abs(d.z));
    if (d.z < 0.0) {
        p = (1.0 - abs(p.yx)) * select(vec2f(-1.0), vec2f(1.0), p >= vec2f(0.0));
    }

    return p * 0.5 + 0.5;
}

// Distance moments of a texel of the map of a probe. A texel past an edge of the map is the
// texel mirrored across that edge, which the octahedral fold makes its neighbour.
fn visibility_moments(index: u32, x: i32, y: i32) -> vec2f {
    var tx = x;
    var ty = y;
    if (tx < 0 || tx >= VISIBILITY_SIZE) {
        tx = clamp(tx, 0, VISIBILITY_SIZE - 1);
        ty = VISIBILITY_SIZE - 1 - ty;
    }

    if (ty < 0 || ty >= VISIBILITY_SIZE) {
        ty = clamp(ty, 0, VISIBILITY_SIZE - 1);
        tx = VISIBILITY_SIZE - 1 - tx;
    }

    let texels = u32(VISIBILITY_SIZE * VISIBILITY_SIZE);
    return unpack2x16float(probe_visibility[index * texels + u32(tx + ty * VISIBILITY_SIZE)]);
}

// Chance that a probe sees a point, from the offset of the point from the probe: one up to the
// mean distance its faces saw in that direction, then Chebyshev's bound from the variance,
// cubed, as DDGI weighs its probes. The four texels around the point are read directly unless
// one of them lies past an edge of the map.
fn visibility_from(index: u32, offset: vec3f) -> f32 {
    let len = length(offset);
    let dist = len / volume.origin.w;
    let t = octahedral_uv(offset / max(len, 1e-6)) * f32(VISIBILITY_SIZE) - 0.5;
    let i = vec2i(floor(t));
    let f = t - floor(t);
    var m00: vec2f;
    var m10: vec2f;
    var m01: vec2f;
    var m11: vec2f;
    if (all(i >= vec2i(0)) && all(i < vec2i(VISIBILITY_SIZE - 1))) {
        let row = u32(VISIBILITY_SIZE);
        let at = index * row * row + u32(i.x) + u32(i.y) * row;
        m00 = unpack2x16float(probe_visibility[at]);
        m10 = unpack2x16float(probe_visibility[at + 1u]);
        m01 = unpack2x16float(probe_visibility[at + row]);
        m11 = unpack2x16float(probe_visibility[at + row + 1u]);
    } else {
        m00 = visibility_moments(index, i.x, i.y);
        m10 = visibility_moments(index, i.x + 1, i.y);
        m01 = visibility_moments(index, i.x, i.y + 1);
        m11 = visibility_moments(index, i.x + 1, i.y + 1);
    }
    let low = mix(m00, m10, f.x);
    let high = mix(m01, m11, f.x);
    let m = mix(low, high, f.y);
    if (len <= 1e-6 || dist <= m.x) {
        return 1.0;
    }

    let variance = max(m.y - m.x * m.x, VISIBILITY_VARIANCE);
    let delta = dist - m.x;
    let p = variance / (variance + delta * delta);
    return p * p * p;
}

// Irradiance at a position with a normal, blended from the 8 probes around it: trilinear
// weights, times the wrap of the direction toward the probe around the normal as in DDGI, so
// probes behind the surface count less, times the visibility of the position, moved off the
// surface along the normal, from the probe. Invalid probes are skipped; outside the grid the
// position clamps to the border probes. Without a valid probe it is the ambient (the SH of the
// environment of the view, else the hemisphere), which also makes up the weight the visible
// probes lack below VISIBILITY_FALLBACK of the weight of every valid probe, so a surface that
// sees none of its probes does not take the light of the probes behind it. The SH evaluation is
// linear in the coefficients, so every probe
// is evaluated in the two directions and the results are blended; the hemisphere ambient is
// the irradiance its SH projection evaluates to.
fn volume_irradiance(world: vec3f, n: vec3f, spec: vec3f) -> Indirect {
    let size = volume.size.xyz;
    let g = clamp((world - volume.origin.xyz) / volume.origin.w, vec3f(0.0), vec3f(size - 1u));
    let base = min(vec3u(floor(g)), max(size, vec3u(2u)) - 2u);
    let t = clamp(g - vec3f(base), vec3f(0.0), vec3f(1.0));
    let cells = max(size, vec3u(2u)) - 1u;
    let cell = base.x + (base.y + base.z * cells.y) * cells.x;
    let corners = cell_valid[cell >> 2u] >> ((cell & 3u) * 8u);
    let biased = world + n * (VISIBILITY_BIAS * volume.origin.w);
    var diffuse = vec3f(0.0);
    var specular = vec3f(0.0);
    var total = 0.0;
    var reach = 0.0;
    for (var i = 0u; i < 8u; i++) {
        let o = vec3u(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
        let p = min(base + o, size - 1u);
        let index = p.x + (p.y + p.z * size.y) * size.x;
        let valid = (corners >> i) & 1u;
        let trilinear = select(1.0 - t, t, o == vec3u(1u));
        let position = volume.origin.xyz + vec3f(p) * volume.origin.w;
        let to_probe = position - world;
        let dist = length(to_probe);
        let dir = select(n, to_probe / max(dist, 1e-4), dist > 1e-4);
        let wrap = (dot(dir, n) + 1.0) * 0.5;
        let reached = trilinear.x * trilinear.y * trilinear.z * (wrap * wrap + 0.2) * f32(valid);
        if (reached > 0.0) {
            let w = reached * visibility_from(index, biased - position);
            let at = index * SH_COEFFICIENTS;
            let e0 = probes[at].rgb;
            let e1 = probes[at + 1u].rgb;
            let e2 = probes[at + 2u].rgb;
            let e3 = probes[at + 3u].rgb;
            let e4 = probes[at + 4u].rgb;
            let e5 = probes[at + 5u].rgb;
            let e6 = probes[at + 6u].rgb;
            let e7 = probes[at + 7u].rgb;
            let e8 = probes[at + 8u].rgb;
            diffuse += sh_irradiance(e0, e1, e2, e3, e4, e5, e6, e7, e8, n) * w;
            specular += sh_irradiance(e0, e1, e2, e3, e4, e5, e6, e7, e8, spec) * w;
            total += w;
            reach += reached;
        }
    }

    if (reach <= 0.0) {
        return Indirect(ambient(n), ambient(spec));
    }

    let fallback = max(VISIBILITY_FALLBACK * reach - total, 0.0);
    let scale = 1.0 / (PI * (total + fallback));
    diffuse = (diffuse + ambient(n) * (PI * fallback)) * scale;
    specular = (specular + ambient(spec) * (PI * fallback)) * scale;
    return Indirect(max(diffuse, vec3f(0.0)), max(specular, vec3f(0.0)));
}

// A world direction or offset in the frame of the boxes of a reflection probe.
fn probe_local(p: ReflectionProbe, v: vec3f) -> vec3f {
    return vec3f(dot(v, p.axes[0].xyz), dot(v, p.axes[1].xyz), dot(v, p.axes[2].xyz));
}

// Weight of a reflection probe at a position: 1 inside its influence box, falling linearly to 0
// over blend toward its faces and 0 outside; a blend of 0 is a hard edge.
fn probe_fade(p: ReflectionProbe, world: vec3f) -> f32 {
    let inside = p.extent - abs(probe_local(p, world - p.center));
    let d = min(min(inside.x, inside.y), inside.z);
    if (p.blend > 0.0) {
        return clamp(d / p.blend, 0.0, 1.0);
    }

    return select(0.0, 1.0, d >= 0.0);
}

// Direction a reflection probe is sampled in for a ray from a position along dir: from the
// capture point toward where the ray leaves the projection box (parallax correction), or dir
// itself when the box has no volume. A position outside the box that looks away from it uses
// the direction from the capture point to the position.
fn probe_direction(p: ReflectionProbe, world: vec3f, dir: vec3f) -> vec3f {
    if (any(p.projection <= vec3f(0.0))) {
        return dir;
    }

    let o = probe_local(p, world - p.center);
    let d = probe_local(p, dir);
    let safe = select(d, vec3f(1e-6), abs(d) < vec3f(1e-6));
    let exit = max((p.projection - o) / safe, (-p.projection - o) / safe);
    let t = max(min(min(exit.x, exit.y), exit.z), 0.0);
    let result = world + dir * t - p.capture;
    return select(dir, result, dot(result, result) > 1e-8);
}

// Irradiance over pi in a direction from the SH of the cubemap slot of a reflection probe.
fn probe_irradiance(slot: u32, d: vec3f) -> vec3f {
    let at = slot * SH_COEFFICIENTS;
    let e = sh_irradiance(reflection_sh[at].rgb, reflection_sh[at + 1u].rgb,
        reflection_sh[at + 2u].rgb, reflection_sh[at + 3u].rgb, reflection_sh[at + 4u].rgb,
        reflection_sh[at + 5u].rgb, reflection_sh[at + 6u].rgb, reflection_sh[at + 7u].rgb,
        reflection_sh[at + 8u].rgb, d);
    return max(e / PI, vec3f(0.0));
}

fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

// Prefiltered radiance a reflection probe reflects toward a position along the specular
// direction spec at a mip level. For a view that samples baked lighting it is scaled by the
// luminance of the irradiance grid at the position over that of the SH of the probe, both in
// direction spec, within REFLECTION_NORMALIZE_MIN and REFLECTION_NORMALIZE_MAX, so a dim
// alcove inside a bright probe does not reflect its bright light.
fn probe_radiance(p: ReflectionProbe, world: vec3f, spec: vec3f, mip: f32, grid: vec3f) -> vec3f {
    let d = probe_direction(p, world, normalize(spec));
    let radiance = textureSampleLevel(reflection_cubes, reflection_sampler, d, p.slot, mip).rgb;
    if (volume.size.w == 0u) {
        return radiance;
    }

    let own = luminance(probe_irradiance(p.slot, spec));
    let scale = luminance(grid) / max(own, 1e-4);
    return radiance * clamp(scale, REFLECTION_NORMALIZE_MIN, REFLECTION_NORMALIZE_MAX);
}

// Prefiltered radiance the environment of the view reflects along the specular direction spec,
// an infinitely distant probe turned by the yaw of the view, at the mip level of a perceptual
// roughness, times its intensity. For a view that samples baked lighting it is normalized as
// probe_radiance normalizes a probe, by the luminance of the irradiance grid over that of the
// SH of the environment, both in direction spec.
fn environment_radiance(spec: vec3f, roughness: f32, grid: vec3f) -> vec3f {
    let mip = roughness * f32(textureNumLevels(environment_cube) - 1u);
    let d = environment_direction(view, normalize(spec));
    let radiance = textureSampleLevel(environment_cube, reflection_sampler, d, mip).rgb *
        view.environment.x;
    if (volume.size.w == 0u) {
        return radiance;
    }

    let own = luminance(environment_irradiance(spec));
    let scale = luminance(grid) / max(own, 1e-4);
    return radiance * clamp(scale, REFLECTION_NORMALIZE_MIN, REFLECTION_NORMALIZE_MAX);
}

// Indirect specular light of a fragment from the reflection probes of its cluster, along the
// specular direction spec. The cluster lists its probes in blend order: each one takes its fade
// weight of the weight still left, until none is; the scene probe, last in the probe buffer,
// takes what is left, else the environment of the view does, else the fallback: the ambient or,
// for a view that samples baked lighting, the irradiance grid, both in direction spec. A probe
// whose slot holds no capture yet is skipped. The mip level follows the perceptual roughness as
// the prefilter wrote them.
fn reflections(world: vec3f, spec: vec3f, roughness: f32, cluster: u32, fallback: vec3f) -> vec3f {
    let mip = roughness * f32(textureNumLevels(reflection_cubes) - 1u);
    var sum = vec3f(0.0);
    var left = 1.0;
    if (view.grid.w > 0u) {
        let count = clusters[2u + 2u * cluster];
        let listed = cluster_probes(count);
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster] +
            cluster_lights(count);
        for (var i = 0u; i < listed && left > REFLECTION_EPSILON; i++) {
            let p = reflection_probes[clusters[first + i]];
            let w = probe_fade(p, world) * left * f32(p.ready);
            if (w > 0.0) {
                sum += probe_radiance(p, world, spec, mip, fallback) * w;
                left -= w;
            }
        }
    }

    if (left > REFLECTION_EPSILON) {
        let last = view.reflection.x - 1u;
        var rest = fallback;
        if (view.reflection.x > view.grid.w && reflection_probes[last].ready != 0u) {
            rest = probe_radiance(reflection_probes[last], world, spec, mip, fallback);
        } else if (view.environment.w > 0.0) {
            rest = environment_radiance(spec, roughness, fallback);
        }

        sum += rest * left;
    }

    return sum;
}

// Surface of a fragment from the material constants of its instance. Texture layers apply
// here on front faces; a plain back face keeps the constants, is unoccluded and faces the
// camera, except that an alpha tested one takes the albedo alpha, so its holes go through both
// sides. The occlusion scales the indirect light only. geometric is the interpolated vertex
// normal before the normal map, negated on back faces as normal is, which the shadow lookup
// is offset along. coat and coat_roughness are the strength and roughness of the clear coat,
// material constants that apply on both faces; the coat lies on geometric. anisotropy is the
// strength of the anisotropy of the base, a material constant that applies on both faces too,
// along tangent, perpendicular to normal, with bitangent = cross(normal, tangent); it is 0
// without one and where the UVs give no direction. sheen is the linear color of the sheen,
// black without one, and sheen_roughness its roughness clamped to 0.07..1, material constants
// that apply on both faces. back_factor is the factor of the diffuse transmission, 0 without
// one, back_color the light its back lobe passes through, its linear color times the diffuse
// color times the factor, and thickness the distance behind the surface of its shadow lookup;
// they apply on front faces and plain back faces.
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

// Tangent frame from the screen derivatives of the position and the UVs (cotangent frame), its
// tangent and bitangent scaled by the same factor, the longer of the two of unit length. The
// framebuffer y of dpdy points down, which negates both derivative axes of the frame. degenerate
// is set where the UV derivatives give no frame. It reads dpdx and dpdy, so it is called in
// uniform control flow.
struct Frame {
    tangent: vec3f,
    bitangent: vec3f,
    degenerate: bool,
};

fn cotangent_frame(n: vec3f, p: vec3f, uv: vec2f) -> Frame {
    let dp1 = dpdx(p);
    let dp2 = dpdy(p);
    let duv1 = dpdx(uv);
    let duv2 = dpdy(uv);
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

fn surface(f: VertexOut, front: bool) -> Surface {
    let albedo = textureSample(albedo_map, material_sampler, f.uv);
    let emissive = textureSample(emissive_map, material_sampler, f.uv).rgb;
    let orm = textureSample(orm_map, material_sampler, f.uv).rgb;
    let tangent_normal = textureSample(normal_map, material_sampler, f.uv).xyz * 2.0 - 1.0;
    let geometric = normalize(f.normal);
    let frame = cotangent_frame(geometric, f.world, f.uv);
    let mapped = perturb(geometric, frame, tangent_normal);
    var s: Surface;
    s.albedo = f.color.rgb;
    s.alpha = f.color.a;
    s.metallic = clamp(f.material.x, 0.0, 1.0);
    s.roughness = clamp(f.material.y, MIN_ROUGHNESS, 1.0);
    s.occlusion = 1.0;
    s.emissive = f.emissive;
    s.normal = -geometric;
    s.geometric = -geometric;
    s.coat = clamp(f.clear_coat.x, 0.0, 1.0);
    s.coat_roughness = clamp(f.clear_coat.y, MIN_ROUGHNESS, 1.0);
    s.sheen = max(f.sheen.rgb, vec3f(0.0));
    s.sheen_roughness = clamp(f.sheen.w, SHEEN_MIN_ROUGHNESS, 1.0);
    if (front) {
        s.albedo *= albedo.rgb;
        s.alpha *= albedo.a;
        s.metallic = clamp(f.material.x * orm.b, 0.0, 1.0);
        s.roughness = clamp(f.material.y * orm.g, MIN_ROUGHNESS, 1.0);
        s.occlusion = orm.r;
        s.emissive *= emissive;
        s.normal = mapped;
        s.geometric = geometric;
    } else if (alpha_cutout) {
        s.alpha *= albedo.a;
    }

    s.back_factor = clamp(f.diffuse_transmission.w, 0.0, 1.0);
    s.back_color = vec3f(0.0);
    s.thickness = 0.0;
    if (s.back_factor > 0.0) {
        s.back_color = max(f.diffuse_transmission.rgb, vec3f(0.0)) * s.albedo *
            (1.0 - s.metallic) * s.back_factor;
        s.thickness = max(f.scalars.y, 0.0);
    }

    // The direction of the anisotropy: the tangent of the frame turned by the rotation around
    // the normal, made perpendicular to the shading normal.
    s.anisotropy = 0.0;
    s.tangent = vec3f(0.0);
    s.bitangent = vec3f(0.0);
    if (f.anisotropy.x > 0.0 && !frame.degenerate) {
        let rotation = f.anisotropy.y * (2.0 * PI);
        let dir = frame.tangent * cos(rotation) + frame.bitangent * sin(rotation);
        let along = dir - s.normal * dot(dir, s.normal);
        let len2 = dot(along, along);
        if (len2 > 1e-12) {
            s.anisotropy = clamp(f.anisotropy.x, 0.0, 1.0);
            s.tangent = along * inverseSqrt(len2);
            s.bitangent = cross(s.normal, s.tangent);
        }
    }
    return s;
}

fn distribution_ggx(ndh: f32, a: f32) -> f32 {
    let a2 = a * a;
    let d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

fn visibility_smith(ndv: f32, ndl: f32, a: f32) -> f32 {
    let a2 = a * a;
    let gv = ndl * sqrt((ndv - a2 * ndv) * ndv + a2);
    let gl = ndv * sqrt((ndl - a2 * ndl) * ndl + a2);
    return 0.5 / max(gv + gl, 1e-7);
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

fn fresnel_schlick(cos_theta: f32, f0: vec3f) -> vec3f {
    let x = 1.0 - cos_theta;
    let x2 = x * x;
    return f0 + (vec3f(1.0) - f0) * (x2 * x2 * x);
}

// Analytic fit of the split sum environment BRDF (Karis), scale and bias of f0.
fn env_brdf(roughness: f32, ndv: f32) -> vec2f {
    let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
    let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
    let r = roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2f(-1.04, 1.04) * a004 + r.zw;
}

// Light reflected toward the eye, split into its diffuse and specular parts; the emission is
// part of the specular one.
struct Lit {
    diffuse: vec3f,
    specular: vec3f,
};

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
    if (s.anisotropy > 0.0) {
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
    if (any(s.sheen > vec3f(0.0))) {
        let alpha = s.sheen_roughness * s.sheen_roughness;
        let comp = sheen_compensation(s.sheen);
        let sheen_specular = s.sheen * distribution_charlie(alpha, ndh) *
            visibility_neubelt(ndv, ndl);
        lit.diffuse *= comp;
        lit.specular = lit.specular * comp + sheen_specular * radiance * ndl;
    }
    if (s.coat > 0.0) {
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
    if (any(s.sheen > vec3f(0.0))) {
        back *= sheen_compensation(s.sheen);
    }
    if (s.coat > 0.0) {
        let ndv_coat = max(dot(s.geometric, v), 1e-4);
        back *= 1.0 - fresnel_schlick(ndv_coat, vec3f(0.04)).x * s.coat;
    }
    return back;
}

// Light inputs. Every direct light the forward pass uses comes from here: the directional
// lights of the scene, the first one through its shadow map and light cookie, then the point
// and spot lights of the cluster of the fragment, a spot or point light with a tile of the
// shadow atlas through it. The back lobe of a diffuse transmission (brdf_back) adds to the
// diffuse part, with its shadow, cookie and attenuation at the position thickness behind the
// surface along the view ray, looked up with the inverted geometric normal.
fn lights_direct(s: Surface, v: vec3f, world: vec3f, cluster: u32) -> Lit {
    var sum = Lit(vec3f(0.0), vec3f(0.0));
    var shadow = shadow_factor(world, s.geometric);
    if (view.lights.x > 0u) {
        shadow *= light_cookie(world, normalize(lights[0].position), s.geometric);
    }

    let back = s.back_factor > 0.0;
    let world_back = world - v * s.thickness;
    var shadow_back = 1.0;
    if (back) {
        shadow_back = shadow_factor(world_back, -s.geometric);
        if (view.lights.x > 0u) {
            shadow_back *= light_cookie(world_back, normalize(lights[0].position), -s.geometric);
        }
    }

    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let visible = select(1.0, shadow, i == 0u);
        let lit = brdf(s, v, normalize(light.position), light.color);
        sum.diffuse += lit.diffuse * visible;
        sum.specular += lit.specular * visible;
        if (back) {
            let visible_back = select(1.0, shadow_back, i == 0u);
            sum.diffuse += brdf_back(s, v, normalize(light.position), light.color) *
                visible_back;
        }
    }

    if (view.lights.y > view.lights.x) {
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        let count = cluster_lights(clusters[2u + 2u * cluster]);
        for (var i = 0u; i < count; i++) {
            let light = lights[clusters[first + i]];
            let delta = light.position - world;
            let dist = length(delta);
            let l = delta / max(dist, 1e-4);
            var visible = 1.0;
            if (light.shadow < view.spot_config.x) {
                visible = punctual_shadow(light, world, s.geometric);
            }

            let lit = brdf(s, v, l, light.color * attenuation(light, l, dist));
            sum.diffuse += lit.diffuse * visible;
            sum.specular += lit.specular * visible;
            if (back) {
                let delta_back = light.position - world_back;
                let dist_back = length(delta_back);
                let l_back = delta_back / max(dist_back, 1e-4);
                var visible_back = 1.0;
                if (light.shadow < view.spot_config.x) {
                    visible_back = punctual_shadow(light, world_back, -s.geometric);
                }

                sum.diffuse += brdf_back(s, v, l_back,
                    light.color * attenuation(light, l_back, dist_back)) * visible_back;
            }
        }
    }

    return sum;
}

// Ambient occlusion of the view at the pixel of a fragment.
fn view_occlusion(pos: vec4f) -> f32 {
    let size = vec2i(textureDimensions(gtao_image));
    return textureLoad(gtao_image, min(vec2i(pos.xy), size - 1), 0).r;
}

// Specular occlusion from the ambient occlusion (Lagarde, Moving Frostbite to PBR), with a the
// squared perceptual roughness.
fn specular_occlusion(ndv: f32, ao: f32, a: f32) -> f32 {
    return clamp(pow(ndv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Indirect light of a fragment at a normal n, its diffuse part in direction n and its specular
// part along spec at a perceptual roughness: the ambient or the probe volume, then for the
// specular part the reflection probes of its cluster or the environment of the view. Every
// texture it reads is read with textureSampleLevel, so it may run in non uniform control flow.
fn indirect_light(world: vec3f, n: vec3f, spec: vec3f, roughness: f32, cluster: u32) -> Indirect {
    var e: Indirect;
    if (volume.size.w == 0u) {
        e = Indirect(ambient(n), ambient(spec));
    } else {
        e = volume_irradiance(world, n, spec);
    }

    if (view.reflection.x > 0u) {
        e.specular = reflections(world, spec, roughness, cluster, e.specular);
    } else if (view.environment.w > 0.0) {
        e.specular = environment_radiance(spec, roughness, e.specular);
    }

    return e;
}

// Light a surface reflects toward the eye, the indirect and direct diffuse light apart from
// the indirect and direct specular light and the emission. An unlit view shows the albedo as
// the diffuse part and the emission as the specular one. The back lobe of a diffuse
// transmission adds the indirect diffuse light from the far side, in direction -n, without
// ambient occlusion, which describes the front; the front keeps 1 - its factor.
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
    if (s.anisotropy > 0.0) {
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
    var cluster = 0u;
    if (view.lights.y > view.lights.x || view.grid.w > 0u) {
        cluster = cluster_index(view, cluster_view_position(view, world));
    }

    let e = indirect_light(world, n, spec, s.roughness, cluster);
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
        indirect_back = s.back_color * indirect_light(world, -n, -n, 1.0, cluster).diffuse;
    }

    // A sheen reflects the indirect diffuse light by the fit of its albedo, and the base keeps
    // its compensation of what it reflects without one; the coat below covers both.
    if (any(s.sheen > vec3f(0.0))) {
        let comp = sheen_compensation(s.sheen);
        let indirect_sheen = e.diffuse * s.sheen * sheen_albedo(ndv, s.sheen_roughness) *
            s.occlusion;
        indirect_specular = indirect_specular * comp + indirect_sheen;
        indirect_diffuse *= comp;
        indirect_back *= comp;
    }

    // The clear coat reflects the light along the reflection about its own normal at its own
    // roughness, by the split sum albedo of a dielectric, and the base below it what it does not.
    if (s.coat > 0.0) {
        let nc = s.geometric;
        let ac = s.coat_roughness * s.coat_roughness;
        let ndv_coat = max(dot(nc, v), 1e-4);
        let ab_coat = env_brdf(s.coat_roughness, ndv_coat);
        let spec_coat = mix(reflect(-v, nc), nc, ac);
        let coat_light = indirect_light(world, nc, spec_coat, s.coat_roughness, cluster).specular;
        let coat_albedo = (0.04 * ab_coat.x + ab_coat.y) * s.coat;
        indirect_specular = indirect_specular * (1.0 - coat_albedo) +
            coat_albedo * coat_light * specular_occlusion(ndv_coat, s.occlusion, ac);
        indirect_diffuse *= 1.0 - coat_albedo;
        indirect_back *= 1.0 - coat_albedo;
    }

    if (s.back_factor > 0.0) {
        indirect_diffuse += indirect_back;
    }

    let direct = lights_direct(s, v, world, cluster);
    return Lit(direct.diffuse + indirect_diffuse, direct.specular + indirect_specular +
        s.emissive);
}

fn shade(s: Surface, v: vec3f, world: vec3f) -> vec3f {
    let lit = shade_split(s, v, world);
    return lit.diffuse + lit.specular;
}

// Color of a fragment seen through the atmosphere between it and the camera, shaped for the
// blend equation: a fragment that covers the target is attenuated and adds the in scattered
// light weighted by its coverage, light added to or taken from the target is only attenuated,
// and a filter of the target is left as it is.
fn aerial_perspective(color: vec3f, alpha: f32, pos: vec4f, world: vec3f) -> vec3f {
    let ap = aerial_sample(pos, world);
    switch blend_mode {
        case 0u, 1u: {
            return color * ap.a + ap.rgb;
        }
        case 2u: {
            return color * ap.a + ap.rgb * alpha;
        }
        case 4u, 7u: {
            return color;
        }
        default: {
            return color * ap.a;
        }
    }
}

fn output(color: vec3f, alpha: f32) -> vec4f {
    switch blend_mode {
        case 0u: {
            return vec4f(color, 1.0);
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

// Mip of the opaque snapshot a Transmission shape with a DiffuseTransmission.blur samples: the
// blur radius grows by blur world units per world unit between the surface and the opaque
// image behind it, read from the depth copy of the snapshot, and is measured in pixels of mip 0
// at the distance of what is blurred (in a perspective view) or at the scale of an orthographic
// one. Contact is sharp, the sky (depth 0) takes the top mip, and the roughness blur stays the
// least it samples.
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

        // The blur radius in world units, seen at the distance of what is blurred, in pixels of
        // mip 0. view.cluster.y is the inverse of the projection scale of y: the tangent of
        // half the vertical field of view, or the half height of an orthographic view, where
        // the distance does not matter.
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

// Color of a transmissive fragment (Transmission), as flecs-engine shades it: the opaque
// snapshot seen where the ray refracted by the index of refraction leaves a volume of the
// thickness of the material times the scale of the instance, at a mip that follows the
// roughness, reduced by the Fresnel reflection of the index (Schlick), absorbed toward the
// attenuation color over the attenuation distance (Beer-Lambert) and tinted by the albedo. The
// transmission factor replaces the diffuse light by it; the specular light and the emission are
// added on top. The atmosphere between the fragment and the camera attenuates its own light
// and the transmitted light and adds its in scattered light by what is not transmitted. A
// factor of 0 is the lit surface. It writes no depth, so it takes no ambient occlusion of the
// view, which belongs to what is behind it.
fn transmission(f: VertexOut, s: Surface) -> vec4f {
    var ap = vec4f(0.0, 0.0, 0.0, 1.0);
    if (view.aerial.w > 0.0) {
        ap = aerial_sample(f.pos, f.world);
    }

    let v = normalize(view.camera.xyz - f.world);
    let reflected = shade_split(s, v, f.world);
    let factor = f.transmission.x;
    let ior = f.transmission.y;
    let thickness = f.transmission.z;
    let att_distance = f.transmission.w;
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

    // Screen-space refraction: project refracted exit point through VP
    let refracted = refract(-v, s.normal, 1.0 / max(ior, 0.001));
    let exit_pos = f.world + normalize(refracted) * thickness * f.model_scale;
    let exit_clip = view.view_proj * vec4f(exit_pos, 1.0);
    var screen_uv = exit_clip.xy / exit_clip.w * vec2f(0.5, -0.5) + vec2f(0.5, 0.5);
    screen_uv = clamp(screen_uv, vec2f(0.0), vec2f(1.0));

    // Sample opaque scene at refracted UV with roughness-dependent LOD
    let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);
    var bg_lod = s.roughness * max_mip;
    let diffuse_blur = f.scalars.z;
    if (diffuse_blur > 0.0) {
        bg_lod = transmission_blur_lod(f.world, screen_uv, diffuse_blur, bg_lod, max_mip);
    }
    let background = textureSampleLevel(opaque_snapshot, reflection_sampler, screen_uv,
        bg_lod).rgb;

    // Beer-Lambert absorption
    let att_color = max(f.attenuation, vec3f(0.001));
    let att_ratio = thickness / max(att_distance, 0.001);
    let transmittance = pow(att_color, vec3f(att_ratio));
    let transmitted = (1.0 - fresnel) * background * transmittance * s.albedo;

    // Blend: transmission replaces diffuse, specular always added on top
    let tf = saturate(factor);
    let own = reflected.diffuse * (1.0 - tf) + reflected.specular;
    let seen = mix(background * (1.0 - fresnel), transmitted, ap.a);
    let final_color = own * ap.a + ap.rgb * (1.0 - tf) + seen * tf;
    return vec4f(final_color, 1.0);
}

@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    var s = surface(f, front);
    if (transmissive) {
        return transmission(f, s);
    }

    if (alpha_cutout && s.alpha < f.scalars.x) {
        discard;
    }

    if (blend_mode == 0u) {
        s.occlusion = min(s.occlusion, view_occlusion(f.pos));
    }

    let v = normalize(view.camera.xyz - f.world);
    var color = shade(s, v, f.world);
    if (view.aerial.w > 0.0) {
        color = aerial_perspective(color, s.alpha, f.pos, f.world);
    }

    return output(color, s.alpha);
}
