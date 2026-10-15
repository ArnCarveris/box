// Cull pass: tests the instances of the draws of a view, or of a cascade of its shadows,
// against its frustum, its smallest projected size and, for a view, the depth pyramid of the
// geometry it drew first, and writes the slots of the instances to draw, compacted per draw,
// with the instance count of its indexed indirect draw arguments.
//
// A job is the slot range of one draw in the instance buffer bound at group 1. Its bounds are
// the box around the mesh, moved into world space by the model matrix (mesh bounds times the
// instance scale and the world transform); the planes are the frustum in world space.
//
// A view that culls by occlusion runs two phases. The early phase draws the instances in the
// frustum that its history (group 2, one word per instance slot, 0 when the instance was
// drawn the previous frame) marks as drawn. The pyramid is reduced from the depth of that
// draw (hiz_first, hiz_reduce): with reversed Z a texel keeps the smallest, farthest depth of
// its footprint. The late phase tests every instance against the frustum and the pyramid,
// draws the visible ones the early phase did not draw and writes the history, so an instance
// that comes into view is drawn the same frame. Cascades and views without occlusion run one
// phase that draws everything in the frustum.
//
// cull_instances tests one instance per invocation of one job and records which instances of
// its workgroup it draws as a 64 bit mask with their count in the scan words of the dispatch.
// One cull_scan dispatch per cull view, phase and page turns the counts of the workgroups of each
// of its parallel draws into exclusive offsets, in workgroup order, and writes their instance
// counts (a workgroup per draw, from a table of job, first scan word and workgroups); one
// cull_write dispatch over the workgroups of all those draws (a list of their scan words and
// table entries) puts every drawn slot at the offset of its workgroup plus the drawn slots before
// it in the mask. The visible slots keep slot order, so
// exact depth ties resolve the same way every frame (a prefix sum stream compaction, GPU Gems 3
// chapter 39, in passes because WebGPU gives no forward progress guarantee for a single pass
// scan). cull_ordered tests one job per invocation in the order of its slots, for the blended
// draws that are sorted back to front: their jobs list the slots (list is 1, first indexes
// slot_lists), so a run of one batch in the sorted order is one draw. Both add the drawn and
// the frustum culled instances to the counters of the cull view.

#include "include/instance.wgsl"

struct Cull {
    planes: array<vec4f, 6>,
    clip_w: vec4f,
    size: vec4f,
    jobs: vec4u,
    view_proj: mat4x4f,
    hiz: vec4u,
    scan: vec4u,
};

struct Job {
    first: u32,
    count: u32,
    args: u32,
    base: u32,
    center: vec3f,
    history: u32,
    extent: vec3f,
    late: u32,
    list: u32,
};

// Words of the indexed indirect draw arguments of a draw; the instance count is the second.
const ARGS: u32 = 5u;

// Words of the counters of a cull view: drawn, frustum culled and two unused.
const COUNTERS: u32 = 4u;

// Job without history: a blended draw, or a draw of a view without occlusion.
const NONE: u32 = 0xffffffffu;

// Phases in jobs.z: one phase without occlusion, the early and the late phase with it.
const PHASE_ALL: u32 = 0u;
const PHASE_EARLY: u32 = 1u;
const PHASE_LATE: u32 = 2u;

@group(0) @binding(0) var<uniform> cull: Cull;
@group(0) @binding(1) var<storage, read> jobs: array<Job>;
@group(0) @binding(2) var<storage, read_write> visible_slots: array<u32>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> counters: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read> slot_lists: array<u32>;
@group(0) @binding(6) var<storage, read_write> scan_words: array<u32>;
@group(1) @binding(0) var<storage, read> instances: array<Instance>;
@group(2) @binding(0) var<storage, read_write> history: array<u32>;
@group(2) @binding(1) var pyramid: texture_2d<f32>;

@group(0) @binding(8) var hiz_depth: texture_depth_2d;
@group(0) @binding(9) var hiz_src: texture_2d<f32>;
@group(0) @binding(10) var hiz_dst: texture_storage_2d<r32float, write>;

// Scan words per workgroup of a parallel dispatch: two mask words and the count, which
// cull_scan replaces with the offset of the workgroup.
const SCAN_WORDS: u32 = 3u;

var<workgroup> group_mask: array<atomic<u32>, 2>;
var<workgroup> group_culled: atomic<u32>;
var<workgroup> scan_sums: array<u32, 64>;
var<workgroup> scan_carry: u32;
var<workgroup> scan_groups: u32;

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

