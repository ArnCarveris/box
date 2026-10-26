// Forward pass: draws shape meshes with a physically based surface, a GGX distribution with
// height correlated Smith visibility and Schlick Fresnel, lit by the lights of the scene.
// Instance normals use the cofactor matrix of the model rotation, the inverse transpose up to
// a scale, so non-uniform scale keeps them perpendicular. Texture layers (group 1) are sampled
// with the instance UV transform; the normal map uses a tangent frame from screen derivatives.
// The cull pass writes the slots of the visible instances, which step per instance; the
// instance data is read from the instance buffer of the batch (group 2). Indirect light comes
// from the hemisphere ambient, or for a view that samples baked lighting from the irradiance
// probe volume of its scene (group 3). The indirect specular light of a view that samples
// reflection probes comes from the prefiltered cubemaps of the probes its clusters list. A view
// lit by an environment takes its SH in place of the hemisphere ambient and its prefiltered
// cubemap as an infinitely distant probe below the scene probe. A view
// that renders the atmosphere of its scene attenuates the shaded color and adds the in scattered
// light from one trilinear tap of its aerial perspective volume.
#include "include/instance.wgsl"
#include "include/lights.wgsl"
#include "include/shadow.wgsl"

// BlendMode of the pipeline: the output is shaped for the blend equation it is drawn with.
override blend_mode: u32 = 0u;

const PI: f32 = 3.141592653589793;
const MIN_ROUGHNESS: f32 = 0.045;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;

@group(1) @binding(0) var albedo_map: texture_2d<f32>;
@group(1) @binding(1) var emissive_map: texture_2d<f32>;
@group(1) @binding(2) var roughness_map: texture_2d<f32>;
@group(1) @binding(3) var normal_map: texture_2d<f32>;
@group(1) @binding(4) var material_sampler: sampler;

// Reflection probes of the scene in blend order (box_render_reflection_probe_t), the cubemap
// array with a slot per probe, mip k of N prefiltered for the perceptual roughness k / (N - 1)
// (bake_sh.wgsl), and the L2 SH irradiance of each slot, as a probe of the volume holds it.
@group(0) @binding(5) var<storage, read> reflection_probes: array<ReflectionProbe>;
@group(0) @binding(6) var reflection_cubes: texture_cube_array<f32>;
@group(0) @binding(7) var reflection_sampler: sampler;
@group(0) @binding(8) var<storage, read> reflection_sh: array<vec4f>;

// Aerial perspective volume of the view (atmosphere.wgsl): x and y the uv of the screen, z the
// distance from the camera, rgb the in scattered light and a the transmittance up to it.
@group(0) @binding(9) var aerial_volume: texture_3d<f32>;
@group(0) @binding(10) var aerial_sampler: sampler;

// Environment of the view (LightSettings.environment): a cubemap whose mip k of N is
// prefiltered for the perceptual roughness k / (N - 1), as a reflection probe slot, and its L2
// SH irradiance. Views without one bind the black reflection slot and a zero SH, and their
// view.environment.w is 0.
@group(0) @binding(12) var environment_cube: texture_cube<f32>;
@group(0) @binding(13) var<uniform> environment_sh: array<vec4f, 9>;

@group(2) @binding(0) var<storage, read> instances: array<Instance>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;

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
    @builtin(position) pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) emissive: vec3f,
    @location(4) material: vec2f,
    @location(5) uv: vec2f,
};

