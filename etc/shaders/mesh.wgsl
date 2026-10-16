// Forward pass: draws shape meshes with a physically based surface, a GGX distribution with
// height correlated Smith visibility and Schlick Fresnel, lit by the lights of the scene.
// Instance normals use the cofactor matrix of the model rotation, the inverse transpose up to
// a scale, so non-uniform scale keeps them perpendicular. Texture layers (group 1) are sampled
// with the instance UV transform; the normal map uses a tangent frame from screen derivatives.
// The cull pass writes the slots of the visible instances, which step per instance; the
// instance data is read from the instance buffer of the batch (group 2). Indirect light comes
// from the hemisphere ambient, or for a view that samples baked lighting from the irradiance
// probe volume of its scene (group 3).
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

@group(2) @binding(0) var<storage, read> instances: array<Instance>;

// Grid of the probe volume: probe (x, y, z) sits at origin.xyz + (x, y, z) * origin.w, x
// fastest in the probe buffer. size.w is 0 for the 1x1x1 volume of views that keep the
// hemisphere ambient, 1 when the view samples the probes.
struct Volume {
    origin: vec4f,
    size: vec4u,
};

// Every probe is SH_COEFFICIENTS rgb irradiance coefficients E_lm (Ramamoorthi and Hanrahan),
// in Sloan's real SH basis; probe_valid has one bit per probe, clear inside static geometry.
// probe_visibility holds an octahedral map per probe of VISIBILITY_SIZE texels on each side,
// each the mean and mean square distance to the surfaces its faces saw in probe spacings, packed
// as two halves (bake_sh.wgsl).
@group(3) @binding(0) var<uniform> volume: Volume;
@group(3) @binding(1) var<storage, read> probes: array<vec4f>;
@group(3) @binding(2) var<storage, read> probe_valid: array<u32>;
@group(3) @binding(3) var<storage, read> probe_visibility: array<u32>;

const SH_COEFFICIENTS: u32 = 9u;

// Texels per edge of the octahedral distance map of a probe.
const VISIBILITY_SIZE: i32 = 8;

// Offset along the normal of the point whose visibility from a probe is tested, in spacings.
const VISIBILITY_BIAS: f32 = 0.25;

// Smallest variance of the distance in a direction, in square spacings.
const VISIBILITY_VARIANCE: f32 = 0.001;

// Share of the weight of the probes around a point that the hemisphere ambient makes up for
// when the probes the point sees carry less.
const VISIBILITY_FALLBACK: f32 = 0.1;

struct VertexIn {
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
    let world = model * vec4f(v.position, 1.0);
    var o: VertexOut;
    o.pos = view.view_proj * world;
    o.world = world.xyz;
    o.normal = cofactor * v.normal * flip;
    o.color = inst.color;
    o.emissive = inst.emissive.rgb;
    o.material = inst.material.xy;
    o.uv = v.uv * inst.uv.xy + inst.uv.zw;
    return o;
}

// Hemisphere ambient light of the scene in a direction.
fn ambient(dir: vec3f) -> vec3f {
    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
}

// L2 SH irradiance coefficients of a probe, or of a blend of probes.
struct Irradiance {
    sh: array<vec3f, 9>,
};

