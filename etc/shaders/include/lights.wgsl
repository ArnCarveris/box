// Uniforms of a view and the lights of its scene, shared by the forward pass and the cluster
// pass. The light buffer holds the directional lights first, then the point and spot lights.
// The view frustum is cut into clusters: grid.x by grid.y tiles of the image and grid.z depth
// slices spaced logarithmically from cluster.z to cluster.w. A cluster is grid.w + 1 words in
// the cluster buffer: the number of lights that reach it and their indices.
#pragma once

struct View {
    view_proj: mat4x4f,
    view: mat4x4f,
    camera: vec4f,
    ambient_sky: vec4f,
    ambient_ground: vec4f,
    // x, y: inverse projection scale of x and y, z, w: near and far of the depth slices
    cluster: vec4f,
    // x: directional lights, y: lights, z: 1 when lit, w: 1 for an orthographic camera
    lights: vec4u,
    // x, y, z: clusters per axis, w: lights a cluster lists at most
    grid: vec4u,
};

struct Light {
    position: vec3f,
    range: f32,
    color: vec3f,
    spot_scale: f32,
    direction: vec3f,
    spot_offset: f32,
};

// View space position with the depth in front of the camera as a positive z.
fn cluster_view_position(v: View, world: vec3f) -> vec3f {
    let p = v.view * vec4f(world, 1.0);
    return vec3f(p.x, p.y, -p.z);
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
