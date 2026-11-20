// Grass draw (gpu_grass.c): one indirect draw per Grass and level of detail in the depth prepass
// and the forward pass of a lit view, every instance a blade the place pass appended to the range
// of its level (grass_place.wgsl). The vertex stage builds the blade as a triangle strip from the
// vertex index, the level in its first vertex: two vertices per row across the blade, tapered
// and bent along its facing, and one at the tip. The blades are opaque and lit by the sun of the
// scene through its shadows and cookie, wrapped through the thin blade, by the ambient light and
// seen through the aerial perspective; the fog passes after the forward pass fog them.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"
#include "include/grass.wgsl"

const PI: f32 = 3.141592653589793;

// Share of the light behind a blade that comes through it
const TRANSLUCENCY: f32 = 0.35;

// How far the normal of a blade leans toward up, so the field shades like the ground it covers
const NORMAL_UP: f32 = 0.35;

// Light that reaches the base of a blade, under the others
const BASE_OCCLUSION: f32 = 0.35;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;

@group(1) @binding(0) var<uniform> params: GrassParams;
@group(1) @binding(1) var<storage, read> blades: array<GrassBlade>;

struct VertexOut {
    @builtin(position) @invariant clip: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    // 0 at the base, 1 at the tip
    @location(2) along: f32,
    @location(3) @interpolate(flat) shade: f32,
};

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> VertexOut {
    var out: VertexOut;
    let lod = min(vertex / GRASS_LOD_VERTICES, GRASS_LODS - 1u);
    let v = vertex % GRASS_LOD_VERTICES;
    let segments = grass_segments(lod);
    let b = blades[lod * params.config.w + instance];
    let row = min(v / 2u, segments);
    let t = f32(row) / f32(segments);
    let side = select(f32(v & 1u) * 2.0 - 1.0, 0.0, v >= 2u * segments);
    let facing = vec3f(cos(b.yaw), 0.0, sin(b.yaw));
    let across = vec3f(-facing.z, 0.0, facing.x);
    let bend = 0.45 * b.lean;
    let half_width = 0.5 * b.width * (1.0 - t);
    let rise = b.height * t * (1.0 - 0.3 * bend * t);
    let lean = b.height * bend * t * t;
    let world = b.position + across * (side * half_width) + vec3f(0.0, rise, 0.0) +
        facing * lean;
    let tangent = normalize(vec3f(0.0, 1.0 - 0.6 * bend * t, 0.0) + facing * (2.0 * bend * t));
    let flat_normal = normalize(cross(across, tangent));
    out.normal = normalize(flat_normal + across * (0.5 * side));
    out.world = world;
    out.clip = view.view_proj * vec4f(world, 1.0);
    out.along = t;
    out.shade = b.shade;
    return out;
}

// Light a blade reflects toward the eye: the ambient light, darker toward its base, and the
// directional lights, the first through its shadows and cookie, on its front and through it.
fn grass_light(world: vec3f, n: vec3f, along: f32) -> vec3f {
    let lit = normalize(mix(n, vec3f(0.0, 1.0, 0.0), NORMAL_UP));
    var sum = ambient(lit) * mix(BASE_OCCLUSION, 1.0, along);
    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let l = normalize(light.position);
        var visible = 1.0;
        if (i == 0u) {
            visible = shadow_factor(world, lit) * light_cookie(world, l, lit);
        }

        let front = saturate(dot(lit, l));
        let through = saturate(dot(-n, l)) * TRANSLUCENCY * along;
        sum += light.color * ((front + through) * visible / PI);
    }
    return sum;
}

@fragment
fn fs(in: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let n = normalize(in.normal) * select(-1.0, 1.0, front);
    let albedo = mix(params.base.rgb, params.tip.rgb, in.along) * in.shade;
    var color = albedo * grass_light(in.world, n, in.along);
    if (view.aerial.w > 0.0) {
        let ap = aerial_sample(in.clip, in.world);
        color = color * ap.a + ap.rgb;
    }

    return vec4f(color, 1.0);
}
