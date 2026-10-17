// Uniforms of a view and the lights of its scene, shared by the forward pass and the cluster
// pass. The light buffer holds the directional lights first, then the point and spot lights.
// The view frustum is cut into clusters: grid.x by grid.y tiles of the image and grid.z depth
// slices spaced logarithmically from cluster.z to cluster.w. The cluster buffer starts with the
// number of list entries the cluster pass allocated, then holds the offset and count word of
// every cluster, then the index list: a cluster lists cluster_lights(count) light indices from
// its offset, followed by cluster_probes(count) indices into the reflection probe buffer of the
// scene in blend order, at most CLUSTER_PROBES. The shadow members describe the cascaded shadow map of the first directional light,
// see shadow.wgsl.
#pragma once

struct View {
    view_proj: mat4x4f,
    view: mat4x4f,
    camera: vec4f,
    // x, y: render width and height in pixels, z, w: their inverse
    viewport: vec4f,
    ambient_sky: vec4f,
    ambient_ground: vec4f,
    // x, y: inverse projection scale of x and y, z, w: near and far of the depth slices
    cluster: vec4f,
    // x: directional lights, y: lights, z: 1 when lit, w: 1 for an orthographic camera
    lights: vec4u,
    // x, y, z: clusters per axis, w: reflection probes the clusters list, the first ones of the
    // reflection probe buffer
    grid: vec4u,
    // World to shadow clip space of each cascade, reversed Z: 1 is nearest the light
    shadow_matrix: array<mat4x4f, 4>,
    // View depth each cascade ends at
    shadow_split: vec4f,
    // Depth added to the compared depth of each cascade
    shadow_bias: vec4f,
    // World distance a lookup in each cascade moves along the normal
    shadow_offset: vec4f,
    // x: view depth the fade starts at, y: view depth the shadows end at, z: texel in uv
    shadow_params: vec4f,
    // x: cascades, 0 without shadows, y: PCF taps per axis
    shadow_config: vec4u,
};

struct Light {
    position: vec3f,
    range: f32,
    color: vec3f,
    spot_scale: f32,
    direction: vec3f,
    spot_offset: f32,
};

// A resident reflection probe of the scene, see box_render_reflection_probe_t. The influence
// and projection boxes are centered on center and oriented by axes.
struct ReflectionProbe {
    center: vec3f,
    blend: f32,
    axes: array<vec4f, 3>,
    extent: vec3f,
    priority: i32,
    projection: vec3f,
    slot: u32,
    capture: vec3f,
    scene: u32,
};

// Reflection probes a cluster lists at most.
const CLUSTER_PROBES: u32 = 8u;

// Bit of the count word of a cluster its probe count starts at; the lights are below it.
const CLUSTER_PROBE_SHIFT: u32 = 28u;

// Lights a cluster lists, from its count word.
fn cluster_lights(count: u32) -> u32 {
    return count & ((1u << CLUSTER_PROBE_SHIFT) - 1u);
}

// Reflection probes a cluster lists after its lights, from its count word.
fn cluster_probes(count: u32) -> u32 {
    return count >> CLUSTER_PROBE_SHIFT;
}

// View space position with the depth in front of the camera as a positive z.
fn cluster_view_position(v: View, world: vec3f) -> vec3f {
    let p = v.view * vec4f(world, 1.0);
    return vec3f(p.x, p.y, -p.z);
}

// Number of clusters of a view.
fn cluster_count(v: View) -> u32 {
    return v.grid.x * v.grid.y * v.grid.z;
}

// Distance in front of the camera at which a depth slice starts.
fn cluster_slice_depth(v: View, slice: u32) -> f32 {
    let t = f32(slice) / f32(v.grid.z);
    return v.cluster.z * pow(v.cluster.w / v.cluster.z, t);
}

// Index of the cluster a view space position falls in.
fn cluster_index(v: View, p: vec3f) -> u32 {
    let depth = max(p.z, 1e-4);
    var ndc = p.xy / v.cluster.xy;
    if (v.lights.w == 0u) {
        ndc = ndc / depth;
    }

    let fx = (ndc.x + 1.0) * 0.5 * f32(v.grid.x);
    let fy = (1.0 - ndc.y) * 0.5 * f32(v.grid.y);
    let fz = log(depth / v.cluster.z) / log(v.cluster.w / v.cluster.z) * f32(v.grid.z);
    let x = min(u32(max(fx, 0.0)), v.grid.x - 1u);
    let y = min(u32(max(fy, 0.0)), v.grid.y - 1u);
    let z = min(u32(max(fz, 0.0)), v.grid.z - 1u);
    return x + y * v.grid.x + z * v.grid.x * v.grid.y;
}
