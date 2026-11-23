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
// of blades that keeps the coverage, chosen by its hash. Its stiffness in the wind is its own
// and its clump's, the phase of its swing that of its clump, so neighbouring clumps do not move
// in lockstep.
// Blades outside the frustum are dropped, the others take their bend in the wind of the frame
// (grass_wind_bend), which every vertex of the draws reads, and are appended to the range of
// their level of detail.
//
// A view that culls by occlusion places its blades after its depth pyramid (gpu_hiz.c) is built
// from the depth of what it drew first this frame, from its camera of this frame, so nothing it
// hides lags behind the camera (hiz.z levels, 0 for none). A patch whose box is behind the
// pyramid places nothing, its ground bounded by the heights of the terrain on a grid over the
// patch, when it has enough blades to pay for the grid; a blade whose bound is behind it is
// dropped. Both count in the stats of the packet.
//
// The placement of the blades that cast shadows (cast.w) keeps the blades within the distance
// they cast within whose shadow falls into the frustum: each side plane moves out along the
// light by as far as the shadows reach (cast.z), and a view projection of zero keeps every blade.
// Its counts do not add to those of the packet.
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

// The blades placed and drawn per level over the views of a packet, then the patches and the
// blades culled by occlusion (box_gpu_grass_stats_t)
struct GrassStats {
    generated: array<atomic<u32>, 3>,
    drawn: array<atomic<u32>, 3>,
    culled: array<atomic<u32>, 2>,
};

@group(1) @binding(0) var<uniform> params: GrassParams;
@group(1) @binding(1) var<storage, read_write> blades: array<GrassBlade>;
@group(1) @binding(2) var<storage, read_write> counters: GrassCounters;
@group(1) @binding(3) var<storage, read_write> stats: GrassStats;

// The depth pyramid of the view, farthest depths (reversed Z), params.hiz its size and levels
@group(2) @binding(0) var pyramid: texture_2d<f32>;

// Lowest and highest ground of the patch of a workgroup in BOUND_QUANTA, whether it is hidden
// (1) and its blades culled by occlusion
var<workgroup> patch_low: atomic<i32>;
var<workgroup> patch_high: atomic<i32>;
var<workgroup> patch_hidden: u32;
var<workgroup> patch_culled: atomic<u32>;

// Blades of the patch of a workgroup made per level, those of a round it keeps per level and
// where their range of the level starts
var<workgroup> group_generated: array<atomic<u32>, 3>;
var<workgroup> group_kept: array<atomic<u32>, 3>;
var<workgroup> group_base: array<u32, 3>;

// Invocations of a workgroup of the place dispatch (BOX_GPU_GRASS_GROUP)
const GROUP: u32 = 128u;

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

// Share of the stiffness of a blade that comes from its clump
const CLUMP_STIFFNESS: f32 = 0.5;

// Factor of the radius of the bound of a blade that leans in the wind, so a blade bent over by a
// gust is not culled
const WIND_BOUND: f32 = 1.15;

// Heights of the terrain along each edge of the grid that bounds the ground of a patch
const BOUND_SAMPLES: u32 = 9u;

// Fewest blades a patch places for it to be culled as a whole
const BOUND_BLADES: u32 = 64u;

// Quanta per meter of the bounds of the ground of a patch
const BOUND_QUANTA: f32 = 64.0;

// Slack of the bounds of the ground of a patch for the terrain between their heights: meters,
// then share of the height range of the patch
const BOUND_SLACK: f32 = 0.25;
const BOUND_SLACK_RANGE: f32 = 0.25;

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
        if (params.caster.w != 0.0) {
            planes[i].w += max(-params.caster.z * dot(planes[i].xyz, params.light.xyz), 0.0);
        }
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

// Whether a rectangle of the screen (uv) whose nearest depth (the largest with reversed Z) is
// given is behind the depth pyramid of the view, as occluded of cull.wgsl does: the finest level
// whose texels are a third of the rectangle covers it with at most four by four texels, and it is
// hidden when its nearest depth is farther than the farthest they hold. A rectangle in front of
// the near plane hides nothing.
fn grass_hidden(lo: vec2f, hi: vec2f, depth: f32) -> bool {
    let a = clamp(lo, vec2f(0.0), vec2f(1.0));
    let b = clamp(hi, vec2f(0.0), vec2f(1.0));
    if (depth >= 1.0 || any(b < a)) {
        return false;
    }

    let size = vec2f(params.hiz.xy);
    let span = max((b.x - a.x) * size.x, (b.y - a.y) * size.y);
    let level = min(u32(max(ceil(log2(span / 3.0)), 0.0)), params.hiz.z - 1u);
    let dims = max(params.hiz.xy >> vec2u(level), vec2u(1u));
    let ta = min(vec2u(a * vec2f(dims)), dims - 1u);
    let tb = min(vec2u(b * vec2f(dims)), dims - 1u);
    if (any(tb - ta > vec2u(3u))) {
        return false;
    }

    var far = 1.0;
    for (var y = ta.y; y <= tb.y; y++) {
        for (var x = ta.x; x <= tb.x; x++) {
            far = min(far, textureLoad(pyramid, vec2u(x, y), i32(level)).r);
        }
    }

    return depth < far;
}

