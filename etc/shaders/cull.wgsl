// Cull pass: tests the instances of the draws of a cull group, a view and the cascades of its
// shadows, against the frustum of each, its smallest projected size and, for a view, the depth
// pyramid of the geometry it drew first, and writes the slots of the instances to draw,
// compacted per draw, with the instance count of its indexed indirect draw arguments.

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
    // World position of the light of a shadow tile, w 1, w 0 for a frustum without a light.
    light: vec4f,
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
    // 1 for a batch of LightFixture shapes: a frustum with a light inside the world box of an
    // instance does not draw it.
    fixture: u32,
    // Page slot plus one of the instance no frustum of the group draws, the shape of a planar
    // reflection, 0 for none.
    exclude: u32,
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
// counters, then for a late entry with a history its early draw arguments plus one and first
// early visible entry, which it appends what it keeps to, else two zeros.
const ENTRY_WORDS: u32 = 8u;

// Words of a workgroup of the cull_instances list: job, first scan word of the job, first table
// entry of the job, the workgroup in the job.
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
@group(0) @binding(11) var hiz_dst1: texture_storage_2d<r32float, write>;
@group(0) @binding(12) var hiz_dst2: texture_storage_2d<r32float, write>;
@group(0) @binding(13) var hiz_dst3: texture_storage_2d<r32float, write>;

// Scan words per workgroup and frustum of a parallel dispatch: two mask words and the count,
// which cull_scan replaces with the offset of the workgroup.
const SCAN_WORDS: u32 = 3u;

var<workgroup> group_masks: array<atomic<u32>, 64>;
var<workgroup> group_culled: array<atomic<u32>, 32>;
var<workgroup> scan_sums: array<u32, 256>;
var<workgroup> hiz_tile: array<f32, 64>;

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

// Whether the light of a frustum is inside a world space box, inclusive; false for a frustum
// without a light.
fn holds(b: Bounds, f: u32) -> bool {
    let light = frustums[f].light;
    return light.w != 0.0 && all(abs(light.xyz - b.center) <= b.extent);
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
    let inside = visible(b, f) && slot + 1u != job.exclude;
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
    let entry = scan_words[at + 2u];
    let part = scan_words[at + 3u];
    let job_groups = scan_words[entry + 2u];
    let index = part * 64u + local;
    let word = local / 32u;
    let bit = local % 32u;
    var own = 0u;
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
        let fixture = job.fixture != 0u;
        while (bits != 0u) {
            let f = firstTrailingBit(bits);
            bits &= bits - 1u;
            var d = vec2<bool>(false, true);
            if (!fixture || !holds(b, f)) {
                d = decide(job, slot, m, b, f);
            }
            if (d.x) {
                drawn |= 1u << f;
            }

            if (d.y && phase_of(f) != PHASE_EARLY) {
                culled |= 1u << f;
            }
        }

        own = drawn;
        while (drawn != 0u) {
            let f = firstTrailingBit(drawn);
            drawn &= drawn - 1u;
            atomicOr(&group_masks[f * 2u + word], 1u << bit);
        }

        while (culled != 0u) {
            let f = firstTrailingBit(culled);
            culled &= culled - 1u;
            atomicAdd(&group_culled[f], 1u);
        }
    }

    workgroupBarrier();
    if (job_groups == 1u) {
        while (own != 0u) {
            let f = firstTrailingBit(own);
            own &= own - 1u;
            let k = countOneBits(job.mask & ((1u << f) - 1u));
            let low = atomicLoad(&group_masks[f * 2u]);
            let mask = select(atomicLoad(&group_masks[f * 2u + 1u]), low, local < 32u);
            let below = countOneBits(mask & ((1u << bit) - 1u)) +
                select(countOneBits(low), 0u, local < 32u);
            let e = entry + k * ENTRY_WORDS;
            visible_slots[scan_words[e + 4u] + below] = job.first + index;
            let early = scan_words[e + 6u];
            if (early != 0u) {
                let kept = atomicLoad(&args[(early - 1u) * ARGS + 1u]);
                visible_slots[scan_words[e + 7u] + kept + below] = job.first + index;
            }
        }
    }

    storageBarrier();
    if (local < GROUP_VIEWS && ((job.mask >> local) & 1u) != 0u) {
        let k = countOneBits(job.mask & ((1u << local) - 1u));
        let low = atomicLoad(&group_masks[local * 2u]);
        let high = atomicLoad(&group_masks[local * 2u + 1u]);
        let total = countOneBits(low) + countOneBits(high);
        if (job_groups == 1u) {
            let e = entry + k * ENTRY_WORDS;
            atomicStore(&args[scan_words[e + 3u] * ARGS + 1u], total);
            if (total > 0u) {
                atomicAdd(&counters[scan_words[e + 5u] * COUNTERS], total);
            }

            let early = scan_words[e + 6u];
            if (early != 0u && total > 0u) {
                atomicAdd(&args[(early - 1u) * ARGS + 1u], total);
            }
        } else {
            let words = scan + (k * job_groups + part) * SCAN_WORDS;
            scan_words[words] = low;
            scan_words[words + 1u] = high;
            scan_words[words + 2u] = total;
        }

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
        let early = scan_words[entry + 6u];
        if (early != 0u && total > 0u) {
            atomicAdd(&args[(early - 1u) * ARGS + 1u], total);
        }
    }
}

