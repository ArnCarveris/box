// Cull pass: tests the instances of the draws of a cull group, a view and the cascades of its
// shadows, against the frustum of each, its smallest projected size and, for a view, the depth
// pyramid of the geometry it drew first, and writes the slots of the instances to draw,
// compacted per draw, with the instance count of its indexed indirect draw arguments.
//
// A cull group is up to 32 frustums, one bit each of a u32 (frustums, binding 7): its planes in
// world space, its smallest projected size, the index of its counters and its flags. A job is
// the slot range of one batch in its instance page, whose transform stream group 1 binds, and
// mask, the frustums of the group that draw it: every batch owns a run of slots of a page shared
// by many batches, so a job's first is its page slot. Its bounds are the box around the mesh,
// or around the parametric surfaces of its batch, moved into world space by the model matrix
// (local bounds times the instance scale and the world transform).
//
// A view that culls by occlusion runs two phases. The early phase draws the instances in the
// frustum that its history (group 2, one word per instance slot, 0 when the instance was
// drawn the previous frame) marks as drawn. The pyramid is reduced from the depth of that
// draw (hiz_first, hiz_reduce): with reversed Z a texel keeps the smallest, farthest depth of
// its footprint. The late phase tests every instance against the frustum and the pyramid,
// draws the visible ones the early phase did not draw and writes the history, so an instance
// that comes into view is drawn the same frame. The early phase of a group tests its cascades
// too, which have one phase that draws everything in the frustum; the late phase is a pass over
// the view alone. Groups without occlusion run one phase.
//
// cull_instances tests one instance per invocation: it loads the transform once, moves the box
// into world space once and tests it against the frustum of each bit of its job's mask, giving
// the mask of the frustums it is drawn in. The workgroup turns those into a 64 bit mask per
// frustum (two u32 atomics) with its count in the scan words of the (job, frustum) entry, and
// counts the frustum culled instances per frustum. A dispatch covers a run of the jobs of a
// table that share an instance page and a history: its part of the workgroup list (scan.z,
// scan.w workgroups) gives each workgroup its job, the first scan word and workgroups of the
// job and its workgroup in the job.
// One cull_scan dispatch per group, phase and page turns the counts of the workgroups of each of
// its (job, frustum) entries into exclusive offsets, in workgroup order, and writes their instance
// counts (a workgroup per entry, from a table of job, first scan word, workgroups, draw
// arguments, first visible entry and counters); one cull_write dispatch over the workgroups of
// all those entries (a list of their scan words and table entries) puts every drawn slot at the
// offset of its workgroup plus the drawn slots before it in the mask. The visible slots of each
// frustum keep slot order, so exact depth ties resolve the same way every frame (a prefix sum
// stream compaction, GPU Gems 3 chapter 39, in passes because WebGPU gives no forward progress
// guarantee for a single pass scan). cull_ordered tests one job per invocation against one
// frustum (jobs.w) in the order of its slots, for the blended draws of a view that are sorted
// back to front: their jobs list the slots (list is 1, first indexes slot_lists), so a run of
// one batch in the sorted order is one draw. All of them add the drawn and the frustum culled
// instances to the counters of each frustum.

#include "include/instance.wgsl"

struct Cull {
    jobs: vec4u,
    view_proj: mat4x4f,
    hiz: vec4u,
    scan: vec4u,
};

struct Frustum {
    planes: array<vec4f, 6>,
    clip_w: vec4f,
    size: vec4f,
    flags: vec4u,
};

struct Job {
    first: u32,
    count: u32,
    args: u32,
    base: u32,
    center: vec3f,
    history: u32,
    extent: vec3f,
    list: u32,
    mask: u32,
    pad: u32,
};