// Whether a box is behind the depth pyramid of the view: its eight corners give its screen
// rectangle and nearest depth. A box that reaches behind the camera, or a view without a pyramid,
// hides nothing.
fn grass_occluded(lo: vec3f, hi: vec3f) -> bool {
    if (params.hiz.z == 0u) {
        return false;
    }

    var a = vec2f(1.0);
    var b = vec2f(0.0);
    var depth = 0.0;
    for (var i = 0u; i < 8u; i++) {
        let corner = select(lo, hi, vec3<bool>((i & 1u) != 0u, (i & 2u) != 0u, (i & 4u) != 0u));
        let p = params.view_proj * vec4f(corner, 1.0);
        if (p.w <= 1e-6) {
            return false;
        }
        let ndc = p.xyz / p.w;
        let uv = vec2f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        a = min(a, uv);
        b = max(b, uv);
        depth = max(depth, ndc.z);
    }

    return grass_hidden(a, b, depth);
}

// Whether a sphere is behind the depth pyramid of the view, cheaper than the box around it: each
// clip coordinate of its points lies within the radius times the length of its row of the view
// projection of that of its center, which bounds its screen rectangle and nearest depth. A sphere
// that reaches behind the camera, or a view without a pyramid, hides nothing.
fn grass_sphere_occluded(c: vec3f, r: f32) -> bool {
    if (params.hiz.z == 0u) {
        return false;
    }

    let m = params.view_proj;
    let p = m * vec4f(c, 1.0);
    let e = r * vec4f(length(vec3f(m[0].x, m[1].x, m[2].x)), length(vec3f(m[0].y, m[1].y, m[2].y)),
        length(vec3f(m[0].z, m[1].z, m[2].z)), length(vec3f(m[0].w, m[1].w, m[2].w)));
    let w0 = p.w - e.w;
    let w1 = p.w + e.w;
    if (w0 <= 1e-6) {
        return false;
    }

    let near_ = (p.xyz + e.xyz) / w0;
    let far_ = (p.xyz + e.xyz) / w1;
    let low = min((p.xy - e.xy) / w0, (p.xy - e.xy) / w1);
    let high = max(near_.xy, far_.xy);
    let depth = max(near_.z, far_.z);
    let a = clamp(vec2f(low.x * 0.5 + 0.5, 0.5 - high.y * 0.5), vec2f(0.0), vec2f(1.0));
    let b = clamp(vec2f(high.x * 0.5 + 0.5, 0.5 - low.y * 0.5), vec2f(0.0), vec2f(1.0));
    if (depth >= 1.0 || any(b < a)) {
        return false;
    }

    // The level whose texels are half the rectangle when it covers the rectangle with two by
    // two texels, else the one above, whose texels are as large as it: four loads that do not
    // wait on each other
    let size = vec2f(params.hiz.xy);
    let span = max((b.x - a.x) * size.x, (b.y - a.y) * size.y);
    var level = min(u32(max(ceil(log2(span)) - 1.0, 0.0)), params.hiz.z - 1u);
    var dims = max(params.hiz.xy >> vec2u(level), vec2u(1u));
    var ta = min(vec2u(a * vec2f(dims)), dims - 1u);
    var tb = min(vec2u(b * vec2f(dims)), dims - 1u);
    if (any(tb - ta > vec2u(1u)) && level + 1u < params.hiz.z) {
        level += 1u;
        dims = max(params.hiz.xy >> vec2u(level), vec2u(1u));
        ta = min(vec2u(a * vec2f(dims)), dims - 1u);
        tb = min(vec2u(b * vec2f(dims)), dims - 1u);
    }

    if (any(tb - ta > vec2u(1u))) {
        return false;
    }

    let far = min(
        min(textureLoad(pyramid, ta, i32(level)).r,
            textureLoad(pyramid, vec2u(tb.x, ta.y), i32(level)).r),
        min(textureLoad(pyramid, vec2u(ta.x, tb.y), i32(level)).r,
            textureLoad(pyramid, tb, i32(level)).r));
    return depth < far;
}