@vertex fn vs(v: VertexIn) -> VertexOut {
    let inst = instances[v.slot];
    let model = inst.model;
    let c0 = model[0].xyz;
    let c1 = model[1].xyz;
    let c2 = model[2].xyz;
    let cofactor = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    var position = v.position;
    var normal = v.normal;
    if (inst.surface.p[19].x != 0.0) {
        position = v.position;
        normal = v.normal;
    } else if (inst.surface.p[0].x == 7.0) {
        position.y *= inst.surface.p[1].y;
    } else if (inst.surface.p[0].x == 1.0 && v.position.z < 2.0) {
        let sample = surface_sweep_sample(inst.surface, i32(v.position.z), v.position.x, v.position.y);
        position = sample.position;
        normal = sv_unit(sample.normal);
        if (sv_dot(sample.normal, sample.normal) < 0.000000000001) {
            normal = surface_normal(inst.surface, i32(v.position.z), v.position.x, v.position.y);
        }
    } else if (inst.surface.p[0].x != 0.0) {
        position = surface_position(inst.surface, i32(v.position.z), v.position.x, v.position.y);
        normal = surface_normal(inst.surface, i32(v.position.z), v.position.x, v.position.y);
    }
    let world = model * vec4f(position, 1.0);
    var o: VertexOut;
    o.pos = view.view_proj * world;
    o.world = world.xyz;
    o.normal = cofactor * normal * flip;
    o.color = inst.color;
    o.emissive = inst.emissive.rgb;
    o.material = inst.material.xy;
    o.uv = v.uv * inst.uv.xy + inst.uv.zw;
    if (inst.surface.p[0].x != 0.0) {
        var region = i32(v.position.z);
        if (inst.surface.p[0].x == 7.0) {
            region = select(0, 1, v.uv.x >= 2.0);
            if (abs(v.normal.y) > 0.5) {
                region = 2;
            } else if (region == 1) {
                o.uv.x -= 2.0 * inst.uv.x;
            }
        }
        var material_region = 2;
        if (region == 0 || region == 1) {
            material_region = region;
        } else if (region == 2 || region == 3 || region == 8 || region == 9) {
            material_region = 3;
        }
        if (inst.surface.p[19].x != 0.0) {
            material_region = i32(surface_assets[v.vertex / 4u][v.vertex % 4u]);
        }
        let packed = bitcast<u32>(inst.surface.p[21][material_region]);
        if ((packed & 0x80000000u) != 0u) {
            let tint = unpack4x8unorm(bitcast<u32>(inst.surface.p[20][material_region]));
            let linear = select(pow((tint.rgb + vec3f(0.055)) / 1.055, vec3f(2.4)),
                tint.rgb / 12.92, tint.rgb <= vec3f(0.04045));
            o.color *= vec4f(linear, tint.a);
            o.material = vec2f(f32((packed >> 16u) & 32767u) / 32767.0,
                f32(packed & 65535u) / 65535.0);
        }
    }
    return o;
}

// Irradiance in a direction from the L2 SH irradiance coefficients E_lm of a probe
// (Ramamoorthi and Hanrahan, Sloan's real basis), before the division by pi and the clamp.
fn sh_irradiance(e0: vec3f, e1: vec3f, e2: vec3f, e3: vec3f, e4: vec3f, e5: vec3f,
    e6: vec3f, e7: vec3f, e8: vec3f, d: vec3f) -> vec3f
{
    let band0 = e0 * 0.2820947918;
    let band1 = (e1 * d.y + e2 * d.z + e3 * d.x) * 0.4886025119;
    let band2 = (e4 * (d.x * d.y) + e5 * (d.y * d.z) + e7 * (d.x * d.z)) * 1.0925484306
        + e6 * (0.3153915653 * (3.0 * d.z * d.z - 1.0))
        + e8 * (0.5462742153 * (d.x * d.x - d.y * d.y));
    return band0 + band1 + band2;
}

// Irradiance over pi in a world direction from the SH of the environment of the view, turned by
// its yaw, times its intensity.
fn environment_irradiance(dir: vec3f) -> vec3f {
    let d = environment_direction(view, dir);
    let e = sh_irradiance(environment_sh[0].rgb, environment_sh[1].rgb, environment_sh[2].rgb,
        environment_sh[3].rgb, environment_sh[4].rgb, environment_sh[5].rgb,
        environment_sh[6].rgb, environment_sh[7].rgb, environment_sh[8].rgb, d);
    return max(e / PI, vec3f(0.0)) * view.environment.x;
}