// Irradiance of the hemisphere ambient: mix(ground, sky, y / 2 + 1 / 2) is a + b * y, which
// projects exactly onto the constant and the y coefficient.
fn hemisphere_irradiance() -> Irradiance {
    var e: Irradiance;
    let a = (view.ambient_sky.rgb + view.ambient_ground.rgb) * 0.5;
    let b = (view.ambient_sky.rgb - view.ambient_ground.rgb) * 0.5;
    e.sh[0] = a * (PI * 3.5449077018);
    e.sh[1] = b * (PI * 2.0466534158);
    return e;
}

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
// cubed, as DDGI weighs its probes.
fn visibility_from(index: u32, offset: vec3f) -> f32 {
    let len = length(offset);
    let dist = len / volume.origin.w;
    let t = octahedral_uv(offset / max(len, 1e-6)) * f32(VISIBILITY_SIZE) - 0.5;
    let i = vec2i(floor(t));
    let f = t - floor(t);
    let low = mix(visibility_moments(index, i.x, i.y),
        visibility_moments(index, i.x + 1, i.y), f.x);
    let high = mix(visibility_moments(index, i.x, i.y + 1),
        visibility_moments(index, i.x + 1, i.y + 1), f.x);
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
// position clamps to the border probes. Without a valid probe it is the hemisphere ambient,
// which also makes up the weight the visible probes lack below VISIBILITY_FALLBACK of the
// weight of every valid probe, so a surface that sees none of its probes does not take the
// light of the probes behind it.
fn volume_irradiance(world: vec3f, n: vec3f) -> Irradiance {
    let size = volume.size.xyz;
    let g = clamp((world - volume.origin.xyz) / volume.origin.w, vec3f(0.0), vec3f(size - 1u));
    let base = min(vec3u(floor(g)), max(size, vec3u(2u)) - 2u);
    let t = clamp(g - vec3f(base), vec3f(0.0), vec3f(1.0));
    let biased = world + n * (VISIBILITY_BIAS * volume.origin.w);
    var e: Irradiance;
    var total = 0.0;
    var reach = 0.0;
    for (var i = 0u; i < 8u; i++) {
        let o = vec3u(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
        let p = min(base + o, size - 1u);
        let index = p.x + (p.y + p.z * size.y) * size.x;
        let valid = (probe_valid[index >> 5u] >> (index & 31u)) & 1u;
        let trilinear = select(1.0 - t, t, o == vec3u(1u));
        let position = volume.origin.xyz + vec3f(p) * volume.origin.w;
        let to_probe = position - world;
        let dist = length(to_probe);
        let dir = select(n, to_probe / max(dist, 1e-4), dist > 1e-4);
        let wrap = (dot(dir, n) + 1.0) * 0.5;
        let reached = trilinear.x * trilinear.y * trilinear.z * (wrap * wrap + 0.2) * f32(valid);
        if (reached > 0.0) {
            let w = reached * visibility_from(index, biased - position);
            for (var k = 0u; k < SH_COEFFICIENTS; k++) {
                e.sh[k] += probes[index * SH_COEFFICIENTS + k].rgb * w;
            }

            total += w;
            reach += reached;
        }
    }

    if (reach <= 0.0) {
        return hemisphere_irradiance();
    }

    let fallback = max(VISIBILITY_FALLBACK * reach - total, 0.0);
    let hemisphere = hemisphere_irradiance();
    for (var k = 0u; k < SH_COEFFICIENTS; k++) {
        e.sh[k] = (e.sh[k] + hemisphere.sh[k] * fallback) / (total + fallback);
    }

    return e;
}

// Irradiance over pi in a direction, the term the hemisphere ambient stands in for. The
// direction is used as given, like the hemisphere, which reads its y without normalizing.
fn irradiance(e: Irradiance, d: vec3f) -> vec3f {
    let band0 = e.sh[0] * 0.2820947918;
    let band1 = (e.sh[1] * d.y + e.sh[2] * d.z + e.sh[3] * d.x) * 0.4886025119;
    let band2 = (e.sh[4] * (d.x * d.y) + e.sh[5] * (d.y * d.z) + e.sh[7] * (d.x * d.z))
        * 1.0925484306 + e.sh[6] * (0.3153915653 * (3.0 * d.z * d.z - 1.0))
        + e.sh[8] * (0.5462742153 * (d.x * d.x - d.y * d.y));
    return max((band0 + band1 + band2) / PI, vec3f(0.0));
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
fn lights_direct(s: Surface, v: vec3f, world: vec3f) -> vec3f {
    var sum = vec3f(0.0);
    let shadow = shadow_factor(world, s.normal);
    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let visible = select(1.0, shadow, i == 0u);
        sum += brdf(s, v, normalize(light.position), light.color) * visible;
    }

    if (view.lights.y > view.lights.x) {
        let cluster = cluster_index(view, cluster_view_position(view, world));
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        let count = clusters[2u + 2u * cluster];
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
    var specular_light: vec3f;
    var diffuse_light: vec3f;
    if (volume.size.w == 0u) {
        specular_light = ambient(mix(r, n, a));
        diffuse_light = ambient(n);
    } else {
        let e = volume_irradiance(world, n);
        specular_light = irradiance(e, mix(r, n, a));
        diffuse_light = irradiance(e, n);
    }

    let indirect_specular = (f0 * ab.x + ab.y) * specular_light;
    let indirect_diffuse = diffuse_color * diffuse_light;
    return lights_direct(s, v, world) + indirect_diffuse + indirect_specular + s.emissive;
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
    return output(shade(s, v, f.world), s.alpha);
}