// The lowest and highest ground under the blades of a patch, out to reach from its corner along
// each edge: the plane, or the heights of the terrain on a grid of BOUND_SAMPLES along each edge
// with slack for the terrain between them; x above y where the terrain has no tile. Called by
// every invocation of the workgroup, the bounds are those of the first.
fn grass_patch_ground(at_patch: vec2i, reach: f32, local: u32) -> vec2f {
    if (params.config.y == 0u) {
        return vec2f(params.ground.z);
    }

    if (local == 0u) {
        atomicStore(&patch_low, 0x7fffffff);
        atomicStore(&patch_high, -0x7fffffff);
    }
    workgroupBarrier();

    let step = reach / f32(BOUND_SAMPLES - 1u);
    for (var i = local; i < BOUND_SAMPLES * BOUND_SAMPLES; i += GROUP) {
        let offset = vec2f(f32(i % BOUND_SAMPLES), f32(i / BOUND_SAMPLES)) * step;
        let tile = terrain_sample_find(at_patch * i32(params.corner.z), offset);
        if (tile.found) {
            let y = (terrain_sample_tile_height(tile) + terrain_sample_params.origin.w) *
                BOUND_QUANTA;
            atomicMin(&patch_low, i32(floor(y)));
            atomicMax(&patch_high, i32(ceil(y)));
        }
    }
    workgroupBarrier();

    let low = f32(atomicLoad(&patch_low)) / BOUND_QUANTA;
    let high = f32(atomicLoad(&patch_high)) / BOUND_QUANTA;
    let slack = BOUND_SLACK + BOUND_SLACK_RANGE * (high - low);
    return select(vec2f(low - slack, high + slack), vec2f(1.0, 0.0), low > high);
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

        // The mask first: most cells of a Grass on a mask channel do not grow and skip the
        // height
        if (params.config.x != 0u && terrain_sample_params.strides.z != 0u) {
            weight = terrain_sample_tile_mask(tile)[params.config.x - 1u];
        }
        if (grass_unit(h2) >= weight) {
            return out;
        }

        y = terrain_sample_tile_height(tile) + terrain_sample_params.origin.w;
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

    // A blade thinner than a pixel widens to it, and only the share of blades that keeps the
    // coverage stays, the others shrinking away over a short band of the share
    let widen = min(exp2(2.0 * t), GRASS_WIDEN_MAX);
    var width = mix(params.blade.z, params.blade.w, grass_unit(h4)) * widen;
    let pixel = MIN_PIXELS * grass_pixel(d);
    var keep = fade * mix(0.7, 1.0, weight);
    if (width < pixel) {
        let share = width / pixel;
        keep *= saturate((share - grass_unit(h10)) * 8.0);
        width = pixel;
    }

    // Before the clump and the variation, which only lower the tallest blade the cell can hold:
    // a cell whose tallest blade is too short or outside the frustum holds none
    let bound = select(0.6, WIND_BOUND, params.wind.strength.x > 0.0);
    let tallest = params.blade.y * keep;
    if (tallest < 0.05 * params.blade.x ||
        !grass_sphere_visible(planes, base + vec3f(0.0, 0.5 * tallest, 0.0),
            bound * tallest + width))
    {
        return out;
    }

    // The clump and the variation over the world at the absolute place of the blade
    let world = (vec2f(c) + vec2f(grass_unit(h0), grass_unit(h1))) * params.corner.w;
    let clump = grass_clump(world);
    let follow = saturate(params.shape.y);
    let ch1 = grass_hash(clump.hash ^ SALT_CLUMP);
    let ch2 = grass_hash(ch1);
    let ch3 = grass_hash(ch2);
    let ch4 = grass_hash(ch3);
    let ch5 = grass_hash(ch4);
    let ch6 = grass_hash(ch5);
    let h11 = grass_hash(h10);
    let variation = grass_field_variation(world, params.shape.z);

    // Taller blades toward the center of a clump, all of them as tall as their clump
    let tall = mix(grass_unit(h3), grass_unit(ch1), follow) *
        mix(1.0, 1.0 - CLUMP_HEIGHT * saturate(clump.distance), follow);
    let height = mix(params.blade.x, params.blade.y, tall) * variation.w * keep;
    if (height < 0.05 * params.blade.x) {
        return out;
    }

    let center = base + vec3f(0.0, 0.5 * height, 0.0);
    if (!grass_sphere_visible(planes, center, bound * height + width)) {
        return out;
    }

    // The facing, lean and shade of the clump
    let clump_yaw = grass_unit(ch2) * TAU;
    let own_yaw = grass_unit(h5) * TAU;
    let yaw = own_yaw + grass_turn(own_yaw, clump_yaw) * follow * CLUMP_FACING;
    let lean = mix(grass_unit(h6), grass_unit(ch3), follow);
    let shade = mix(0.75, 1.1, mix(grass_unit(h7), grass_unit(ch4), 0.5 * follow));
    let stiffness = mix(grass_unit(h11), grass_unit(ch6), CLUMP_STIFFNESS);
    let tint = (pack4x8unorm(vec4f(saturate(0.5 * variation.rgb), 0.0)) & 0xFFFFFFu) |
        grass_wind_bits(grass_unit(ch5), stiffness);

    out.keep = true;
    out.blade = GrassBlade(base, yaw, height, width, lean, shade, grass_unit(h8),
        grass_unit(h9) * 2.0 - 1.0, tint, clump.distance, vec4f(0.0));
    return out;
}

