// Cluster pass: lists, for every cluster of a view, the point and spot lights whose range
// reaches its view space box. The last depth slice reaches to infinity, so a fragment beyond
// the far end of the slices still finds the lights around it.
#include "include/lights.wgsl"

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read_write> clusters: array<u32>;

const FAR_SLICE: f32 = 1e9;

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

    let cluster = id.x + id.y * grid.x + id.z * grid.x * grid.y;
    let base = cluster * (grid.w + 1u);
    var count = 0u;
    for (var i = view.lights.x; i < view.lights.y && count < grid.w; i++) {
        let l = lights[i];
        let p = cluster_view_position(view, l.position);
        let d = p - clamp(p, lo, hi);
        if (dot(d, d) <= l.range * l.range) {
            clusters[base + 1u + count] = i;
            count++;
        }
    }

    clusters[base] = count;
}
