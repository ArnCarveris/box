// Grass place pass (gpu_grass.c): per Grass and view a reset, a place dispatch with a workgroup
// per patch of a grid around the camera and a finish.
//
// A patch outside the frustum or beyond the last distance places nothing. A visible one walks
// the blocks of 2^k cells along each edge whose first cell it holds, k the thinning level at its
// nearest point: each block picks one of its cells by a hash of the absolute block, recursively
// through the blocks of the levels below, so the blades of a coarser level are a subset of those
// of a finer one. A blade stands for the blocks above that picked it too (its level) and shrinks
// away as the thinning passes its level, while every blade widens by the share of blades the
// thinning dropped, so the coverage holds. Its ground is the height of the terrain
// (terrain_sample.wgsl) or the plane, and a mask channel of the terrain weighs whether it grows.
// A blade takes the facing, height and lean of the clump it is in, the nearest center of a
// voronoi pattern of absolute cells of a clump edge, as much as the clump follow says, and the
// color and height of the variation over the world at its absolute place (grass_field.wgsl). A
// blade thinner than a pixel at its distance widens to the pixel and stays only with the share
// of blades that keeps the coverage, chosen by its hash.
// Blades outside the frustum are dropped, the others appended to the range of their level of
// detail.
#include "include/terrain_sample.wgsl"
#include "include/grass.wgsl"
#include "include/grass_field.wgsl"

// The indirect draw arguments of each level, vertex count, instance count, first vertex and
// first instance, then the blades placed per level (box_gpu_grass_counters_t)
struct GrassCounters {
    args: array<atomic<u32>, 12>,
    generated: array<atomic<u32>, 3>,
    pad: u32,
};

// The blades placed and drawn per level over the views of a packet (box_gpu_grass_stats_t)
struct GrassStats {
    generated: array<atomic<u32>, 3>,
    drawn: array<atomic<u32>, 3>,
    pad: vec2u,
};

@group(1) @binding(0) var<uniform> params: GrassParams;
@group(1) @binding(1) var<storage, read_write> blades: array<GrassBlade>;
@group(1) @binding(2) var<storage, read_write> counters: GrassCounters;
@group(1) @binding(3) var<storage, read_write> stats: GrassStats;

// Invocations of a workgroup of the place dispatch (BOX_GPU_GRASS_GROUP)
const GROUP: u32 = 64u;

// Salts of the hashes of a cell
const SALT_PICK: u32 = 0x51a7u;
const SALT_PLACE: u32 = 0x3c6eu;
const SALT_CLUMP: u32 = 0x9e37u;

// Narrowest a blade draws, in pixels
const MIN_PIXELS: f32 = 1.0;

// Share of the turn toward the facing of its clump a blade of a fully following clump takes
const CLUMP_FACING: f32 = 0.85;

// Largest factor of the height of the blades of a clump
const CLUMP_HEIGHT: f32 = 0.35;

const TAU: f32 = 6.283185307179586;

// The cell the block of 2^k cells along each edge whose first cell is base picks for its blade.
fn grass_pick(base: vec2i, k: u32) -> vec2i {
    var c = base;
    for (var j = k; j > 0u; j--) {
        let q = grass_cell_hash(c >> vec2u(j), SALT_PICK + j, params.config.z) & 3u;
        c += vec2i(i32(q & 1u), i32(q >> 1u)) << vec2u(j - 1u);
    }
    return c;
}

// The level of the blade of a cell picked by its block of level k: the highest level up to which
// every block above picks the block that holds it.
fn grass_level(c: vec2i, k: u32) -> u32 {
    var level = k;
    for (var j = k + 1u; j <= GRASS_THIN_LEVELS; j++) {
        let q = grass_cell_hash(c >> vec2u(j), SALT_PICK + j, params.config.z) & 3u;
        let child = (c >> vec2u(j - 1u)) & vec2i(1);
        if (q != u32(child.x) + 2u * u32(child.y)) {
            break;
        }
        level = j;
    }
    return level;
}