// Whether a blade is behind the depth pyramid of the view: the sphere around it, leaning in the
// wind. Tested once the blade is made, which keeps the registers of grass_blade to themselves.
fn grass_blade_occluded(b: GrassBlade) -> bool {
    let bound = select(0.6, WIND_BOUND, params.wind.strength.x > 0.0);
    return grass_sphere_occluded(b.position + vec3f(0.0, 0.5 * b.height, 0.0),
        bound * b.height + b.width);
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
@compute @workgroup_size(128)
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

    // A patch of at least BOUND_BLADES blades behind the depth pyramid places nothing; its box
    // holds the blades standing on its ground, leaning in the wind, out to the cells of the
    // blocks that start in it. The blades of a thinner patch are culled one by one, as bounding
    // its ground would cost more than they do.
    if (params.hiz.z != 0u && total >= BOUND_BLADES) {
        let ground = grass_patch_ground(at_patch, reach, local);
        if (local == 0u) {
            let hidden = ground.x <= ground.y && grass_occluded(
                vec3f(lo.x - margin, ground.x, lo.y - margin),
                vec3f(lo.x + reach + margin, ground.y + params.blade.y * WIND_BOUND,
                    lo.y + reach + margin));
            patch_hidden = select(0u, 1u, hidden);
            if (hidden) {
                atomicAdd(&stats.culled[0], 1u);
            }
        }
        if (workgroupUniformLoad(&patch_hidden) != 0u) {
            return;
        }
    }

    // A round makes a blade per invocation, counts them per level in the workgroup and reserves
    // the range of each level with a single atomic of the counters, so the invocations of the
    // grid do not wait on each other for every blade
    let capacity = params.config.w;
    let rounds = (total + GROUP - 1u) / GROUP;
    var made: GrassPlaced;
    for (var r = 0u; r < rounds; r++) {
        // Assigned every round: an invocation past the last cell of the patch makes no blade,
        // rather than the one of its round before
        let i = r * GROUP + local;
        made = GrassPlaced();
        if (i < total) {
            let b = start + vec2i(i32(i % columns), i32(i / columns)) * s;
            let c = grass_pick(b, k);
            made = grass_blade(at_patch, lo, c, first, k, planes);
            if (made.keep && grass_blade_occluded(made.blade)) {
                made.keep = false;
                atomicAdd(&patch_culled, 1u);
            }
            if (made.keep) {
                made.blade.wind = grass_wind_bend(made.blade);
            }
        }

        if (made.generated) {
            atomicAdd(&group_generated[made.lod], 1u);
        }
        var slot = 0u;
        if (made.keep) {
            slot = atomicAdd(&group_kept[made.lod], 1u);
        }
        workgroupBarrier();

        if (local < GRASS_LODS) {
            let kept = atomicExchange(&group_kept[local], 0u);
            var at = 0u;
            if (kept != 0u) {
                at = atomicAdd(&counters.args[local * 4u + 1u], kept);
            }
            group_base[local] = at;
        }
        workgroupBarrier();

        if (made.keep) {
            let at_level = group_base[made.lod] + slot;
            if (at_level < capacity) {
                blades[made.lod * capacity + at_level] = made.blade;
            }
        }
    }

    // Every round ended with a barrier, so the counts of the workgroup are complete
    if (local < GRASS_LODS) {
        let generated = atomicLoad(&group_generated[local]);
        if (generated != 0u) {
            atomicAdd(&counters.generated[local], generated);
        }
    }
    if (local == 0u) {
        let culled = atomicLoad(&patch_culled);
        if (culled != 0u) {
            atomicAdd(&stats.culled[1], culled);
        }
    }
}

// Clamp the blades of the levels to what they hold and add the counts of the view to those of the
// packet.
@compute @workgroup_size(1)
fn finish() {
    let capacity = params.config.w;
    for (var l = 0u; l < GRASS_LODS; l++) {
        let drawn = min(atomicLoad(&counters.args[l * 4u + 1u]), capacity);
        atomicStore(&counters.args[l * 4u + 1u], drawn);
        if (params.caster.w == 0.0) {
            atomicAdd(&stats.drawn[l], drawn);
            atomicAdd(&stats.generated[l], atomicLoad(&counters.generated[l]));
        }
    }
}