// Ambient light of the scene in a direction: the SH of the environment of a view lit by one,
// else the hemisphere ambient.
fn ambient(dir: vec3f) -> vec3f {
    if (view.environment.w > 0.0) {
        return environment_irradiance(dir);
    }

    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
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
// here on front faces; a plain back face keeps the constants and faces the camera.
struct Surface {
    albedo: vec3f,
    alpha: f32,
    metallic: f32,
    roughness: f32,
    emissive: vec3f,
    normal: vec3f,
};

// Tangent frame from the screen derivatives of the position and the UVs (cotangent frame),
// applied to a tangent space normal whose y points up the image, toward smaller v. The
// framebuffer y of dpdy points down, which negates both derivative axes of the frame.
fn perturb(n: vec3f, p: vec3f, uv: vec2f, m: vec3f) -> vec3f {
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
    let tangent = mat3x3f(-t * scale, b * scale, n);
    return select(normalize(tangent * m), n, len2 < 1e-20);
}

fn surface(f: VertexOut, front: bool) -> Surface {
    let albedo = textureSample(albedo_map, material_sampler, f.uv);
    let emissive = textureSample(emissive_map, material_sampler, f.uv).rgb;
    let roughness = textureSample(roughness_map, material_sampler, f.uv).g;
    let tangent_normal = textureSample(normal_map, material_sampler, f.uv).xyz * 2.0 - 1.0;
    let geometric = normalize(f.normal);
    let mapped = perturb(geometric, f.world, f.uv, tangent_normal);
    var s: Surface;
    s.albedo = f.color.rgb;
    s.alpha = f.color.a;
    s.metallic = clamp(f.material.x, 0.0, 1.0);
    s.roughness = clamp(f.material.y, MIN_ROUGHNESS, 1.0);
    s.emissive = f.emissive;
    s.normal = -geometric;
    if (front) {
        s.albedo *= albedo.rgb;
        s.alpha *= albedo.a;
        s.roughness = clamp(f.material.y * roughness, MIN_ROUGHNESS, 1.0);
        s.emissive *= emissive;
        s.normal = mapped;
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

// Light reflected toward the eye by one light arriving from direction l with a radiance.
fn brdf(s: Surface, v: vec3f, l: vec3f, radiance: vec3f) -> vec3f {
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
    return (diffuse + specular) * radiance * ndl;
}

// Distance below which a point or spot light stops getting brighter, so a surface at the light
// stays finite.
const LIGHT_MIN_DISTANCE: f32 = 0.01;

// Radiance of a point or spot light at a position: inverse square falloff, smoothly windowed
// to zero at the range as in Frostbite's smoothDistanceAtt, times the smooth cone of a spot.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    let near_ = max(dist, LIGHT_MIN_DISTANCE);
    return window * window / (near_ * near_) * cone * cone;
}

// Light inputs. Every direct light the forward pass uses comes from here: the directional
// lights of the scene, the first one through its shadow map, then the point and spot lights
// of the cluster of the fragment.
fn lights_direct(s: Surface, v: vec3f, world: vec3f, cluster: u32) -> vec3f {
    var sum = vec3f(0.0);
    let shadow = shadow_factor(world, s.normal);
    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let visible = select(1.0, shadow, i == 0u);
        sum += brdf(s, v, normalize(light.position), light.color) * visible;
    }

    if (view.lights.y > view.lights.x) {
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        let count = cluster_lights(clusters[2u + 2u * cluster]);
        for (var i = 0u; i < count; i++) {
            let light = lights[clusters[first + i]];
            let delta = light.position - world;
            let dist = length(delta);
            let l = delta / max(dist, 1e-4);
            sum += brdf(s, v, l, light.color * attenuation(light, l, dist));
        }
    }

    return sum;
}

fn shade(s: Surface, v: vec3f, world: vec3f) -> vec3f {
    if (view.lights.z == 0u) {
        return s.albedo + s.emissive;
    }

    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let ab = env_brdf(s.roughness, ndv);
    let r = reflect(-v, n);
    let spec = mix(r, n, a);
    var specular_light: vec3f;
    var diffuse_light: vec3f;
    if (volume.size.w == 0u) {
        specular_light = ambient(spec);
        diffuse_light = ambient(n);
    } else {
        let e = volume_irradiance(world, n, spec);
        specular_light = e.specular;
        diffuse_light = e.diffuse;
    }

    var cluster = 0u;
    if (view.lights.y > view.lights.x || view.grid.w > 0u) {
        cluster = cluster_index(view, cluster_view_position(view, world));
    }

    if (view.reflection.x > 0u) {
        specular_light = reflections(world, spec, s.roughness, cluster, specular_light);
    } else if (view.environment.w > 0.0) {
        specular_light = environment_radiance(spec, s.roughness, specular_light);
    }

    // The split sum scale and bias already integrate the Fresnel over the GGX lobe of the
    // roughness, so the grazing reflection of a rough surface fades without a roughness aware
    // f90. What the lobe reflects is not available to the diffuse term, as 1 - F in brdf().
    let specular_albedo = f0 * ab.x + ab.y;
    let indirect_specular = specular_albedo * specular_light;
    let indirect_diffuse = (vec3f(1.0) - specular_albedo) * diffuse_color * diffuse_light;
    return lights_direct(s, v, world, cluster) + indirect_diffuse + indirect_specular +
        s.emissive;
}

// Color of a fragment seen through the atmosphere between it and the camera, shaped for the
// blend equation: a fragment that covers the target is attenuated and adds the in scattered
// light weighted by its coverage, light added to or taken from the target is only attenuated,
// and a filter of the target is left as it is.
fn aerial_perspective(color: vec3f, alpha: f32, pos: vec4f, world: vec3f) -> vec3f {
    let uv = pos.xy * view.viewport.zw;
    let km = length(world - view.camera.xyz) * view.aerial.x;
    let slices = view.aerial.z;
    let w = (km * view.aerial.y * (slices - 1.0) + 0.5) / slices;
    let ap = textureSampleLevel(aerial_volume, aerial_sampler, vec3f(uv, w), 0.0);
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

@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let s = surface(f, front);
    let v = normalize(view.camera.xyz - f.world);
    var color = shade(s, v, f.world);
    if (view.aerial.w > 0.0) {
        color = aerial_perspective(color, s.alpha, f.pos, f.world);
    }

    return output(color, s.alpha);
}
