// Samples the terrain of the TerrainSample tests of test/gpu/render/src/TerrainSample.c at a
// list of absolute positions (box_render_gpu_terrain_compute): per point whether a tile answered,
// its level, the height and the local y, then the normal, then the masks.
#include "etc/shaders/include/terrain_sample.wgsl"

struct ProbePoint {
    whole: vec2i,
    rest: vec2f,
};

struct ProbeResult {
    value: vec4f,
    normal: vec4f,
    mask: vec4f,
};

@group(1) @binding(0) var<storage, read> probe_points: array<ProbePoint>;
@group(1) @binding(1) var<storage, read_write> probe_results: array<ProbeResult>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i >= arrayLength(&probe_points) || i >= arrayLength(&probe_results)) {
        return;
    }

    let p = probe_points[i];
    let s = terrain_sample(p.whole, p.rest);
    let h = terrain_sample_height(p.whole, p.rest);
    var found = 0.0;
    if (s.found && h.y == 1.0 && abs(h.x - s.height) < 1e-4) {
        found = 1.0;
    }

    probe_results[i] = ProbeResult(vec4f(found, f32(s.level), s.height, s.y),
        vec4f(s.normal, 0.0), s.mask);
}
