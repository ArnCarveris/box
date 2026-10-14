// Forward pass: draws shape meshes with a physically based surface, a GGX distribution with
// height correlated Smith visibility and Schlick Fresnel, lit by the lights of the scene.
// Instance normals use the cofactor matrix of the model rotation, the inverse transpose up to
// a scale, so non-uniform scale keeps them perpendicular. Texture layers (group 1) are sampled
// with the instance UV transform; the normal map uses a tangent frame from screen derivatives.
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

struct VertexIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) model_0: vec4f,
    @location(4) model_1: vec4f,
    @location(5) model_2: vec4f,
    @location(6) model_3: vec4f,
    @location(7) color: vec4f,
    @location(8) emissive: vec4f,
    @location(9) material: vec4f,
    @location(10) uv_transform: vec4f,
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
    let model = mat4x4f(v.model_0, v.model_1, v.model_2, v.model_3);
    let c0 = v.model_0.xyz;
    let c1 = v.model_1.xyz;
    let c2 = v.model_2.xyz;
    let cofactor = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let world = model * vec4f(v.position, 1.0);
    var o: VertexOut;
    o.pos = view.view_proj * world;
    o.world = world.xyz;
    o.normal = cofactor * v.normal * flip;
    o.color = v.color;
    o.emissive = v.emissive.rgb;
    o.material = v.material.xy;
    o.uv = v.uv * v.uv_transform.xy + v.uv_transform.zw;
    return o;
}

// Hemisphere ambient light of the scene in a direction.
fn ambient(dir: vec3f) -> vec3f {
    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
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

// Radiance of a point or spot light at a position: a windowed inverse square falloff that
// reaches zero at the range, times the smooth cone of a spot light.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    return window * window / (dist * dist + 1.0) * cone * cone;
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
        let base = cluster_index(view, cluster_view_position(view, world)) * (view.grid.w + 1u);
        let count = min(clusters[base], view.grid.w);
        for (var i = 0u; i < count; i++) {
            let light = lights[clusters[base + 1u + i]];
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
    let indirect_specular = (f0 * ab.x + ab.y) * ambient(mix(r, n, a));
    let indirect_diffuse = diffuse_color * ambient(n);
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