// Whether the box of an instance is behind the depth pyramid. The eight corners of the box
// give its screen rectangle and its nearest depth (the largest with reversed Z); the finest
// level whose texels are a third of the rectangle covers it with at most four by four texels,
// and the instance is hidden when its nearest depth is farther than the farthest depth they
// hold. A box that reaches behind the camera or in front of the near plane is never hidden.
fn occluded(job: Job, slot: u32) -> bool {
    let m = instances[slot].model;
    let center = m[0].xyz * job.center.x + m[1].xyz * job.center.y + m[2].xyz * job.center.z +
        m[3].xyz;
    let origin = cull.view_proj * vec4f(center, 1.0);
    let dx = cull.view_proj * vec4f(m[0].xyz * job.extent.x, 0.0);
    let dy = cull.view_proj * vec4f(m[1].xyz * job.extent.y, 0.0);
    let dz = cull.view_proj * vec4f(m[2].xyz * job.extent.z, 0.0);
    var lo = vec2f(1.0);
    var hi = vec2f(0.0);
    var depth = 0.0;
    var crossed = false;
    for (var i = 0u; i < 8u; i++) {
        let p = origin + select(-dx, dx, (i & 1u) != 0u) + select(-dy, dy, (i & 2u) != 0u) +
            select(-dz, dz, (i & 4u) != 0u);
        if (p.w <= 1e-6) {
            crossed = true;
        } else {
            let ndc = p.xyz / p.w;
            let uv = vec2f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
            lo = min(lo, uv);
            hi = max(hi, uv);
            depth = max(depth, ndc.z);
        }
    }

    lo = clamp(lo, vec2f(0.0), vec2f(1.0));
    hi = clamp(hi, vec2f(0.0), vec2f(1.0));
    if (crossed || depth >= 1.0 || any(hi < lo) || cull.hiz.z == 0u) {
        return false;
    }

    let size = vec2f(cull.hiz.xy);
    let span = max((hi.x - lo.x) * size.x, (hi.y - lo.y) * size.y);
    let level = min(u32(max(ceil(log2(span / 3.0)), 0.0)), cull.hiz.z - 1u);
    let dims = max(cull.hiz.xy >> vec2u(level), vec2u(1u));
    let a = min(vec2u(lo * vec2f(dims)), dims - 1u);
    let b = min(vec2u(hi * vec2f(dims)), dims - 1u);
    if (any(b - a > vec2u(3u))) {
        return false;
    }

    var far = 1.0;
    for (var y = a.y; y <= b.y; y++) {
        for (var x = a.x; x <= b.x; x++) {
            far = min(far, textureLoad(pyramid, vec2u(x, y), i32(level)).r);
        }
    }

    return depth < far;
}

// Whether a phase draws an instance, and whether it is in the frustum. The late phase writes
// the history of a job that has one.
fn decide(job: Job, slot: u32) -> vec2<bool> {
    let phase = cull.jobs.z;
    let inside = visible(job, slot);
    var draw = inside;
    if (phase == PHASE_EARLY) {
        draw = inside && history[job.history + slot] == 0u;
    } else if (phase == PHASE_LATE) {
        let now = inside && !occluded(job, slot);
        draw = now;
        if (job.history != NONE) {
            draw = now && history[job.history + slot] != 0u;
            history[job.history + slot] = select(1u, 0u, now);
        }
    }

    return vec2<bool>(draw, !inside);
}

// Indirect draw arguments a phase writes: the late ones of an opaque draw in the late phase.
fn args_of(job: Job) -> u32 {
    return select(job.args, job.late, cull.jobs.z == PHASE_LATE && job.late != NONE);
}

// First entry of the visible slots a phase writes.
fn base_of(job: Job) -> u32 {
    return select(job.base, job.base + job.count,
        cull.jobs.z == PHASE_LATE && job.late != NONE);
}

@compute @workgroup_size(64)
fn cull_instances(
    @builtin(global_invocation_id) id: vec3u,
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u)
{
    let job = jobs[cull.jobs.x];
    if (local == 0u) {
        atomicStore(&group_mask[0], 0u);
        atomicStore(&group_mask[1], 0u);
        atomicStore(&group_culled, 0u);
    }

    workgroupBarrier();
    if (id.x < job.count) {
        let d = decide(job, job.first + id.x);
        if (d.x) {
            atomicOr(&group_mask[local / 32u], 1u << (local % 32u));
        }

        if (d.y && cull.jobs.z != PHASE_EARLY) {
            atomicAdd(&group_culled, 1u);
        }
    }

    workgroupBarrier();
    if (local == 0u) {
        let low = atomicLoad(&group_mask[0]);
        let high = atomicLoad(&group_mask[1]);
        let at = cull.scan.x + group.x * SCAN_WORDS;
        scan_words[at] = low;
        scan_words[at + 1u] = high;
        scan_words[at + 2u] = countOneBits(low) + countOneBits(high);
        let culled = atomicLoad(&group_culled);
        if (culled > 0u) {
            atomicAdd(&counters[cull.jobs.w * COUNTERS + 1u], culled);
        }
    }
}