// Writes the drawn slots of the jobs of more than one workgroup of a table at their offsets, in
// slot order: a workgroup per workgroup of the cull_instances list of the table (scan.z, scan.w
// of them), over each frustum of its job.
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

    let at = cull.scan.z + g * LIST_WORDS;
    let entry = scan_words[at + 2u];
    let job_groups = scan_words[entry + 2u];
    let job = jobs[scan_words[at]];
    let part = scan_words[at + 3u];
    let index = part * 64u + local;
    if (job_groups == 1u || index >= job.count) {
        return;
    }

    let scan = scan_words[at + 1u];
    let bit = local % 32u;
    let frustums = countOneBits(job.mask);
    for (var k = 0u; k < frustums; k++) {
        let words = scan + (k * job_groups + part) * SCAN_WORDS;
        let low = scan_words[words];
        let mask = select(scan_words[words + 1u], low, local < 32u);
        if (((mask >> bit) & 1u) != 0u) {
            let below = countOneBits(mask & ((1u << bit) - 1u)) +
                select(countOneBits(low), 0u, local < 32u);
            let e = entry + k * ENTRY_WORDS;
            let offset = scan_words[words + 2u] + below;
            visible_slots[scan_words[e + 4u] + offset] = job.first + index;
            let early = scan_words[e + 6u];
            if (early != 0u) {
                let kept = atomicLoad(&args[(early - 1u) * ARGS + 1u]) -
                    atomicLoad(&args[scan_words[e + 3u] * ARGS + 1u]);
                visible_slots[scan_words[e + 7u] + kept + offset] = job.first + index;
            }
        }
    }
}

@compute @workgroup_size(64)
fn cull_ordered(
    @builtin(local_invocation_index) local: u32,
    @builtin(workgroup_id) group: vec3u,
    @builtin(num_workgroups) groups: vec3u)
{
    let j = group.y * groups.x + group.x;
    if (j >= cull.jobs.y) {
        return;
    }

    let job = jobs[cull.jobs.x + j];
    let f = cull.jobs.w;
    let bit = local % 32u;
    var count = 0u;
    if (local == 0u) {
        atomicStore(&group_culled[0], 0u);
    }

    for (var at = 0u; at < job.count; at += 64u) {
        atomicStore(&group_masks[local], 0u);
        workgroupBarrier();
        let i = at + local;
        var slot = 0u;
        var draw = false;
        if (i < job.count) {
            slot = job.first + i;
            if (job.list != 0u) {
                slot = slot_lists[job.first + i];
            }

            let m = instance_model(transforms[slot]);
            let d = decide(job, slot, m, bounds(job, m), f);
            draw = d.x;
            if (draw) {
                atomicOr(&group_masks[local / 32u], 1u << bit);
            }

            if (d.y) {
                atomicAdd(&group_culled[0], 1u);
            }
        }

        workgroupBarrier();
        let low = atomicLoad(&group_masks[0]);
        let high = atomicLoad(&group_masks[1]);
        if (draw) {
            let mask = select(high, low, local < 32u);
            let below = countOneBits(mask & ((1u << bit) - 1u)) +
                select(countOneBits(low), 0u, local < 32u);
            visible_slots[job.base + count + below] = slot;
        }

        count += countOneBits(low) + countOneBits(high);
        workgroupBarrier();
    }

    if (local == 0u) {
        let counter = frustums[f].flags.x * COUNTERS;
        atomicStore(&args[job.args * ARGS + 1u], count);
        atomicAdd(&counters[counter], count);
        atomicAdd(&counters[counter + 1u], atomicLoad(&group_culled[0]));
    }
}

fn hiz_next(local: vec2u, n: u32, below: vec2u) -> f32 {
    let inside = all(local < vec2u(n));
    var result = 1.0;
    if (inside) {
        let last = min(below, vec2u(n * 2u)) - 1u;
        let a = min(local * 2u, last);
        let b = min(local * 2u + 1u, last);
        result = min(min(hiz_tile[a.y * 8u + a.x], hiz_tile[a.y * 8u + b.x]),
            min(hiz_tile[b.y * 8u + a.x], hiz_tile[b.y * 8u + b.x]));
    }

    workgroupBarrier();
    if (inside) {
        hiz_tile[local.y * 8u + local.x] = result;
    }

    workgroupBarrier();
    return result;
}