// Thinning at a distance: 0 up to the distance it starts at, plus one per doubling beyond.
fn grass_thin(d: f32) -> f32 {
    let start = max(params.ground.w, 1e-3);
    return log2(max(d, start) / start);
}

// The side planes of the frustum of the view, inward, normalized.
fn grass_planes() -> array<vec4f, 4> {
    let m = params.view_proj;
    let r0 = vec4f(m[0].x, m[1].x, m[2].x, m[3].x);
    let r1 = vec4f(m[0].y, m[1].y, m[2].y, m[3].y);
    let r3 = vec4f(m[0].w, m[1].w, m[2].w, m[3].w);
    var planes = array<vec4f, 4>(r3 + r0, r3 - r0, r3 + r1, r3 - r1);
    for (var i = 0u; i < 4u; i++) {
        planes[i] = planes[i] / max(length(planes[i].xyz), 1e-12);
    }
    return planes;
}

// Whether a box is at least partly inside the side planes.
fn grass_box_visible(planes: array<vec4f, 4>, lo: vec3f, hi: vec3f) -> bool {
    for (var i = 0u; i < 4u; i++) {
        let p = planes[i];
        let v = select(lo, hi, p.xyz >= vec3f(0.0));
        if (dot(p.xyz, v) + p.w < 0.0) {
            return false;
        }
    }
    return true;
}

// Whether a sphere is at least partly inside the side planes.
fn grass_sphere_visible(planes: array<vec4f, 4>, c: vec3f, r: f32) -> bool {
    for (var i = 0u; i < 4u; i++) {
        if (dot(planes[i].xyz, c) + planes[i].w < -r) {
            return false;
        }
    }
    return true;
}

// Distance from the camera to the nearest point of a box.
fn grass_box_distance(lo: vec3f, hi: vec3f) -> f32 {
    let p = params.camera.xyz;
    return length(max(max(lo - p, p - hi), vec3f(0.0)));
}

// The smallest multiple of 2^k at or above a cell, per axis.
fn grass_align_up(a: vec2i, k: u32) -> vec2i {
    let s = (1 << k) - 1;
    return ((a + vec2i(s)) >> vec2u(k)) << vec2u(k);
}

// The clump of a blade: the hash of the nearest center of a voronoi pattern over the world and
// the distance to it in clump edges.
struct GrassClump {
    hash: u32,
    distance: f32,
};

// The clump of an absolute world x and z.
fn grass_clump(xz: vec2f) -> GrassClump {
    let size = max(params.shape.x, 1e-3);
    let q = xz / size;
    let cell = vec2i(floor(q));
    var out = GrassClump(0u, 1e9);
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let n = cell + vec2i(x, y);
            let h = grass_cell_hash(n, SALT_CLUMP, params.config.z);
            let center = vec2f(n) + vec2f(grass_unit(h), grass_unit(grass_hash(h)));
            let d = length(q - center);
            if (d < out.distance) {
                out = GrassClump(h, d);
            }
        }
    }
    return out;
}

// The angle from a to b, -PI to PI.
fn grass_turn(a: f32, b: f32) -> f32 {
    let d = b - a;
    return d - TAU * round(d / TAU);
}

// A blade the place dispatch made of a cell.
struct GrassPlaced {
    generated: bool,    // It stands on the ground within the distance and the mask grows it
    keep: bool,         // It is visible and has not shrunk away
    lod: u32,           // Its level of detail
    blade: GrassBlade,
};