// Exclusive offsets of the workgroups of the parallel draws of a table, 64 at a time, and their
// instance counts. A workgroup per draw; its table entry at scan.x holds the job, the first scan
// word and the workgroups of the draw.
@compute @workgroup_size(64)
fn cull_scan(
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u)
{
    let entry = cull.scan.x + group.x * 4u;
    let job = jobs[scan_words[entry]];
    let base = scan_words[entry + 1u];
    if (local == 0u) {
        scan_carry = 0u;
        scan_groups = scan_words[entry + 2u];
    }

    let groups = workgroupUniformLoad(&scan_groups);
    for (var start = 0u; start < groups; start += 64u) {
        let g = start + local;
        var own = 0u;
        if (g < groups) {
            own = scan_words[base + g * SCAN_WORDS + 2u];
        }

        scan_sums[local] = own;
        workgroupBarrier();
        for (var step = 1u; step < 64u; step *= 2u) {
            var add = 0u;
            if (local >= step) {
                add = scan_sums[local - step];
            }

            workgroupBarrier();
            scan_sums[local] += add;
            workgroupBarrier();
        }

        let carry = scan_carry;
        if (g < groups) {
            scan_words[base + g * SCAN_WORDS + 2u] = carry + scan_sums[local] - own;
        }

        workgroupBarrier();
        if (local == 63u) {
            scan_carry = carry + scan_sums[63];
        }

        workgroupBarrier();
    }

    if (local == 0u) {
        let total = scan_carry;
        atomicStore(&args[args_of(job) * ARGS + 1u], total);
        atomicAdd(&counters[cull.jobs.w * COUNTERS], total);
    }
}

// Writes the drawn slots of the parallel draws of a table at their offsets, in slot order. The
// workgroups of all its draws are listed at scan.z (scan.w of them) as their first scan word and
// their table entry.
@compute @workgroup_size(64)
fn cull_write(
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u,
    @builtin(num_workgroups) groups: vec3u)
{
    let g = group.y * groups.x + group.x;
    if (g >= cull.scan.w) {
        return;
    }

    let at = scan_words[cull.scan.z + g * 2u];
    let entry = scan_words[cull.scan.z + g * 2u + 1u];
    let job = jobs[scan_words[entry]];
    let index = (at - scan_words[entry + 1u]) / SCAN_WORDS * 64u + local;
    let low = scan_words[at];
    let mask = select(scan_words[at + 1u], low, local < 32u);
    let bit = local % 32u;
    if (index < job.count && ((mask >> bit) & 1u) != 0u) {
        let below = countOneBits(mask & ((1u << bit) - 1u)) +
            select(countOneBits(low), 0u, local < 32u);
        visible_slots[base_of(job) + scan_words[at + 2u] + below] = job.first + index;
    }
}

@compute @workgroup_size(64)
fn cull_ordered(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= cull.jobs.y) {
        return;
    }

    let job = jobs[cull.jobs.x + id.x];
    var count = 0u;
    var culled = 0u;
    for (var i = 0u; i < job.count; i++) {
        var slot = job.first + i;
        if (job.list != 0u) {
            slot = slot_lists[job.first + i];
        }

        let d = decide(job, slot);
        if (d.x) {
            visible_slots[job.base + count] = slot;
            count++;
        }

        if (d.y) {
            culled++;
        }
    }

    atomicStore(&args[job.args * ARGS + 1u], count);
    atomicAdd(&counters[cull.jobs.w * COUNTERS], count);
    atomicAdd(&counters[cull.jobs.w * COUNTERS + 1u], culled);
}

// First level of the pyramid from the depth of the early phase. A level has a power of two
// size no larger than the depth, so a texel covers up to three by three depth pixels: every
// pixel its area touches.
@compute @workgroup_size(8, 8)
fn hiz_first(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(hiz_dst);
    if (any(id.xy >= size)) {
        return;
    }

    let depth = textureDimensions(hiz_depth);
    let lo = id.xy * depth / size;
    let hi = min(((id.xy + 1u) * depth + size - 1u) / size, depth);
    var result = 1.0;
    for (var y = lo.y; y < hi.y; y++) {
        for (var x = lo.x; x < hi.x; x++) {
            result = min(result, textureLoad(hiz_depth, vec2u(x, y), 0));
        }
    }

    textureStore(hiz_dst, id.xy, vec4f(result, 0.0, 0.0, 0.0));
}

// Next level of the pyramid: the farthest depth of the two by two texels below a texel, one by
// two once an axis is down to one texel.
@compute @workgroup_size(8, 8)
fn hiz_reduce(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(hiz_dst);
    if (any(id.xy >= size)) {
        return;
    }

    let src = textureDimensions(hiz_src) - 1u;
    let a = min(id.xy * 2u, src);
    let b = min(id.xy * 2u + 1u, src);
    let result = min(
        min(textureLoad(hiz_src, a, 0).r, textureLoad(hiz_src, vec2u(b.x, a.y), 0).r),
        min(textureLoad(hiz_src, vec2u(a.x, b.y), 0).r, textureLoad(hiz_src, b, 0).r));
    textureStore(hiz_dst, id.xy, vec4f(result, 0.0, 0.0, 0.0));
}
