// Cull pass: tests the instances of the draws of a view, or of a cascade of its shadows,
// against its frustum and its smallest projected size, and writes the slots of the visible
// ones, compacted per draw, with the instance count of its indexed indirect draw arguments.
//
// A job is the slot range of one draw in the instance buffer bound at group 1. Its bounds are
// the box around the mesh, moved into world space by the model matrix (mesh bounds times the
// instance scale and the world transform); the planes are the frustum in world space.
//
// cull_instances tests one instance per invocation of one job: a workgroup counts its visible
// instances and reserves their entries with one atomic, so the order within a draw is not
// kept. cull_ordered tests one job per invocation in slot order, for the blended draws that
// are sorted back to front. A depth pyramid test can follow the frustum test in visible().

#include "include/instance.wgsl"

struct Cull {
    planes: array<vec4f, 6>,
    clip_w: vec4f,
    size: vec4f,
    jobs: vec4u,
};

struct Job {
    first: u32,
    count: u32,
    args: u32,
    base: u32,
    center: vec3f,
    extent: vec3f,
};

// Words of the indexed indirect draw arguments of a draw; the instance count is the second.
const ARGS: u32 = 5u;

@group(0) @binding(0) var<uniform> cull: Cull;
@group(0) @binding(1) var<storage, read> jobs: array<Job>;
@group(0) @binding(2) var<storage, read_write> visible_slots: array<u32>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>>;
@group(1) @binding(0) var<storage, read> instances: array<Instance>;

var<workgroup> group_count: atomic<u32>;
var<workgroup> group_base: u32;

// Whether the world space box around an instance reaches into the frustum and is large enough.
// The size is the diameter of the sphere around the box in pixels, tested only when the box is
// in front of the camera (w beyond the radius); size.z is 0 for orthographic projections.
fn visible(job: Job, slot: u32) -> bool {
    let m = instances[slot].model;
    let c0 = m[0].xyz;
    let c1 = m[1].xyz;
    let c2 = m[2].xyz;
    let center = c0 * job.center.x + c1 * job.center.y + c2 * job.center.z + m[3].xyz;
    let extent = abs(c0) * job.extent.x + abs(c1) * job.extent.y + abs(c2) * job.extent.z;
    var result = true;
    for (var i = 0u; i < 6u; i++) {
        let p = cull.planes[i];
        if (dot(p.xyz, center) + p.w + dot(abs(p.xyz), extent) < 0.0) {
            result = false;
        }
    }

    let radius = length(extent);
    let w = dot(cull.clip_w, vec4f(center, 1.0));
    if (result && cull.size.y > 0.0 && w > radius * cull.size.z) {
        result = 2.0 * radius * cull.size.x >= cull.size.y * w;
    }

    return result;
}

@compute @workgroup_size(64)
fn cull_instances(
    @builtin(global_invocation_id) id: vec3u,
    @builtin(local_invocation_index) local: u32)
{
    let job = jobs[cull.jobs.x];
    if (local == 0u) {
        atomicStore(&group_count, 0u);
    }

    workgroupBarrier();
    let slot = job.first + id.x;
    let keep = id.x < job.count && visible(job, slot);
    var index = 0u;
    if (keep) {
        index = atomicAdd(&group_count, 1u);
    }

    workgroupBarrier();
    if (local == 0u) {
        let total = atomicLoad(&group_count);
        group_base = 0u;
        if (total > 0u) {
            group_base = atomicAdd(&args[job.args * ARGS + 1u], total);
        }
    }

    let base = workgroupUniformLoad(&group_base);
    if (keep) {
        visible_slots[job.base + base + index] = slot;
    }
}

@compute @workgroup_size(64)
fn cull_ordered(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= cull.jobs.y) {
        return;
    }

    let job = jobs[cull.jobs.x + id.x];
    var count = 0u;
    for (var i = 0u; i < job.count; i++) {
        if (visible(job, job.first + i)) {
            visible_slots[job.base + count] = job.first + i;
            count++;
        }
    }

    atomicStore(&args[job.args * ARGS + 1u], count);
}
