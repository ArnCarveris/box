// Cluster pass: lists, for every cluster of a view, the point and spot lights that reach its
// view space box, in one light index list the clusters share. A spot light is also tested
// against its cone. The last depth slice reaches to infinity, so a fragment beyond the far end
// of the slices still finds the lights around it.
#include "include/lights.wgsl"

// The cluster buffer after its first word: the offset and count of every cluster, then the
// light index list the clusters take their ranges from.
struct ClusterBuffer {
    allocated: atomic<u32>,
    words: array<u32>,
};

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read_write> clusters: ClusterBuffer;

const FAR_SLICE: f32 = 1e9;

// Whether a light reaches the view space box of a cluster: the sphere of its range touches
// the box and, for a spot light, its cone touches the sphere around the box.
fn cluster_reaches(l: Light, lo: vec3f, hi: vec3f) -> bool {
    let p = cluster_view_position(view, l.position);
    let d = p - clamp(p, lo, hi);
    var result = dot(d, d) <= l.range * l.range;
    if (result && l.spot_scale > 0.0) {
        let cos_outer = clamp(-l.spot_offset / l.spot_scale, -1.0, 1.0);
        let sin_outer = sqrt(1.0 - cos_outer * cos_outer);
        let world_dir = (view.view * vec4f(l.direction, 0.0)).xyz;
        let dir = normalize(vec3f(world_dir.x, world_dir.y, -world_dir.z));
        let center = (lo + hi) * 0.5;
        let radius = length(hi - lo) * 0.5;
        let v = center - p;
        let along = dot(v, dir);
        let across = sqrt(max(dot(v, v) - along * along, 0.0));
        let closest = cos_outer * across - along * sin_outer;
        result = closest <= radius && along >= -radius;
    }

    return result;
}

@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let grid = view.grid;
    if (id.x >= grid.x || id.y >= grid.y || id.z >= grid.z) {
        return;
    }

    let z0 = cluster_slice_depth(view, id.z);
    var z1 = cluster_slice_depth(view, id.z + 1u);
    if (id.z + 1u == grid.z) {
        z1 = FAR_SLICE;
    }

    let x0 = (f32(id.x) / f32(grid.x) * 2.0 - 1.0) * view.cluster.x;
    let x1 = (f32(id.x + 1u) / f32(grid.x) * 2.0 - 1.0) * view.cluster.x;
    let y1 = (1.0 - f32(id.y) / f32(grid.y) * 2.0) * view.cluster.y;
    let y0 = (1.0 - f32(id.y + 1u) / f32(grid.y) * 2.0) * view.cluster.y;
    var lo = vec3f(x0, y0, z0);
    var hi = vec3f(x1, y1, z1);
    if (view.lights.w == 0u) {
        let xs = vec4f(x0 * z0, x0 * z1, x1 * z0, x1 * z1);
        let ys = vec4f(y0 * z0, y0 * z1, y1 * z0, y1 * z1);
        lo = vec3f(min(min(xs.x, xs.y), min(xs.z, xs.w)), min(min(ys.x, ys.y), min(ys.z, ys.w)), z0);
        hi = vec3f(max(max(xs.x, xs.y), max(xs.z, xs.w)), max(max(ys.x, ys.y), max(ys.z, ys.w)), z1);
    }

    var count = 0u;
    for (var i = view.lights.x; i < view.lights.y; i++) {
        if (cluster_reaches(lights[i], lo, hi)) {
            count++;
        }
    }

    let total = cluster_count(view);
    let list = 2u * total;
    let capacity = arrayLength(&clusters.words) - min(list, arrayLength(&clusters.words));
    let offset = atomicAdd(&clusters.allocated, count);
    let listed = min(count, capacity - min(offset, capacity));
    var n = 0u;
    for (var i = view.lights.x; i < view.lights.y && n < listed; i++) {
        if (cluster_reaches(lights[i], lo, hi)) {
            clusters.words[list + offset + n] = i;
            n++;
        }
    }

    let cluster = id.x + id.y * grid.x + id.z * grid.x * grid.y;
    clusters.words[2u * cluster] = offset;
    clusters.words[2u * cluster + 1u] = listed;
}