// Make the blade of a cell picked by its block of level k, at an offset from the corner of its
// patch in meters.
fn grass_blade(at_patch: vec2i, lo: vec2f, c: vec2i, first: vec2i, k: u32,
    planes: array<vec4f, 4>) -> GrassPlaced
{
    var out: GrassPlaced;
    let seed = params.config.z;
    let h0 = grass_cell_hash(c, SALT_PLACE, seed);
    let h1 = grass_hash(h0);
    let h2 = grass_hash(h1);
    let h3 = grass_hash(h2);
    let h4 = grass_hash(h3);
    let h5 = grass_hash(h4);
    let h6 = grass_hash(h5);
    let h7 = grass_hash(h6);
    let h8 = grass_hash(h7);
    let h9 = grass_hash(h8);
    let h10 = grass_hash(h9);
    let offset = (vec2f(c - first) + vec2f(grass_unit(h0), grass_unit(h1))) * params.corner.w;
    var y = params.ground.z;
    var weight = 1.0;
    if (params.config.y != 0u) {
        let tile = terrain_sample_find(at_patch * i32(params.corner.z), offset);
        if (!tile.found) {
            return out;
        }

        y = terrain_sample_tile_height(tile) + terrain_sample_params.origin.w;
        if (params.config.x != 0u && terrain_sample_params.strides.z != 0u) {
            weight = terrain_sample_tile_mask(tile)[params.config.x - 1u];
        }
    }

    if (grass_unit(h2) >= weight) {
        return out;
    }

    let xz = lo + offset;
    let base = vec3f(xz.x, y, xz.y);
    let d = length(base - params.camera.xyz);
    if (d >= params.camera.w) {
        return out;
    }

    out.generated = true;
    out.lod = select(select(2u, 1u, d < params.lod.y), 0u, d < params.lod.x);
    let level = grass_level(c, k);
    let t = grass_thin(d);
    var fade = 1.0 - smoothstep(params.lod.w, params.camera.w, d);
    if (level < GRASS_THIN_LEVELS) {
        fade *= clamp((f32(level) + 1.0 - t) * 2.0, 0.0, 1.0);
    }

    // The clump and the variation over the world at the absolute place of the blade
    let world = (vec2f(c) + vec2f(grass_unit(h0), grass_unit(h1))) * params.corner.w;
    let clump = grass_clump(world);
    let follow = saturate(params.shape.y);
    let ch1 = grass_hash(clump.hash ^ SALT_CLUMP);
    let ch2 = grass_hash(ch1);
    let ch3 = grass_hash(ch2);
    let ch4 = grass_hash(ch3);
    let variation = grass_field_variation(world, params.shape.z);

    // Taller blades toward the center of a clump, all of them as tall as their clump
    let tall = mix(grass_unit(h3), grass_unit(ch1), follow) *
        mix(1.0, 1.0 - CLUMP_HEIGHT * saturate(clump.distance), follow);
    var height = mix(params.blade.x, params.blade.y, tall) * variation.w *
        mix(0.7, 1.0, weight) * fade;
    let widen = min(exp2(2.0 * t), GRASS_WIDEN_MAX);
    var width = mix(params.blade.z, params.blade.w, grass_unit(h4)) * widen;

    // A blade thinner than a pixel widens to it, and only the share of blades that keeps the
    // coverage stays, the others shrinking away over a short band of the share
    let pixel = MIN_PIXELS * grass_pixel(d);
    if (width < pixel) {
        let share = width / pixel;
        height *= saturate((share - grass_unit(h10)) * 8.0);
        width = pixel;
    }

    if (height < 0.05 * params.blade.x) {
        return out;
    }

    let center = base + vec3f(0.0, 0.5 * height, 0.0);
    if (!grass_sphere_visible(planes, center, 0.6 * height + width)) {
        return out;
    }

    // The facing, lean and shade of the clump
    let clump_yaw = grass_unit(ch2) * TAU;
    let own_yaw = grass_unit(h5) * TAU;
    let yaw = own_yaw + grass_turn(own_yaw, clump_yaw) * follow * CLUMP_FACING;
    let lean = mix(grass_unit(h6), grass_unit(ch3), follow);
    let shade = mix(0.75, 1.1, mix(grass_unit(h7), grass_unit(ch4), 0.5 * follow));
    let tint = pack4x8unorm(vec4f(saturate(0.5 * variation.rgb), 0.0));

    out.keep = true;
    out.blade = GrassBlade(base, yaw, height, width, lean, shade, grass_unit(h8),
        grass_unit(h9) * 2.0 - 1.0, tint, clump.distance);
    return out;
}

