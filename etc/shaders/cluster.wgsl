// Cluster pass: lists, for every cluster of a view, the point and spot lights that reach its
// view space box, in one light index list the clusters share. A spot light is also tested
// against its cone. The last depth slice reaches to infinity, so a fragment beyond the far end
// of the slices still finds the lights around it. A workgroup moves the lights to view space in
// batches its invocations share, once to count the lights of each cluster and once to list them.
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

// Invocations of a workgroup, and lights of a batch.
const BATCH: u32 = 64u;

// View space position and range of the lights of the batch.
var<workgroup> batch_sphere: array<vec4f, 64>;

// View space direction and cosine of the outer half angle of the lights of the batch, a cosine
// below -1 for a light without a cone.
var<workgroup> batch_cone: array<vec4f, 64>;

// Move light first + local of the batch to view space.
fn stage(first: u32, local: u32) {
    let i = first + local;
    if (i < view.lights.y) {
        let l = lights[i];
        batch_sphere[local] = vec4f(cluster_view_position(view, l.position), l.range);
        var cone = vec4f(0.0, 0.0, 1.0, -2.0);
        if (l.spot_scale > 0.0) {
            let d = (view.view * vec4f(l.direction, 0.0)).xyz;
            cone = vec4f(normalize(vec3f(d.x, d.y, -d.z)), clamp(-l.spot_offset / l.spot_scale, -1.0, 1.0));
        }
        batch_cone[local] = cone;
    }
}

// Whether light j of the batch reaches the view space box of a cluster: the sphere of its
// range touches the box and, for a spot light, its cone touches the sphere around the box
// (Wronski, "Cull that cone").
fn reaches(j: u32, lo: vec3f, hi: vec3f) -> bool {
    let sphere = batch_sphere[j];
    let d = sphere.xyz - clamp(sphere.xyz, lo, hi);
    var result = dot(d, d) <= sphere.w * sphere.w;
    let cone = batch_cone[j];
    if (result && cone.w >= -1.0) {
        let sin_outer = sqrt(1.0 - cone.w * cone.w);
        let center = (lo + hi) * 0.5;
        let radius = length(hi - lo) * 0.5;
        let v = center - sphere.xyz;
        let along = dot(v, cone.xyz);
        let across = sqrt(max(dot(v, v) - along * along, 0.0));
        let closest = cone.w * across - along * sin_outer;
        result = closest <= radius && along >= -radius;
    }

    return result;
}

@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) id: vec3u, @builtin(local_invocation_index) local: u32) {
    let grid = view.grid;
    let inside = id.x < grid.x && id.y < grid.y && id.z < grid.z;
    let z0 = cluster_slice_depth(view, id.z);
    var z1 = cluster_slice_depth(view, id.z + 1u);
    if (id.z + 1u >= grid.z) {
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
    for (var first = view.lights.x; first < view.lights.y; first += BATCH) {
        stage(first, local);
        workgroupBarrier();
        let size = min(BATCH, view.lights.y - first);
        for (var j = 0u; inside && j < size; j++) {
            if (reaches(j, lo, hi)) {
                count++;
            }
        }
        workgroupBarrier();
    }

    let total = cluster_count(view);
    let list = 2u * total;
    let capacity = arrayLength(&clusters.words) - min(list, arrayLength(&clusters.words));
    var offset = 0u;
    if (inside) {
        offset = atomicAdd(&clusters.allocated, count);
    }

    let listed = min(count, capacity - min(offset, capacity));
    var n = 0u;
    for (var first = view.lights.x; first < view.lights.y; first += BATCH) {
        stage(first, local);
        workgroupBarrier();
        let size = min(BATCH, view.lights.y - first);
        for (var j = 0u; inside && j < size && n < listed; j++) {
            if (reaches(j, lo, hi)) {
                clusters.words[list + offset + n] = first + j;
                n++;
            }
        }
        workgroupBarrier();
    }

    if (inside) {
        let cluster = id.x + id.y * grid.x + id.z * grid.x * grid.y;
        clusters.words[2u * cluster] = offset;
        clusters.words[2u * cluster + 1u] = listed;
    }
}