// World space box around an instance.
struct Bounds {
    center: vec3f,
    extent: vec3f,
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

// Frustums of a cull group at most, one bit each of a job's mask.
const GROUP_VIEWS: u32 = 32u;

// Flag of a frustum (flags.y) with an occlusion history: it runs the phase of the dispatch,
// the other frustums run one phase.
const FRUSTUM_HISTORY: u32 = 4u;

// Words of a table entry: job, first scan word, workgroups, draw arguments, first visible entry,
// counters, two unused.
const ENTRY_WORDS: u32 = 8u;

// Words of a workgroup of the cull_instances list: job, first scan word of the job, workgroups
// of the job, the workgroup in the job.
const LIST_WORDS: u32 = 4u;

@group(0) @binding(0) var<uniform> cull: Cull;
@group(0) @binding(1) var<storage, read> jobs: array<Job>;
@group(0) @binding(2) var<storage, read_write> visible_slots: array<u32>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> counters: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read> slot_lists: array<u32>;
@group(0) @binding(6) var<storage, read_write> scan_words: array<u32>;
@group(0) @binding(7) var<uniform> frustums: array<Frustum, 32>;
@group(1) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(1) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(0) var<storage, read_write> history: array<u32>;
@group(2) @binding(1) var pyramid: texture_2d<f32>;

@group(0) @binding(8) var hiz_depth: texture_depth_2d;
@group(0) @binding(9) var hiz_src: texture_2d<f32>;
@group(0) @binding(10) var hiz_dst: texture_storage_2d<r32float, write>;

// Scan words per workgroup and frustum of a parallel dispatch: two mask words and the count,
// which cull_scan replaces with the offset of the workgroup.
const SCAN_WORDS: u32 = 3u;

var<workgroup> group_masks: array<atomic<u32>, 64>;
var<workgroup> group_culled: array<atomic<u32>, 32>;
var<workgroup> scan_sums: array<u32, 256>;

// The box around the bounds of a job moved into world space by a model matrix.
fn bounds(job: Job, m: mat4x4f) -> Bounds {
    let local_center = job.center;
    let local_extent = job.extent;
    let c0 = m[0].xyz;
    let c1 = m[1].xyz;
    let c2 = m[2].xyz;
    let center = c0 * local_center.x + c1 * local_center.y + c2 * local_center.z + m[3].xyz;
    let extent = abs(c0) * local_extent.x + abs(c1) * local_extent.y + abs(c2) * local_extent.z;

    return Bounds(center, extent);
}

// Whether a world space box reaches into a frustum of the group and is large enough there.
// The size is the diameter of the sphere around the box in pixels, tested only when the box is
// in front of the camera (w beyond the radius); size.z is 0 for orthographic projections and
// size.y 0 for a frustum that does not test it.
fn visible(b: Bounds, f: u32) -> bool {
    var result = true;
    for (var i = 0u; i < 6u; i++) {
        let p = frustums[f].planes[i];
        if (dot(p.xyz, b.center) + p.w + dot(abs(p.xyz), b.extent) < 0.0) {
            result = false;
        }
    }

    let size = frustums[f].size;
    let radius = length(b.extent);
    let w = dot(frustums[f].clip_w, vec4f(b.center, 1.0));

    if (result && size.y > 0.0 && w > radius * size.z) {
        result = 2.0 * radius * size.x >= size.y * w;
    }

    return result;
}

// Whether the box of an instance is behind the depth pyramid. The eight corners of the box
// give its screen rectangle and its nearest depth (the largest with reversed Z); the finest
// level whose texels are a third of the rectangle covers it with at most four by four texels,
// and the instance is hidden when its nearest depth is farther than the farthest depth they
// hold. A box that reaches behind the camera or in front of the near plane is never hidden.
fn occluded(job: Job, m: mat4x4f) -> bool {
    let local_center = job.center;
    let local_extent = job.extent;
    let center = m[0].xyz * local_center.x + m[1].xyz * local_center.y + m[2].xyz * local_center.z +
        m[3].xyz;
    let origin = cull.view_proj * vec4f(center, 1.0);
    let dx = cull.view_proj * vec4f(m[0].xyz * local_extent.x, 0.0);
    let dy = cull.view_proj * vec4f(m[1].xyz * local_extent.y, 0.0);
    let dz = cull.view_proj * vec4f(m[2].xyz * local_extent.z, 0.0);
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

// Phase a frustum of the group runs in: the phase of the dispatch for the frustum with a
// history, the only phase for the others.
fn phase_of(f: u32) -> u32 {
    return select(PHASE_ALL, cull.jobs.z, (frustums[f].flags.y & FRUSTUM_HISTORY) != 0u);
}

// Whether a frustum draws an instance in its phase, and whether it is in the frustum. The late
// phase writes the history of a job that has one.
fn decide(job: Job, slot: u32, m: mat4x4f, b: Bounds, f: u32) -> vec2<bool> {
    let phase = phase_of(f);
    let inside = visible(b, f);
    var draw = inside;
    if (phase == PHASE_EARLY) {
        draw = inside && (job.history == NONE || history[job.history + slot] == 0u);
    } else if (phase == PHASE_LATE) {
        let now = inside && !occluded(job, m);
        draw = now;
        if (job.history != NONE) {
            draw = now && history[job.history + slot] != 0u;
            history[job.history + slot] = select(1u, 0u, now);
        }
    }

    return vec2<bool>(draw, !inside);
}

@compute @workgroup_size(64)
fn cull_instances(
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u,
    @builtin(num_workgroups) groups: vec3u)
{
    let g = group.y * groups.x + group.x;
    if (g >= cull.scan.w) {
        return;
    }

    let at = cull.scan.z + g * LIST_WORDS;
    let job = jobs[scan_words[at]];
    let scan = scan_words[at + 1u];
    let job_groups = scan_words[at + 2u];
    let part = scan_words[at + 3u];
    let index = part * 64u + local;
    atomicStore(&group_masks[local], 0u);
    if (local < GROUP_VIEWS) {
        atomicStore(&group_culled[local], 0u);
    }

    workgroupBarrier();
    if (index < job.count) {
        let slot = job.first + index;
        let m = instance_model(transforms[slot]);
        let b = bounds(job, m);
        var drawn = 0u;
        var culled = 0u;
        var bits = job.mask;
        while (bits != 0u) {
            let f = firstTrailingBit(bits);
            bits &= bits - 1u;
            let d = decide(job, slot, m, b, f);
            if (d.x) {
                drawn |= 1u << f;
            }

            if (d.y && phase_of(f) != PHASE_EARLY) {
                culled |= 1u << f;
            }
        }

        let word = local / 32u;
        let bit = 1u << (local % 32u);
        while (drawn != 0u) {
            let f = firstTrailingBit(drawn);
            drawn &= drawn - 1u;
            atomicOr(&group_masks[f * 2u + word], bit);
        }

        while (culled != 0u) {
            let f = firstTrailingBit(culled);
            culled &= culled - 1u;
            atomicAdd(&group_culled[f], 1u);
        }
    }

    workgroupBarrier();
    if (local < GROUP_VIEWS && ((job.mask >> local) & 1u) != 0u) {
        let k = countOneBits(job.mask & ((1u << local) - 1u));
        let words = scan + (k * job_groups + part) * SCAN_WORDS;
        let low = atomicLoad(&group_masks[local * 2u]);
        let high = atomicLoad(&group_masks[local * 2u + 1u]);
        scan_words[words] = low;
        scan_words[words + 1u] = high;
        scan_words[words + 2u] = countOneBits(low) + countOneBits(high);
        let culled = atomicLoad(&group_culled[local]);
        if (culled > 0u) {
            atomicAdd(&counters[frustums[local].flags.x * COUNTERS + 1u], culled);
        }
    }
}

// Exclusive offsets of the workgroups of the (job, frustum) entries of a table and their
// instance counts. A workgroup per entry (scan.y of them); its table entry at scan.x holds the
// first scan word and the workgroups of the entry, its draw arguments and its counters. Each
// invocation sums a contiguous run of the workgroup counts, the workgroup scans the 256 sums,
// and each invocation writes the offsets of its run.
@compute @workgroup_size(256)
fn cull_scan(
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u,
    @builtin(num_workgroups) groups: vec3u)
{
    let e = group.y * groups.x + group.x;
    if (e >= cull.scan.y) {
        return;
    }

    let entry = cull.scan.x + e * ENTRY_WORDS;
    let base = scan_words[entry + 1u];
    let work = scan_words[entry + 2u];
    let per = (work + 255u) / 256u;
    let first = min(local * per, work);
    let last = min(first + per, work);
    var own = 0u;
    for (var g = first; g < last; g++) {
        own += scan_words[base + g * SCAN_WORDS + 2u];
    }

    scan_sums[local] = own;
    workgroupBarrier();
    for (var step = 1u; step < 256u; step *= 2u) {
        var add = 0u;
        if (local >= step) {
            add = scan_sums[local - step];
        }

        workgroupBarrier();
        scan_sums[local] += add;
        workgroupBarrier();
    }

    var offset = scan_sums[local] - own;
    for (var g = first; g < last; g++) {
        let count = scan_words[base + g * SCAN_WORDS + 2u];
        scan_words[base + g * SCAN_WORDS + 2u] = offset;
        offset += count;
    }

    if (local == 255u) {
        let total = scan_sums[255];
        atomicStore(&args[scan_words[entry + 3u] * ARGS + 1u], total);
        atomicAdd(&counters[scan_words[entry + 5u] * COUNTERS], total);
    }
}

// Writes the drawn slots of the (job, frustum) entries of a table at their offsets, in slot
// order. The workgroups of all its entries are listed at scan.z (scan.w of them) as their scan
// words and their table entry.
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
        visible_slots[scan_words[entry + 4u] + scan_words[at + 2u] + below] = job.first + index;
    }
}

@compute @workgroup_size(64)
fn cull_ordered(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= cull.jobs.y) {
        return;
    }

    let job = jobs[cull.jobs.x + id.x];
    let f = cull.jobs.w;
    var count = 0u;
    var culled = 0u;
    for (var i = 0u; i < job.count; i++) {
        var slot = job.first + i;
        if (job.list != 0u) {
            slot = slot_lists[job.first + i];
        }

        let m = instance_model(transforms[slot]);
        let d = decide(job, slot, m, bounds(job, m), f);
        if (d.x) {
            visible_slots[job.base + count] = slot;
            count++;
        }

        if (d.y) {
            culled++;
        }
    }

    let counter = frustums[f].flags.x * COUNTERS;
    atomicStore(&args[job.args * ARGS + 1u], count);
    atomicAdd(&counters[counter], count);
    atomicAdd(&counters[counter + 1u], culled);
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