fn hiz_rest(local: vec2u, group: vec2u, size: vec2u) {
    workgroupBarrier();
    let one = hiz_next(local, 4u, size);
    let at_one = group * 4u + local;
    if (all(local < vec2u(4u)) && all(at_one < textureDimensions(hiz_dst1))) {
        textureStore(hiz_dst1, at_one, vec4f(one, 0.0, 0.0, 0.0));
    }

    let two = hiz_next(local, 2u, textureDimensions(hiz_dst1));
    let at_two = group * 2u + local;
    if (all(local < vec2u(2u)) && all(at_two < textureDimensions(hiz_dst2))) {
        textureStore(hiz_dst2, at_two, vec4f(two, 0.0, 0.0, 0.0));
    }

    let three = hiz_next(local, 1u, textureDimensions(hiz_dst2));
    if (all(local == vec2u(0u)) && all(group < textureDimensions(hiz_dst3))) {
        textureStore(hiz_dst3, group, vec4f(three, 0.0, 0.0, 0.0));
    }
}

// First four levels of the pyramid from the depth of the early phase, two by two texels of the
// first level per invocation. The first level has a power of two size no larger than the depth,
// so a texel covers up to three by three depth pixels: every pixel its area touches.
@compute @workgroup_size(8, 8)
fn hiz_first(
    @builtin(local_invocation_id) local: vec3u,
    @builtin(workgroup_id) group: vec3u)
{
    let size = textureDimensions(hiz_dst);
    let depth = textureDimensions(hiz_depth);
    let id = group.xy * 8u + local.xy;
    let at = id * 2u;
    var texels = vec4f(1.0);
    if (all(at < size)) {
        let lo = at * depth / size;
        let split_lo = (at + 1u) * depth / size;
        let split_hi = min(((at + 1u) * depth + size - 1u) / size, depth);
        let hi = min(((at + 2u) * depth + size - 1u) / size, depth);
        for (var y = lo.y; y < hi.y; y++) {
            for (var x = lo.x; x < hi.x; x++) {
                let d = textureLoad(hiz_depth, vec2u(x, y), 0);
                let left = x < split_hi.x;
                let right = x >= split_lo.x;
                let top = y < split_hi.y;
                let bottom = y >= split_lo.y;
                texels.x = select(texels.x, min(texels.x, d), left && top);
                texels.y = select(texels.y, min(texels.y, d), right && top);
                texels.z = select(texels.z, min(texels.z, d), left && bottom);
                texels.w = select(texels.w, min(texels.w, d), right && bottom);
            }
        }

        textureStore(hiz_dst, at, vec4f(texels.x, 0.0, 0.0, 0.0));
        if (at.x + 1u < size.x) {
            textureStore(hiz_dst, at + vec2u(1u, 0u), vec4f(texels.y, 0.0, 0.0, 0.0));
        }

        if (at.y + 1u < size.y) {
            textureStore(hiz_dst, at + vec2u(0u, 1u), vec4f(texels.z, 0.0, 0.0, 0.0));
        }

        if (all(at + 1u < size)) {
            textureStore(hiz_dst, at + 1u, vec4f(texels.w, 0.0, 0.0, 0.0));
        }
    }

    let result = min(min(texels.x, texels.y), min(texels.z, texels.w));
    if (all(id < textureDimensions(hiz_dst1))) {
        textureStore(hiz_dst1, id, vec4f(result, 0.0, 0.0, 0.0));
    }

    hiz_tile[local.y * 8u + local.x] = result;
    workgroupBarrier();
    let two = hiz_next(local.xy, 4u, textureDimensions(hiz_dst1));
    let at_two = group.xy * 4u + local.xy;
    if (all(local.xy < vec2u(4u)) && all(at_two < textureDimensions(hiz_dst2))) {
        textureStore(hiz_dst2, at_two, vec4f(two, 0.0, 0.0, 0.0));
    }

    let three = hiz_next(local.xy, 2u, textureDimensions(hiz_dst2));
    let at_three = group.xy * 2u + local.xy;
    if (all(local.xy < vec2u(2u)) && all(at_three < textureDimensions(hiz_dst3))) {
        textureStore(hiz_dst3, at_three, vec4f(three, 0.0, 0.0, 0.0));
    }
}

// Next four levels of the pyramid: the farthest depth of the two by two texels below a texel,
// one by two once an axis is down to one texel.
@compute @workgroup_size(8, 8)
fn hiz_reduce(
    @builtin(local_invocation_id) local: vec3u,
    @builtin(workgroup_id) group: vec3u)
{
    let size = textureDimensions(hiz_dst);
    let id = group.xy * 8u + local.xy;
    var result = 1.0;
    if (all(id < size)) {
        let src = textureDimensions(hiz_src) - 1u;
        let a = min(id * 2u, src);
        let b = min(id * 2u + 1u, src);
        result = min(
            min(textureLoad(hiz_src, a, 0).r, textureLoad(hiz_src, vec2u(b.x, a.y), 0).r),
            min(textureLoad(hiz_src, vec2u(a.x, b.y), 0).r, textureLoad(hiz_src, b, 0).r));
        textureStore(hiz_dst, id, vec4f(result, 0.0, 0.0, 0.0));
    }

    hiz_tile[local.y * 8u + local.x] = result;
    hiz_rest(local.xy, group.xy, size);
}