// Write the vertex counts of the levels and zero their blades.
@compute @workgroup_size(1)
fn reset() {
    for (var l = 0u; l < GRASS_LODS; l++) {
        atomicStore(&counters.args[l * 4u], grass_vertices(l));
        atomicStore(&counters.args[l * 4u + 1u], 0u);
        atomicStore(&counters.args[l * 4u + 2u], l * GRASS_LOD_VERTICES);
        atomicStore(&counters.args[l * 4u + 3u], 0u);
        atomicStore(&counters.generated[l], 0u);
    }
}

// Place the blades of a patch of the grid.
@compute @workgroup_size(64)
fn place(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) local: u32) {
    let n = u32(params.patches.z);
    if (wg.x >= n || wg.y >= n) {
        return;
    }

    let cells = params.patches.w;
    let size = params.corner.z;
    let cell = params.corner.w;
    let index = vec2i(wg.xy);
    let at_patch = params.patches.xy + index;
    let lo = params.corner.xy + vec2f(index) * size;
    let y0 = params.ground.x;
    let y1 = params.ground.y + params.blade.y;
    let near_ = grass_box_distance(vec3f(lo.x, y0, lo.y), vec3f(lo.x + size, y1, lo.y + size));
    let k0 = u32(clamp(floor(grass_thin(near_)), 0.0, f32(GRASS_THIN_LEVELS)));
    let reach = size + f32(1 << k0) * cell;
    let lo3 = vec3f(lo.x, y0, lo.y);
    let hi3 = vec3f(lo.x + reach, y1, lo.y + reach);
    let d = grass_box_distance(lo3, hi3);
    let planes = grass_planes();
    let margin = params.blade.y + params.blade.w * GRASS_WIDEN_MAX;
    if (d >= params.camera.w ||
        !grass_box_visible(planes, lo3 - vec3f(margin), hi3 + vec3f(margin)))
    {
        return;
    }

    let k = u32(clamp(floor(grass_thin(d)), 0.0, f32(GRASS_THIN_LEVELS)));
    let s = 1 << k;
    let first = at_patch * cells;
    let start = grass_align_up(first, k);
    let end = first + vec2i(cells);
    let span = select(vec2i(0), (end - vec2i(1) - start) / s + vec2i(1), start < end);
    let total = u32(span.x * span.y);
    let columns = u32(max(span.x, 1));
    let capacity = params.config.w;
    for (var i = local; i < total; i += GROUP) {
        let b = start + vec2i(i32(i % columns), i32(i / columns)) * s;
        let c = grass_pick(b, k);
        let made = grass_blade(at_patch, lo, c, first, k, planes);
        if (made.generated) {
            atomicAdd(&counters.generated[made.lod], 1u);
        }
        if (made.keep) {
            let at_level = atomicAdd(&counters.args[made.lod * 4u + 1u], 1u);
            if (at_level < capacity) {
                blades[made.lod * capacity + at_level] = made.blade;
            }
        }
    }
}

// Clamp the blades of the levels to what they hold and add the counts to those of the packet.
@compute @workgroup_size(1)
fn finish() {
    let capacity = params.config.w;
    for (var l = 0u; l < GRASS_LODS; l++) {
        let drawn = min(atomicLoad(&counters.args[l * 4u + 1u]), capacity);
        atomicStore(&counters.args[l * 4u + 1u], drawn);
        atomicAdd(&stats.drawn[l], drawn);
        atomicAdd(&stats.generated[l], atomicLoad(&counters.generated[l]));
    }
}
