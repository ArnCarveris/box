/**
 * @file texture/graph.c
 * @brief Node kinds of texture graphs and the build of a TextureGraph into a compiled graph.
 *
 * A change of a TextureGraph or of a node it reached marks it; the build at the end of the frame
 * walks it from its output, checks it and compiles it into the registry entry of the texture,
 * which the render extract sends to the backend. The backend evaluates it into the image.
 */

#include "texture.h"
#include "../render/render.h"
#include <stdarg.h>

/** Most nodes of a graph */
#define BOX_TEXTURE_GRAPH_NODES (1024)

/** Offset basis of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_BASIS (14695981039346656037ull)

/** Prime of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_PRIME (1099511628211ull)

/** WGSL of the TexConstant node: its value */
static const char *box_texture_constant_wgsl =
    "fn box_tex_constant(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return bitcast<vec4f>(p[0]);\n"
    "}\n";

/** WGSL of the TexCoords node: the coordinates times its scale */
static const char *box_texture_coords_wgsl =
    "fn box_tex_coords(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let scale = bitcast<f32>(p[0].x);\n"
    "    return vec4f(coords.xyz * select(scale, 1.0, scale == 0.0), coords.w);\n"
    "}\n";

/** WGSL of the TexMath node: the operation of its op on a, b and c.
 *
 * Its parameters are op, the remap range and the types word, so p[0] is (op, from_min,
 * from_max, to_min) and p[1] is (to_max, types). A Scalar input node is broadcast; the
 * operations that differ on Scalars check whether the inputs they read are Scalars.
 */
static const char *box_texture_math_wgsl =
    "fn box_tex_math_in(v: vec4f, types: u32, j: u32) -> vec4f {\n"
    "    return select(v, v.xxxx, (types & (1u << j)) != 0u);\n"
    "}\n"
    "\n"
    "fn box_tex_math_scalar(types: u32, used: u32) -> bool {\n"
    "    return (types & (used << 4u)) == 0u && (types & used) != 0u;\n"
    "}\n"
    "\n"
    "fn box_tex_math_div(a: vec4f, b: vec4f) -> vec4f {\n"
    "    let zero = b == vec4f(0.0);\n"
    "    return select(a / select(b, vec4f(1.0), zero), vec4f(0.0), zero);\n"
    "}\n"
    "\n"
    "fn box_tex_math_pow(a: vec4f, b: vec4f) -> vec4f {\n"
    "    let r = pow(max(abs(a), vec4f(1e-30)), b);\n"
    "    let whole = fract(b) == vec4f(0.0);\n"
    "    let odd = whole & (fract(b * 0.5) != vec4f(0.0));\n"
    "    let negative = a < vec4f(0.0);\n"
    "    var v = select(r, -r, negative & odd);\n"
    "    v = select(v, vec4f(0.0), (negative & !whole) | (a == vec4f(0.0)));\n"
    "    return select(v, vec4f(1.0), b == vec4f(0.0));\n"
    "}\n"
    "\n"
    "fn box_tex_math_smoothstep(e0: vec4f, e1: vec4f, x: vec4f) -> vec4f {\n"
    "    let d = e1 - e0;\n"
    "    let same = d == vec4f(0.0);\n"
    "    let t = clamp((x - e0) / select(d, vec4f(1.0), same), vec4f(0.0), vec4f(1.0));\n"
    "    let s = t * t * (3.0 - 2.0 * t);\n"
    "    return select(s, select(vec4f(0.0), vec4f(1.0), x >= e0), same);\n"
    "}\n"
    "\n"
    "fn box_tex_math_remap(a: vec4f, range: vec4f, clamped: bool) -> vec4f {\n"
    "    let d = range.y - range.x;\n"
    "    var t = select((a - range.x) / d, vec4f(0.0), d == 0.0);\n"
    "    if (clamped) {\n"
    "        t = clamp(t, vec4f(0.0), vec4f(1.0));\n"
    "    }\n"
    "\n"
    "    return range.z + t * (range.w - range.z);\n"
    "}\n"
    "\n"
    "fn box_tex_math(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[1].y;\n"
    "    let a = box_tex_math_in(i[0], types, 0u);\n"
    "    let b = box_tex_math_in(i[1], types, 1u);\n"
    "    let c = box_tex_math_in(i[2], types, 2u);\n"
    "    let range = vec4f(bitcast<vec3f>(p[0].yzw), bitcast<f32>(p[1].x));\n"
    "    switch bitcast<i32>(p[0].x) {\n"
    "        case 1: { return a - b; }\n"
    "        case 2: { return a * b; }\n"
    "        case 3: { return min(a, b); }\n"
    "        case 4: { return max(a, b); }\n"
    "        case 5: { return mix(a, b, c); }\n"
    "        case 6: { return min(max(a, b), c); }\n"
    "        case 7: { return box_tex_math_div(a, b); }\n"
    "        case 8: { return box_tex_math_pow(a, b); }\n"
    "        case 9: { return abs(a); }\n"
    "        case 10: { return -a; }\n"
    "        case 11: { return floor(a); }\n"
    "        case 12: { return a - floor(a); }\n"
    "        case 13: { return sin(a); }\n"
    "        case 14: { return cos(a); }\n"
    "        case 15: { return select(vec4f(0.0), vec4f(1.0), a >= b); }\n"
    "        case 16: { return box_tex_math_smoothstep(b, c, a); }\n"
    "        case 17: { return box_tex_math_remap(a, range, false); }\n"
    "        case 18: { return box_tex_math_remap(a, range, true); }\n"
    "        case 19: {\n"
    "            if (box_tex_math_scalar(types, 3u)) {\n"
    "                return vec4f(a.x * b.x, 0.0, 0.0, 0.0);\n"
    "            }\n"
    "\n"
    "            return vec4f(dot(a.xyz, b.xyz), 0.0, 0.0, 0.0);\n"
    "        }\n"
    "        case 20: {\n"
    "            if (box_tex_math_scalar(types, 1u)) {\n"
    "                return vec4f(abs(a.x), 0.0, 0.0, 0.0);\n"
    "            }\n"
    "\n"
    "            return vec4f(length(a.xyz), 0.0, 0.0, 0.0);\n"
    "        }\n"
    "        case 21: {\n"
    "            if (box_tex_math_scalar(types, 1u)) {\n"
    "                return sign(a);\n"
    "            }\n"
    "\n"
    "            let l = length(a.xyz);\n"
    "            return vec4f(select(a.xyz / l, vec3f(0.0), l == 0.0), a.w);\n"
    "        }\n"
    "        case 22: { return vec4f(cross(a.xyz, b.xyz), 0.0); }\n"
    "        default: { return a + b; }\n"
    "    }\n"
    "}\n";

<<<<<<< HEAD
/** WGSL of the TexCells node: Voronoi cells of jittered lattice points, periodic on the plane
 * and 3D on the sphere, the distance in x and three random values of the nearest cell in y, z
 * and w.
 *
 * The cells around the texel are searched inner ring first, skipping a cell whose box, which
 * holds its point wherever evolve moves it, is farther than the second nearest point found. */
static const char *box_texture_cells_wgsl =
    "fn box_tex_cells_hash(cell: vec3i, seed: u32) -> u32 {\n"
    "    return box_texgraph_hash(box_texgraph_hash3(bitcast<vec3u>(cell)) ^\n"
    "        box_texgraph_hash(seed + 0x9e3779b9u));\n"
    "}\n"
    "\n"
    "fn box_tex_cells_random(h: u32, k: u32) -> f32 {\n"
    "    return box_texgraph_random(box_texgraph_hash(h ^ (k * 0x9e3779b9u)));\n"
    "}\n"
    "\n"
    "fn box_tex_cells_wave(x: vec3f) -> vec3f {\n"
    "    return abs(fract(x) * 2.0 - 1.0);\n"
    "}\n"
    "\n"
    "fn box_tex_cells_point(h: u32, jitter: f32, evolve: f32) -> vec3f {\n"
    "    let r = vec3f(box_tex_cells_random(h, 0u), box_tex_cells_random(h, 1u),\n"
    "        box_tex_cells_random(h, 2u));\n"
    "    let speed = 0.5 + 0.5 * vec3f(box_tex_cells_random(h, 3u),\n"
    "        box_tex_cells_random(h, 4u), box_tex_cells_random(h, 5u));\n"
    "    return 0.5 + jitter * (box_tex_cells_wave(r + evolve * speed) - 0.5);\n"
    "}\n"
    "\n"
    "fn box_tex_cells(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let count_param = bitcast<i32>(p[0].x);\n"
    "    let count = f32(select(count_param, 64, count_param <= 0));\n"
    "    let jitter = clamp(bitcast<f32>(p[0].y), 0.0, 1.0);\n"
    "    let seed = p[0].z;\n"
    "    let mode = bitcast<i32>(p[0].w);\n"
    "    let edge_param = bitcast<f32>(p[1].x);\n"
    "    let edge = select(edge_param, 0.25, edge_param <= 0.0);\n"
    "    let evolve = i[1].x;\n"
    "    var q = vec3f(0.0);\n"
    "    var period = vec3i(0);\n"
    "    var weight = vec3f(1.0);\n"
    "    var reach = 2;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        q = i[0].xyz * sqrt(count / (6.0 * BOX_TEXGRAPH_PI));\n"
    "    } else {\n"
    "        let size = vec2f(box_texgraph_stage.size);\n"
    "        let gx = clamp(floor(sqrt(count * size.x / size.y) + 0.5), 1.0, 4096.0);\n"
    "        let gy = clamp(floor(count / gx + 0.5), 1.0, 4096.0);\n"
    "        let ratio = sqrt((size.x / gx) / (size.y / gy));\n"
    "        q = vec3f(i[0].xy * vec2f(gx, gy), 0.0);\n"
    "        period = vec3i(i32(gx), i32(gy), 1);\n"
    "        weight = vec3f(ratio, 1.0 / ratio, 0.0);\n"
    "        reach = 0;\n"
    "    }\n"
    "\n"
    "    let base = vec3i(floor(q));\n"
    "    var f1 = 1e30;\n"
    "    var f2 = 1e30;\n"
    "    var near = 0u;\n"
    "    for (var ring = 0; ring < 2; ring ++) {\n"
    "        for (var z = -reach; z <= reach; z ++) {\n"
    "            for (var y = -2; y <= 2; y ++) {\n"
    "                for (var x = -2; x <= 2; x ++) {\n"
    "                    let o = vec3i(x, y, z);\n"
    "                    let c = base + o;\n"
    "                    let gap = max(max(vec3f(c) - q, q - vec3f(c + vec3i(1))),\n"
    "                        vec3f(0.0)) * weight;\n"
    "                    let inner = all(abs(o) <= vec3i(1));\n"
    "                    if (inner != (ring == 0) || dot(gap, gap) >= f2 * f2) {\n"
    "                        continue;\n"
    "                    }\n"
    "\n"
    "                    var cell = c;\n"
    "                    if (period.x > 0) {\n"
    "                        cell = ((c % period) + period) % period;\n"
    "                    }\n"
    "\n"
    "                    let h = box_tex_cells_hash(cell, seed);\n"
    "                    let at = box_tex_cells_point(h, jitter, evolve);\n"
    "                    let d = length((vec3f(c) + at - q) * weight);\n"
    "                    if (d < f1) {\n"
    "                        f2 = f1;\n"
    "                        f1 = d;\n"
    "                        near = h;\n"
    "                    } else if (d < f2) {\n"
    "                        f2 = d;\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    var d = f1;\n"
    "    if (mode == 1) {\n"
    "        d = f2;\n"
    "    } else if (mode == 2) {\n"
    "        d = smoothstep(0.0, edge, f2 - f1);\n"
    "    }\n"
    "\n"
    "    return vec4f(d, box_tex_cells_random(near, 6u), box_tex_cells_random(near, 7u),\n"
    "        box_tex_cells_random(near, 8u));\n"
=======
/** WGSL of the TexBlocks node: the courses of blocks of box_gen_blocks of texture/gen.c.
 *
 * The cuts of the course of the texel are made again rather than sorted: the block starts at
 * the largest cut at or before u, the last in index order among equal cuts as the stable sort
 * keeps them; its id comes from its rank among the cuts and its tone from its index.
 *
 * Before the first cut the block is the last one with its end one past the first cut, as gen.c
 * has it. Lengths are in widths of the image. */
static const char *box_texture_blocks_wgsl =
    "fn box_tex_blocks_mix(v: u32) -> u32 {\n"
    "    var x = v;\n"
    "    x ^= x >> 16u;\n"
    "    x *= 0x7feb352du;\n"
    "    x ^= x >> 15u;\n"
    "    x *= 0x846ca68bu;\n"
    "    x ^= x >> 16u;\n"
    "    return x;\n"
    "}\n"
    "\n"
    "fn box_tex_blocks_hash(h: u32, v: u32) -> u32 {\n"
    "    return box_tex_blocks_mix(h ^ box_tex_blocks_mix(v + 0x9e3779b9u));\n"
    "}\n"
    "\n"
    "fn box_tex_blocks_unit(h: u32) -> f32 {\n"
    "    return f32(h >> 8u) / 16777216.0;\n"
    "}\n"
    "\n"
    "fn box_tex_blocks_cut(hk: u32, b: i32, n: i32, jitter: f32, shift: f32) -> f32 {\n"
    "    let r = box_tex_blocks_unit(box_tex_blocks_hash(hk, u32(b)));\n"
    "    let cut = (f32(b) + (r * 2.0 - 1.0) * jitter + shift) / f32(n);\n"
    "    return cut - floor(cut);\n"
    "}\n"
    "\n"
    "fn box_tex_blocks(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let counts = bitcast<vec4i>(p[0]);\n"
    "    let shape = bitcast<vec4f>(p[1]);\n"
    "    let courses = select(min(counts.x, 4096), 8, counts.x <= 0);\n"
    "    let n = select(min(counts.y, 4096), 4, counts.y <= 0);\n"
    "    let tones = clamp(counts.z, 0, 4096);\n"
    "    let tone_order = counts.w;\n"
    "    let gap = shape.x;\n"
    "    let jitter = shape.y;\n"
    "    let softness = shape.w;\n"
    "    let at = i[0];\n"
    "    var seed = box_tex_blocks_hash(0x5bd1e995u, p[2].x);\n"
    "    seed = box_tex_blocks_hash(seed, p[2].y);\n"
    "    seed = box_tex_blocks_hash(seed, 1u);\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let v = (at.y - floor(at.y)) * f32(courses);\n"
    "    let u = at.x - floor(at.x);\n"
    "    let k = min(i32(floor(v)), courses - 1);\n"
    "    let dy = min(v - f32(k), f32(k + 1) - v) * size.y / (f32(courses) * size.x);\n"
    "    let hk = box_tex_blocks_hash(seed, u32(k));\n"
    "    let shift = select(0.0, shape.z, (k % 2) == 1);\n"
    "    var start = -1.0;\n"
    "    var start_b = -1;\n"
    "    var end = 2.0;\n"
    "    var lo = 2.0;\n"
    "    var hi = -1.0;\n"
    "    var hi_b = 0;\n"
    "    for (var b = 0; b < n; b ++) {\n"
    "        let c = box_tex_blocks_cut(hk, b, n, jitter, shift);\n"
    "        if (c <= u && c >= start) {\n"
    "            start = c;\n"
    "            start_b = b;\n"
    "        }\n"
    "        if (c > u) {\n"
    "            end = min(end, c);\n"
    "        }\n"
    "        if (c >= hi) {\n"
    "            hi = c;\n"
    "            hi_b = b;\n"
    "        }\n"
    "        lo = min(lo, c);\n"
    "    }\n"
    "    var cut = start;\n"
    "    if (start_b < 0) {\n"
    "        cut = hi;\n"
    "        start = hi - 1.0;\n"
    "        start_b = hi_b;\n"
    "        end = lo + 1.0;\n"
    "    } else if (end > 1.5) {\n"
    "        end = lo + 1.0;\n"
    "    }\n"
    "    var rank = 0;\n"
    "    for (var b = 0; b < n; b ++) {\n"
    "        let c = box_tex_blocks_cut(hk, b, n, jitter, shift);\n"
    "        if (c < cut || (c == cut && b < start_b)) {\n"
    "            rank ++;\n"
    "        }\n"
    "    }\n"
    "    let hid = box_tex_blocks_hash(hk ^ 0xa511e9b3u, 1u);\n"
    "    let htone = box_tex_blocks_hash(hk ^ 0x3c6ef372u, 2u);\n"
    "    let hrandom = box_tex_blocks_hash(hk ^ 0x85ebca6bu, 3u);\n"
    "    let id = box_tex_blocks_unit(box_tex_blocks_hash(hid, u32(rank)));\n"
    "    let random = box_tex_blocks_unit(box_tex_blocks_hash(hrandom, u32(rank)));\n"
    "    var tone = 0;\n"
    "    if (tones > 0) {\n"
    "        if (tone_order == 1) {\n"
    "            tone = (k + start_b) % tones;\n"
    "        } else if (tone_order == 2) {\n"
    "            tone = k % tones;\n"
    "        } else if (tone_order == 3) {\n"
    "            tone = start_b % tones;\n"
    "        } else {\n"
    "            let r = box_tex_blocks_unit(box_tex_blocks_hash(htone, u32(start_b)));\n"
    "            tone = min(i32(floor(r * f32(tones))), tones - 1);\n"
    "        }\n"
    "    }\n"
    "    let d = min(min(u - start, end - u), dy);\n"
    "    let half = 0.5 * gap;\n"
    "    let bevel = select(1.3 * gap, softness, softness > 0.0);\n"
    "    var mask = select(0.0, 1.0, d >= half);\n"
    "    if (bevel > 0.0) {\n"
    "        mask = smoothstep(half, half + bevel, d);\n"
    "    }\n"
    "    let level = select(0.0, f32(tone) / f32(max(tones - 1, 1)), tones > 1);\n"
    "    return vec4f(mask, id, level, random);\n"
    "}\n";

/** WGSL of the TexRamp node: the display encoded colors of the keys around the input mixed as
 * box_gen_ramp does, decoded to linear; the elements are (position, r, g, b) in 0..1 */
static const char *box_texture_ramp_wgsl =
    "fn box_tex_ramp_key(at: u32) -> vec4f {\n"
    "    return bitcast<vec4f>(box_texgraph_params[at]);\n"
    "}\n"
    "\n"
    "fn box_tex_ramp(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let base = p[0].x;\n"
    "    let count = p[0].y;\n"
    "    let h = i[0].x;\n"
    "    if (count == 0u) {\n"
    "        return vec4f(box_texgraph_linear(vec3f(h)), 1.0);\n"
    "    }\n"
    "    var a = box_tex_ramp_key(base);\n"
    "    var b = a;\n"
    "    var t = 0.0;\n"
    "    let last = box_tex_ramp_key(base + count - 1u);\n"
    "    if (h >= last.x) {\n"
    "        a = last;\n"
    "        b = last;\n"
    "    } else if (h > a.x) {\n"
    "        var k = 0u;\n"
    "        while (k + 1u < count && box_tex_ramp_key(base + k + 1u).x <= h) {\n"
    "            k += 1u;\n"
    "        }\n"
    "        a = box_tex_ramp_key(base + k);\n"
    "        b = box_tex_ramp_key(base + k + 1u);\n"
    "        t = select(1.0, (h - a.x) / (b.x - a.x), b.x > a.x);\n"
    "    }\n"
    "    return vec4f(box_texgraph_linear(a.yzw + (b.yzw - a.yzw) * t), 1.0);\n"
    "}\n";

/** WGSL of the TexCurve node: box_curve_value of the curve at the input, the input without
 * keys; the elements are (time, value, control1, control2) of the span from each key, the
 * cubic the twin of curve_cubic in etc/shaders/include/curve.wgsl */
static const char *box_texture_curve_wgsl =
    "fn box_tex_curve_mix(a: f32, b: f32, t: f32) -> f32 {\n"
    "    return a * (1.0 - t) + b * t;\n"
    "}\n"
    "\n"
    "fn box_tex_curve_key(at: u32) -> vec4f {\n"
    "    return bitcast<vec4f>(box_texgraph_params[at]);\n"
    "}\n"
    "\n"
    "fn box_tex_curve(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let base = p[0].x;\n"
    "    let count = p[0].y;\n"
    "    let x = i[0].x;\n"
    "    if (count == 0u) {\n"
    "        return vec4f(x, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let first = box_tex_curve_key(base);\n"
    "    if (count == 1u || !(x > first.x)) {\n"
    "        return vec4f(first.y, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let last = box_tex_curve_key(base + count - 1u);\n"
    "    if (x >= last.x) {\n"
    "        return vec4f(last.y, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    var k = 0u;\n"
    "    while (k + 2u < count && box_tex_curve_key(base + k + 1u).x <= x) {\n"
    "        k += 1u;\n"
    "    }\n"
    "    let a = box_tex_curve_key(base + k);\n"
    "    let b = box_tex_curve_key(base + k + 1u);\n"
    "    let t = (x - a.x) / (b.x - a.x);\n"
    "    let m0 = box_tex_curve_mix(a.y, a.z, t);\n"
    "    let m1 = box_tex_curve_mix(a.z, a.w, t);\n"
    "    let m2 = box_tex_curve_mix(a.w, b.y, t);\n"
    "    let n0 = box_tex_curve_mix(m0, m1, t);\n"
    "    let n1 = box_tex_curve_mix(m1, m2, t);\n"
    "    return vec4f(box_tex_curve_mix(n0, n1, t), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexSplit node: one channel of its input */
static const char *box_texture_split_wgsl =
    "fn box_tex_split(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let v = i[0];\n"
    "    switch bitcast<i32>(p[0].x) {\n"
    "        case 1: { return vec4f(v.y, 0.0, 0.0, 0.0); }\n"
    "        case 2: { return vec4f(v.z, 0.0, 0.0, 0.0); }\n"
    "        case 3: { return vec4f(v.w, 0.0, 0.0, 0.0); }\n"
    "        default: { return vec4f(v.x, 0.0, 0.0, 0.0); }\n"
    "    }\n"
    "}\n";

/** WGSL of the TexCombine node: its four scalars as the channels of a vector */
static const char *box_texture_combine_wgsl =
    "fn box_tex_combine(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x, i[1].x, i[2].x, i[3].x);\n"
    "}\n";

/** WGSL of the TexWarp node: its coordinates moved by (offset - 0.5) * amount, normalized on the
 * sphere */
static const char *box_texture_warp_wgsl =
    "fn box_tex_warp(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let c = i[0];\n"
    "    let moved = c.xyz + (i[1].xyz - vec3f(0.5)) * i[2].x;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        let len = length(moved);\n"
    "        if (len < 1e-6) {\n"
    "            return c;\n"
    "        }\n"
    "        return vec4f(moved / len, c.w);\n"
    "    }\n"
    "    return vec4f(moved, c.w);\n"
    "}\n";

/** WGSL of the TexTransform node: its coordinates scaled and rotated about the centre of the
 * domain, then translated. A rotation by a multiple of 90 degrees is exact, so the plane tiles */
static const char *box_texture_transform_wgsl =
    "fn box_tex_transform(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let c = i[0];\n"
    "    let axis = bitcast<vec3f>(p[0].xyz);\n"
    "    let translate = bitcast<vec3f>(p[1].xyz);\n"
    "    let factor = bitcast<vec3f>(p[2].xyz);\n"
    "    let scale = select(factor, vec3f(1.0), factor == vec3f(0.0));\n"
    "    let deg = i[1].x;\n"
    "    let quarter = round(deg / 90.0);\n"
    "    let k = ((i32(quarter) % 4) + 4) % 4;\n"
    "    let exact = select(select(select(vec2f(0.0, -1.0), vec2f(-1.0, 0.0), k == 2),\n"
    "        vec2f(0.0, 1.0), k == 1), vec2f(1.0, 0.0), k == 0);\n"
    "    let angle = radians(deg);\n"
    "    let cs = select(vec2f(cos(angle), sin(angle)), exact,\n"
    "        abs(deg / 90.0 - quarter) < 1e-6);\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        let len = length(axis);\n"
    "        let n = select(vec3f(0.0, 1.0, 0.0), axis / max(len, 1e-6), len > 1e-6);\n"
    "        let v = c.xyz * scale;\n"
    "        let r = v * cs.x + cross(n, v) * cs.y + n * dot(n, v) * (1.0 - cs.x);\n"
    "        return vec4f(r + translate, c.w);\n"
    "    }\n"
    "    let d = (c.xy - vec2f(0.5)) * scale.xy;\n"
    "    let r = vec2f(cs.x * d.x - cs.y * d.y, cs.y * d.x + cs.x * d.y);\n"
    "    return vec4f(r + vec2f(0.5) + translate.xy, c.z * scale.z + translate.z, c.w);\n"
    "}\n";

/** WGSL of the TexTime node: the seconds of the world clock times its scale plus its offset */
static const char *box_texture_time_wgsl =
    "fn box_tex_time(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let scale = bitcast<f32>(p[0].x);\n"
    "    let offset = bitcast<f32>(p[0].y);\n"
    "    return vec4f(box_texgraph_stage.time * select(scale, 1.0, scale == 0.0) + offset, 0.0,\n"
    "        0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexSplat node: its scalar input in every channel */
static const char *box_texture_splat_wgsl =
    "fn box_tex_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x);\n"
>>>>>>> ec4963cf181bd9d1a403187fb2437ae201c7c186
    "}\n";

/** State of the build of one graph */
typedef struct box_texture_graph_build_t {
    ecs_world_t *world;         /**< The world or a stage */
    box_textures_t *t;          /**< The registry */
    ecs_entity_t graph;         /**< The texture entity with the TextureGraph */
    char *name;                 /**< Path of the graph */
    ecs_map_t index;            /**< Node entity to 0 while it is visited, index + 1 after */
    ecs_vec_t nodes;            /**< box_texture_graph_node_t in topological order */
    ecs_vec_t params;           /**< uint8_t, parameter bytes of the nodes */
    ecs_vec_t *reached;         /**< ecs_entity_t, the entities the walk reached */
    TextureGraphDomain domain;  /**< Coordinates of the texels of the graph */
    char *error;                /**< Why the build failed, NULL while it did not */
    bool wait;                  /**< Wait for a node without a node component */
    bool waiting;               /**< The walk found a node without a node component */
} box_texture_graph_build_t;

/** Add bytes to a 64 bit FNV-1a hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes
 */
static uint64_t box_texture_graph_hash(
    uint64_t hash,
    const void *data,
    int32_t size)
{
    const uint8_t *bytes = data;
    int32_t i;
    for (i = 0; i < size; i ++) {
        hash ^= bytes[i];
        hash *= BOX_TEXTURE_GRAPH_FNV_PRIME;
    }

    return hash;
}

box_texture_node_kind_t* box_texture_node_kind(
    const box_textures_t *t,
    int32_t id)
{
    box_texture_node_kind_t *result = NULL;
    if (t && id > 0 && id <= ecs_vec_count(&t->node_kinds)) {
        result = *ecs_vec_get_t(&t->node_kinds, box_texture_node_kind_t*, id - 1);
    }

    return result;
}

/** Free the strings a node kind owns.
 *
 * @param kind The kind
 */
static void box_texture_node_kind_strings_free(
    box_texture_node_kind_t *kind)
{
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->inputs[i].name));
        ecs_os_free(ECS_CONST_CAST(char*, kind->inputs[i].value));
    }

    for (i = 0; i < kind->param_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->params[i].name));
    }

    ecs_os_free(ECS_CONST_CAST(char*, kind->array.name));

    ecs_os_free(kind->function);
    ecs_os_free(kind->source);
}

void box_texture_graph_clear(
    box_texture_t *tex)
{
    if (tex->graph) {
        ecs_os_free(tex->graph->nodes);
        ecs_os_free(tex->graph->params);
        ecs_os_free(tex->graph);
        tex->graph = NULL;
    }

    ecs_vec_fini_t(NULL, &tex->reached, ecs_entity_t);
    tex->graph_dirty = false;
    tex->graph_waiting = false;
    tex->graph_sent = false;
    tex->graph_pending = false;
}

void box_texture_graphs_fini(
    box_textures_t *t)
{
    int32_t i, count = ecs_vec_count(&t->node_kinds);
    for (i = 0; i < count; i ++) {
        box_texture_node_kind_t *kind = *ecs_vec_get_t(
            &t->node_kinds, box_texture_node_kind_t*, i);
        box_texture_node_kind_strings_free(kind);
        ecs_os_free(kind);
    }

    ecs_vec_fini_t(NULL, &t->node_kinds, box_texture_node_kind_t*);
    ecs_vec_fini_t(NULL, &t->graph_queued, ecs_entity_t);
    ecs_vec_fini_t(NULL, &t->graph_released, uint32_t);
    ecs_vec_fini_t(NULL, &t->reload, ecs_entity_t);
}

void box_texture_graph_request(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_t *tex = box_texture_entry(t, e);
    tex->generation ++;
    tex->graph_dirty = true;
    box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
}

/** Mark every graph dirty that reached an entity in its last build, or every graph.
 *
 * @param t The registry
 * @param e The entity, 0 for every graph
 */
static void box_texture_graphs_touch(
    box_textures_t *t,
    ecs_entity_t e)
{
    int32_t i;
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        const ecs_entity_t *reached = ecs_vec_first_t(&tex->reached, ecs_entity_t);
        int32_t count = ecs_vec_count(&tex->reached);
        if (!e && count) {
            tex->graph_dirty = true;
            continue;
        }

        for (i = 0; i < count; i ++) {
            if (reached[i] == e) {
                tex->graph_dirty = true;
                break;
            }
        }
    }
}

/** Build again the graphs that reached a node whose node component was set or removed, also
 * when the node is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureNodeChanged(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    if (!t || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_texture_graphs_touch(t, it->entities[i]);
    }

done:
    return;
}

/** Copy the description of a node kind into a kind of the registry, checking it.
 *
 * @param kind The kind, its strings receive copies
 * @param desc The description
 * @return 0 on success, -1 when the description is invalid
 */
static int box_texture_node_kind_fill(
    box_texture_node_kind_t *kind,
    const box_texture_node_desc_t *desc)
{
    int32_t i, offset = 0, result = 0;
    bool any = false;
    kind->component = desc->component;
    kind->output = desc->output;
    kind->output_type = desc->output_type;
    kind->flags = desc->flags;
    kind->function = ecs_os_strdup(desc->function);
    kind->source = ecs_os_strdup(desc->source);
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS && desc->inputs[i].name; i ++) {
        kind->inputs[i] = desc->inputs[i];
        kind->inputs[i].name = ecs_os_strdup(desc->inputs[i].name);
        kind->inputs[i].value = desc->inputs[i].value
            ? ecs_os_strdup(desc->inputs[i].value)
            : NULL;
        kind->input_count ++;
        any = any || desc->inputs[i].type == TexAny;
    }

    for (i = 0; i < BOX_TEXTURE_NODE_PARAMS && desc->params[i].name; i ++) {
        ecs_size_t size = desc->params[i].size;
        int32_t align = size <= 4 ? 4 : size <= 8 ? 8 : 16;
        kind->params[i] = desc->params[i];
        kind->params[i].name = ecs_os_strdup(desc->params[i].name);
        kind->param_count ++;
        if (size < 4 || size > 16 || size % 4) {
            result = -1;
        }

        offset = (offset + align - 1) / align * align;
        kind->param_offsets[i] = offset;
        offset += size;
    }

    kind->types_offset = -1;
    if (any) {
        kind->types_offset = (offset + 3) / 4 * 4;
        offset = kind->types_offset + ECS_SIZEOF(uint32_t);
    }

    kind->param_size = (offset + 15) / 16 * 16;
    kind->array_header = -1;
    if (desc->array.name) {
        kind->array = desc->array;
        kind->array.name = ecs_os_strdup(desc->array.name);
        kind->array_header = kind->param_size;
        kind->param_size += 16;
        if (desc->array.max < 1 || desc->array.max > BOX_TEXTURE_NODE_ARRAY_MAX ||
            (!desc->array.pack && (desc->array.size < 4 || desc->array.size > 16)))
        {
            result = -1;
        }
    }

    kind->node_size = kind->param_size + kind->input_count * 16 +
        (kind->array.name ? kind->array.max * 16 : 0);
    if (kind->param_size > BOX_TEXTURE_NODE_PARAM_BYTES) {
        result = -1;
    }

    return result;
}

/** Describe a node kind in the reflected TextureNodeKind of its component.
 *
 * @param world The world
 * @param kind The kind
 */
static void box_texture_node_kind_reflect(
    ecs_world_t *world,
    const box_texture_node_kind_t *kind)
{
    TextureNodeKind value = {
        .id = kind->id,
        .output = kind->output,
        .input_count = kind->input_count,
        .param_count = kind->param_count,
        .param_size = kind->param_size,
        .array = kind->array.name,
        .array_max = kind->array.name ? kind->array.max : 0,
        .array_header = kind->array_header,
        .types_offset = kind->types_offset,
        .function = kind->function,
        .neighbourhood = (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) != 0,
        .animated = (kind->flags & BOX_TEXTURE_NODE_ANIMATED) != 0,
        .plane = (kind->flags & BOX_TEXTURE_NODE_PLANE) != 0
    };
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        value.inputs[i].name = kind->inputs[i].name;
        value.inputs[i].type = kind->inputs[i].type;
        value.inputs[i].fallback = kind->inputs[i].value != NULL;
<<<<<<< HEAD
        value.inputs[i].coords = kind->inputs[i].coords;
=======
        value.inputs[i].value = kind->inputs[i].value;
>>>>>>> ec4963cf181bd9d1a403187fb2437ae201c7c186
    }

    for (i = 0; i < kind->param_count; i ++) {
        value.params[i].name = kind->params[i].name;
        value.params[i].offset = kind->param_offsets[i];
        value.params[i].size = kind->params[i].size;
    }

    ecs_set_ptr(world, kind->component, TextureNodeKind, &value);
}

int32_t box_texture_node_register(
    ecs_world_t *world,
    const box_texture_node_desc_t *desc)
{
    box_textures_t *t = box_textures_get(world);
    box_texture_node_kind_t *kind = NULL, *existing = NULL;
    int32_t result = 0, i, count;
    if (!t || !desc || !desc->component || !desc->function || !desc->source) {
        ecs_err("box_texture_node_register: invalid node kind description");
        goto done;
    }

    count = ecs_vec_count(&t->node_kinds);
    for (i = 0; i < count; i ++) {
        box_texture_node_kind_t *k = *ecs_vec_get_t(&t->node_kinds, box_texture_node_kind_t*, i);
        if (k->component == desc->component) {
            existing = k;
        }
    }

    kind = ecs_os_calloc_t(box_texture_node_kind_t);
    if (box_texture_node_kind_fill(kind, desc)) {
        char *name = ecs_get_path(world, desc->component);
        ecs_err("box_texture_node_register: node kind '%s' has a parameter of an invalid size "
            "or more than %d parameter bytes, or an invalid array", name,
            BOX_TEXTURE_NODE_PARAM_BYTES);
        ecs_os_free(name);
        box_texture_node_kind_strings_free(kind);
        ecs_os_free(kind);
        goto done;
    }

    if (existing) {
        kind->id = existing->id;
        box_texture_node_kind_strings_free(existing);
        *existing = *kind;
        ecs_os_free(kind);
        kind = existing;
    } else {
        kind->id = count + 1;
        *ecs_vec_append_t(NULL, &t->node_kinds, box_texture_node_kind_t*) = kind;
        ecs_observer(world, {
            .query.terms = {{ .id = desc->component }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxTextureNodeChanged
        });
    }

    kind->sent = false;
    result = kind->id;
    box_texture_node_kind_reflect(world, kind);
    box_texture_graphs_touch(t, 0);
done:
    return result;
}

/** Fail a build with an error that names the graph.
 *
 * @param b The build
 * @param fmt Format of the problem, after the name of the graph
 * @param ... Arguments of the format
 */
static void box_texture_graph_error(
    box_texture_graph_build_t *b,
    const char *fmt,
    ...)
{
    va_list args;
    if (b->error) {
        goto done;
    }

    va_start(args, fmt);
    char *problem = flecs_vasprintf(fmt, args);
    va_end(args);
    b->error = flecs_asprintf("texture graph '%s': %s", b->name, problem);
    ecs_os_free(problem);
done:
    return;
}

/** Get the path of an entity for an error, its id when it is not alive.
 *
 * @param world The world or a stage
 * @param e The entity
 * @return The path, freed by the caller
 */
static char* box_texture_graph_name(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    char *result = ecs_is_alive(world, e)
        ? ecs_get_path(world, e)
        : flecs_asprintf("#%u", (uint32_t)e);

    return result;
}

/** Find the node kind of an entity: the kind of the one node component it has.
 *
 * @param b The build
 * @param e The entity
 * @param count Receives the number of node components it has
 * @return The kind, NULL without one
 */
static box_texture_node_kind_t* box_texture_graph_kind_of(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    int32_t *count)
{
    box_texture_node_kind_t *result = NULL;
    int32_t i, kinds = ecs_vec_count(&b->t->node_kinds);
    *count = 0;
    for (i = 0; i < kinds; i ++) {
        box_texture_node_kind_t *kind = *ecs_vec_get_t(
            &b->t->node_kinds, box_texture_node_kind_t*, i);
        if (ecs_has_id(b->world, e, kind->component)) {
            result = result ? result : kind;
            (*count) ++;
        }
    }

    return result;
}

/** Record an entity the walk reached, once.
 *
 * @param b The build
 * @param e The entity
 */
static void box_texture_graph_reach(
    box_texture_graph_build_t *b,
    ecs_entity_t e)
{
    int32_t i, count = ecs_vec_count(b->reached);
    const ecs_entity_t *reached = ecs_vec_first_t(b->reached, ecs_entity_t);
    bool found = false;
    for (i = 0; i < count; i ++) {
        found = found || reached[i] == e;
    }

    if (!found) {
        *ecs_vec_append_t(NULL, b->reached, ecs_entity_t) = e;
    }
}

/** Write the array parameter of a node: its header and its elements after the constants of
 * its inputs, failing the build with more elements than the kind takes or invalid ones.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind, with an array parameter
 * @param ptr Its node component
 * @param params Its parameter bytes, zeroed
 * @param offset Offset of its parameter bytes in the graph parameters
 */
static void box_texture_graph_array(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind,
    const uint8_t *ptr,
    uint8_t *params,
    int32_t offset)
{
    const ecs_vec_t *vec = (const ecs_vec_t*)(ptr + kind->array.offset);
    int32_t i, count = ecs_vec_count(vec);
    int32_t first = kind->param_size + kind->input_count * 16;
    uint32_t *header = (uint32_t*)(params + kind->array_header);
    uint32_t *elements = (uint32_t*)(params + first);
    char *problem = NULL;
    if (count > kind->array.max) {
        char *name = box_texture_graph_name(b->world, e);
        box_texture_graph_error(b, "node '%s' has %d elements in %s, more than %d", name,
            count, kind->array.name, kind->array.max);
        ecs_os_free(name);
        goto done;
    }

    header[0] = (uint32_t)((offset + first) / 16);
    header[1] = (uint32_t)count;
    if (kind->array.pack) {
        problem = kind->array.pack(ptr, count, elements);
    } else {
        const uint8_t *data = ecs_vec_first(vec);
        for (i = 0; i < count; i ++) {
            ecs_os_memcpy(&elements[i * 4], data + i * kind->array.size, kind->array.size);
        }
    }

    if (problem) {
        char *name = box_texture_graph_name(b->world, e);
        box_texture_graph_error(b, "node '%s': %s", name, problem);
        ecs_os_free(name);
        ecs_os_free(problem);
    }

done:
    return;
}

/** Get the type that follows the inputs a node reads: a Vector when one of them has a Vector
 * node, else a Scalar when one has a Scalar node, else a Vector.
 *
 * @param inputs Type of the node of each input, TexAny for none
 * @param used Bit j set when input j is read
 * @return The type
 */
static TextureValueType box_texture_graph_follow(
    const TextureValueType *inputs,
    uint32_t used)
{
    TextureValueType result = TexVector;
    bool scalar = false, vector = false;
    int32_t i;
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS; i ++) {
        if (used & (1u << i)) {
            scalar = scalar || inputs[i] == TexScalar;
            vector = vector || inputs[i] == TexVector;
        }
    }

    if (scalar && !vector) {
        result = TexScalar;
    }

    return result;
}

/** Get the type of the value of a node.
 *
 * @param kind Its kind
 * @param ptr Its component
 * @param inputs Type of the node of each input, TexAny for none
 * @return The type, TexScalar or TexVector
 */
static TextureValueType box_texture_graph_output_type(
    const box_texture_node_kind_t *kind,
    const void *ptr,
    const TextureValueType *inputs)
{
    TextureValueType result = kind->output;
    if (result == TexAny && kind->output_type) {
        result = kind->output_type(ptr, inputs);
    }

    if (result == TexAny) {
        result = box_texture_graph_follow(inputs, (1u << kind->input_count) - 1);
    }

    return result;
}

/** Get the type of the value of a TexMath node: a Scalar for MathDot and MathLength, a Vector
 * for MathCross, else the type that follows the inputs its operation reads.
 *
 * @param node The TexMath
 * @param inputs Type of the node of each input, TexAny for none
 * @return The type
 */
static TextureValueType box_texture_math_type(
    const void *node,
    const TextureValueType *inputs)
{
    const TexMath *math = node;
    TextureValueType result;
    uint32_t used = 3;
    switch (math->op) {
    case MathDot:
    case MathLength:
        result = TexScalar;
        break;
    case MathCross:
        result = TexVector;
        break;
    case MathAbs:
    case MathNegate:
    case MathFloor:
    case MathFract:
    case MathSin:
    case MathCos:
    case MathRemap:
    case MathRemapClamped:
    case MathNormalize:
        used = 1;
        result = box_texture_graph_follow(inputs, used);
        break;
    case MathMix:
    case MathClamp:
    case MathSmoothstep:
        used = 7;
        result = box_texture_graph_follow(inputs, used);
        break;
    default:
        result = box_texture_graph_follow(inputs, used);
        break;
    }

    return result;
}

/** Append the compiled node of an entity whose inputs are compiled, with its parameters and
 * the constants of its inputs without a node.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind
 * @param inputs Index of the node of each input, -1 for none
 * @return Index of the node
 */
static int32_t box_texture_graph_append(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind,
    const int32_t *inputs)
{
    const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
    int32_t i, offset = ecs_vec_count(&b->params);
    int32_t result = ecs_vec_count(&b->nodes);
    TextureValueType types[BOX_TEXTURE_NODE_INPUTS] = { TexAny, TexAny, TexAny, TexAny };
    uint32_t word = 0;
    for (i = 0; i < kind->input_count; i ++) {
        if (inputs[i] >= 0) {
            types[i] = ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, inputs[i])->output;
            word |= types[i] == TexScalar ? (1u << i) : (1u << (4 + i));
        }
    }

    uint8_t *params = ecs_vec_grow_t(NULL, &b->params, uint8_t, kind->node_size);
    ecs_os_memset(params, 0, kind->node_size);
    for (i = 0; i < kind->param_count; i ++) {
        ecs_os_memcpy(params + kind->param_offsets[i], ptr + kind->params[i].offset,
            kind->params[i].size);
    }

    for (i = 0; i < kind->input_count; i ++) {
        const box_texture_node_input_t *in = &kind->inputs[i];
        if (inputs[i] < 0 && in->value) {
            ecs_os_memcpy(params + kind->param_size + i * 16, ptr + in->value_offset,
                in->type == TexScalar ? ECS_SIZEOF(float) : 4 * ECS_SIZEOF(float));
        }
    }

    if (kind->types_offset >= 0) {
        ecs_os_memcpy(params + kind->types_offset, &word, ECS_SIZEOF(uint32_t));
    }

    if (kind->array.name) {
        box_texture_graph_array(b, e, kind, ptr, params, offset);
    }

    box_texture_graph_node_t *node = ecs_vec_append_t(NULL, &b->nodes,
        box_texture_graph_node_t);
    ecs_os_memset_t(node, 0, box_texture_graph_node_t);
    node->entity = e;
    node->kind = kind->id;
    node->output = box_texture_graph_output_type(kind, ptr, types);
    node->params = offset;
    node->param_size = kind->node_size;
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS; i ++) {
        node->inputs[i] = i < kind->input_count ? inputs[i] : -1;
    }

    *ecs_map_ensure(&b->index, e) = (ecs_map_val_t)(result + 1);

    return result;
}

/** Check that a node outputs the type an input expects.
 *
 * @param b The build
 * @param e The node entity
 * @param output Type of the node
 * @param from Node of the input, 0 for the output of the graph
 * @param input Name of the input
 * @param expected Type the input expects
 */
static void box_texture_graph_check_type(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    TextureValueType output,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    if (from && expected != TexAny && output != expected) {
        char *name = box_texture_graph_name(b->world, e);
        char *from_name = box_texture_graph_name(b->world, from);
        box_texture_graph_error(b, "node '%s' outputs %s but input '%s' of node '%s' expects "
            "%s", name, output == TexScalar ? "Scalar" : "Vector", input, from_name,
            expected == TexScalar ? "Scalar" : "Vector");
        ecs_os_free(from_name);
        ecs_os_free(name);
    }
}

/** Visit a node, compiling its inputs before it.
 *
 * @param b The build
 * @param e The node entity
 * @param from Node whose input it is, 0 for the output of the graph
 * @param input Name of that input
 * @param expected Type that input expects
 * @return Index of the node, -1 when the build failed or waits
 */
static int32_t box_texture_graph_visit(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    int32_t result = -1, i, kinds = 0;
    int32_t inputs[BOX_TEXTURE_NODE_INPUTS] = { -1, -1, -1, -1 };
    char *name = NULL;
    box_texture_node_kind_t *kind = NULL;
    const ecs_map_val_t *visited = NULL;
    box_texture_graph_reach(b, e);
    if (!ecs_is_alive(b->world, e)) {
        char *from_name = from ? box_texture_graph_name(b->world, from) : NULL;
        if (from_name) {
            box_texture_graph_error(b, "input '%s' of node '%s' is a missing node #%u", input,
                from_name, (uint32_t)e);
        } else {
            box_texture_graph_error(b, "the output is a missing node #%u", (uint32_t)e);
        }

        ecs_os_free(from_name);
        goto done;
    }

    name = ecs_get_path(b->world, e);
    visited = ecs_map_get(&b->index, e);
    if (visited && !*visited) {
        box_texture_graph_error(b, "node '%s' is part of a cycle", name);
        goto done;
    }

    kind = box_texture_graph_kind_of(b, e, &kinds);
    if (visited) {
        result = (int32_t)*visited - 1;
        box_texture_graph_check_type(b, e,
            ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, result)->output, from, input,
            expected);
        goto done;
    }

    if (kinds > 1) {
        box_texture_graph_error(b, "node '%s' has more than one node component", name);
        goto done;
    }

    if (!kind && b->wait) {
        b->waiting = true;
        goto done;
    }

    if (!kind) {
        box_texture_graph_error(b, "node '%s' has no node component", name);
        goto done;
    }

    if ((kind->flags & BOX_TEXTURE_NODE_PLANE) && b->domain == DomainSphere) {
        char *component = ecs_get_path(b->world, kind->component);
        box_texture_graph_error(b, "node '%s' (%s) works on DomainPlane only, the graph is on "
            "DomainSphere", name, component);
        ecs_os_free(component);
    }

    if (ecs_vec_count(&b->nodes) >= BOX_TEXTURE_GRAPH_NODES) {
        box_texture_graph_error(b, "more than %d nodes", BOX_TEXTURE_GRAPH_NODES);
    }

    if (b->error) {
        goto done;
    }

    *ecs_map_ensure(&b->index, e) = 0;
    for (i = 0; i < kind->input_count && !b->error && !b->waiting; i ++) {
        const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
        ecs_entity_t in = *(const ecs_entity_t*)(ptr + kind->inputs[i].offset);
        if (in) {
            inputs[i] = box_texture_graph_visit(b, in, e, kind->inputs[i].name,
                kind->inputs[i].type);
        }
    }

    if (!b->error && !b->waiting) {
        result = box_texture_graph_append(b, e, kind, inputs);
        box_texture_graph_check_type(b, e,
            ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, result)->output, from, input,
            expected);
    }

done:
    ecs_os_free(name);

    return result;
}

/** Compile the walk of a build into a graph with its hashes.
 *
 * @param b The build
 * @param graph The TextureGraph
 * @return The compiled graph, owned by the caller
 */
static box_texture_graph_t* box_texture_graph_compile(
    box_texture_graph_build_t *b,
    const TextureGraph *graph)
{
    box_texture_graph_t *result = ecs_os_calloc_t(box_texture_graph_t);
    int32_t i;
    box_texture_capped_size(b->world, b->t, graph->width, graph->height, &result->width,
        &result->height);
    result->domain = graph->domain;
    result->format = graph->format;
    result->rate = graph->rate > 0 ? graph->rate : 0;
    result->node_count = ecs_vec_count(&b->nodes);
    result->nodes = ecs_os_memdup(ecs_vec_first(&b->nodes),
        result->node_count * ECS_SIZEOF(box_texture_graph_node_t));
    result->param_size = ecs_vec_count(&b->params);
    result->params = result->param_size
        ? ecs_os_memdup(ecs_vec_first(&b->params), result->param_size)
        : NULL;
    result->structure_hash = box_texture_graph_hash(BOX_TEXTURE_GRAPH_FNV_BASIS,
        &result->node_count, ECS_SIZEOF(int32_t));
    for (i = 0; i < result->node_count; i ++) {
        const box_texture_graph_node_t *node = &result->nodes[i];
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->kind,
            ECS_SIZEOF(int32_t));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, node->inputs,
            ECS_SIZEOF(node->inputs));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->output,
            ECS_SIZEOF(TextureValueType));
    }

    result->param_hash = box_texture_graph_hash(BOX_TEXTURE_GRAPH_FNV_BASIS, result->params,
        result->param_size);

    return result;
}

/** Check whether two compiled graphs make the same image.
 *
 * @param a The first graph
 * @param b The second graph
 * @return Whether their size, domain, format, rate, structure and parameters are the same
 */
static bool box_texture_graph_same(
    const box_texture_graph_t *a,
    const box_texture_graph_t *b)
{
    return a->width == b->width && a->height == b->height && a->domain == b->domain &&
        a->format == b->format && a->rate == b->rate && a->structure_hash == b->structure_hash &&
        a->param_hash == b->param_hash && a->node_count == b->node_count &&
        a->param_size == b->param_size &&
        (!a->param_size || !ecs_os_memcmp(a->params, b->params, a->param_size));
}

/** Check whether the backend of a world evaluates the graphs and reports their outcome, so
 * a graph is ready once its image was written: the GPU backend, unless its device failed.
 *
 * @param world The world or a stage
 * @return Whether Texture and TextureStatus wait for the outcome of the backend
 */
static bool box_texture_graph_deferred(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;

    return box_render_gpu_stats(world, &stats) && !stats.failed;
}

/** Take a compiled graph into the entry of its texture and queue it for the backend, unless
 * it makes the image the backend already has.
 *
 * With a backend that evaluates the graphs the texture stays loading until the outcome of its
 * evaluation arrives from the render extract, else it is ready at once.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param graph The compiled graph, takes ownership
 */
static void box_texture_graph_accept(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    box_texture_graph_t *graph)
{
    box_texture_t *tex = box_texture_entry(t, e);
    int32_t revision = box_texture_revision(world, e);
    bool deferred = box_texture_graph_deferred(world);
    if (tex->graph && box_texture_graph_same(tex->graph, graph)) {
        ecs_os_free(graph->nodes);
        ecs_os_free(graph->params);
        ecs_os_free(graph);
        box_texture_status(world, e, tex->graph_pending ? TextureLoading : TextureReady,
            revision, NULL);
        goto done;
    }

    if (tex->graph) {
        ecs_os_free(tex->graph->nodes);
        ecs_os_free(tex->graph->params);
        ecs_os_free(tex->graph);
    }

    tex->graph = graph;
    tex->loaded = true;
    tex->width = graph->width;
    tex->height = graph->height;
    tex->single_channel = graph->format == GraphSingle;
    tex->graph_serial ++;
    tex->graph_pending = deferred;
    if (!tex->graph_queued) {
        tex->graph_queued = true;
        *ecs_vec_append_t(NULL, &t->graph_queued, ecs_entity_t) = e;
    }

    if (deferred) {
        box_texture_status(world, e, TextureLoading, revision, NULL);
        goto done;
    }

    ecs_set(world, e, Texture, { .width = graph->width, .height = graph->height });
    box_texture_status(world, e, TextureReady, revision + 1, NULL);
done:
    return;
}

/** Build the TextureGraph of a texture: walk it from its output, check it and compile it.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param wait Wait a frame for a node without a node component instead of failing
 */
static void box_texture_graph_build(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    bool wait)
{
    const TextureGraph *graph = ecs_get(world, e, TextureGraph);
    box_texture_t *tex = box_texture_entry(t, e);
    box_texture_graph_build_t b = {
        .world = world, .t = t, .graph = e, .reached = &tex->reached, .wait = wait,
        .domain = graph->domain
    };
    tex->graph_dirty = false;
    tex->graph_waiting = false;
    if (ecs_has(world, e, TexturePath) || ecs_has(world, e, TextureGen)) {
        box_texture_load(world, t, e);
        goto done;
    }

    b.name = ecs_get_path(world, e);
    ecs_map_init(&b.index, NULL);
    ecs_vec_init_t(NULL, &b.nodes, box_texture_graph_node_t, 0);
    ecs_vec_init_t(NULL, &b.params, uint8_t, 0);
    ecs_vec_clear(&tex->reached);
    if (!graph->output) {
        box_texture_graph_error(&b, "no output node");
    } else {
        box_texture_graph_visit(&b, graph->output, 0, NULL, TexVector);
    }

    if (b.error) {
        box_texture_fail(world, t, e, b.error);
    } else if (b.waiting) {
        tex->graph_waiting = true;
        box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
    } else {
        box_texture_graph_accept(world, t, e, box_texture_graph_compile(&b, graph));
    }

    ecs_os_free(b.error);
    ecs_os_free(b.name);
    ecs_map_fini(&b.index);
    ecs_vec_fini_t(NULL, &b.nodes, box_texture_graph_node_t);
    ecs_vec_fini_t(NULL, &b.params, uint8_t);
done:
    return;
}

/** Load the textures whose source component was removed, then build the graphs that changed
 * and fail the ones that waited a frame for a node without a node component.
 *
 * @param it The iterator of the system
 */
static void BoxTextureGraphBuild(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    box_textures_t *t = box_textures_get(world);
    ecs_vec_t build;
    int32_t i, count;
    if (!t) {
        goto done;
    }

    ecs_vec_init_t(NULL, &build, ecs_entity_t, 0);
    count = ecs_vec_count(&t->reload);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &build, ecs_entity_t) =
            *ecs_vec_get_t(&t->reload, ecs_entity_t, i);
    }

    ecs_vec_clear(&t->reload);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&build, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            box_texture_load(world, t, e);
        }
    }

    ecs_vec_clear(&build);
    ecs_map_iter_t mit = ecs_map_iter(&t->textures);
    while (ecs_map_next(&mit)) {
        box_texture_t *tex = ecs_map_ptr(&mit);
        if (tex->graph_dirty || tex->graph_waiting) {
            *ecs_vec_append_t(NULL, &build, ecs_entity_t) = tex->entity;
        }
    }

    count = ecs_vec_count(&build);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&build, ecs_entity_t, i);
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
        if (!tex) {
            continue;
        }

        bool wait = !tex->graph_waiting;
        if (!ecs_is_alive(world, e) || !ecs_has(world, e, TextureGraph)) {
            tex->graph_dirty = false;
            tex->graph_waiting = false;
            continue;
        }

        box_texture_graph_build(world, t, e, wait);
    }

    ecs_vec_fini_t(NULL, &build, ecs_entity_t);
done:
    return;
}

<<<<<<< HEAD
/** Register the built in node kinds: TexConstant, TexCoords, TexMath and TexCells.
=======
/** Pack the keys of a TexRamp: (position, r, g, b) with the display encoded channels in 0..1.
 *
 * @param node The TexRamp
 * @param count Number of keys
 * @param out Receives a vec4 per key
 * @return NULL, every ramp is valid
 */
static char* box_texture_ramp_pack(
    const void *node,
    int32_t count,
    uint32_t *out)
{
    const TexRamp *ramp = node;
    const TextureGenKey *keys = ecs_vec_first_t(&ramp->keys, TextureGenKey);
    float *f = (float*)out;
    int32_t i;
    for (i = 0; i < count; i ++) {
        f[i * 4] = keys[i].position;
        f[i * 4 + 1] = keys[i].color.r / 255.0f;
        f[i * 4 + 2] = keys[i].color.g / 255.0f;
        f[i * 4 + 3] = keys[i].color.b / 255.0f;
    }

    return NULL;
}

/** Pack the keys of the curve of a TexCurve: (time, value, control1, control2) with the
 * controls of the span from the key to the next, box_curve_span_controls, zero for the last.
 *
 * @param node The TexCurve
 * @param count Number of keys
 * @param out Receives a vec4 per key
 * @return Why the curve is invalid, NULL when it is valid
 */
static char* box_texture_curve_pack(
    const void *node,
    int32_t count,
    uint32_t *out)
{
    const TexCurve *curve = node;
    const CurveKey *keys = ecs_vec_first_t(&curve->curve.keys, CurveKey);
    float *f = (float*)out;
    char *problem = box_curve_validate(&curve->curve);
    char *result = NULL;
    int32_t i;
    if (problem) {
        result = flecs_asprintf("curve %s", problem);
        ecs_os_free(problem);
        goto done;
    }

    for (i = 0; i < count; i ++) {
        f[i * 4] = keys[i].time;
        f[i * 4 + 1] = keys[i].value;
        if (i + 1 < count) {
            box_curve_span_controls(&curve->curve, i, &f[i * 4 + 2], &f[i * 4 + 3]);
        }
    }

done:
    return result;
}

/** Register the built in node kinds: TexConstant, TexCoords, TexMath, TexSplit, TexCombine,
 * TexWarp, TexTransform, TexTime, TexSplat, TexRamp, TexCurve and TexBlocks.
>>>>>>> ec4963cf181bd9d1a403187fb2437ae201c7c186
 *
 * @param world The world
 */
static void box_texture_nodes_register(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexConstant),
        .output = TexVector,
        .params = {{ "value", offsetof(TexConstant, value), 16, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_constant",
        .source = box_texture_constant_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCoords),
        .output = TexVector,
        .params = {{ "scale", offsetof(TexCoords, scale), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_coords",
        .source = box_texture_coords_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMath),
        .output = TexAny,
        .output_type = box_texture_math_type,
        .inputs = {
            { "a", offsetof(TexMath, a), TexAny, "a_value", offsetof(TexMath, a_value) },
            { "b", offsetof(TexMath, b), TexAny, "b_value", offsetof(TexMath, b_value) },
            { "c", offsetof(TexMath, c), TexAny, "c_value", offsetof(TexMath, c_value) }
        },
        .params = {
            { "op", offsetof(TexMath, op), 4, BOX_TEXTURE_PARAM_I32 },
            { "from_min", offsetof(TexMath, from_min), 4, BOX_TEXTURE_PARAM_F32 },
            { "from_max", offsetof(TexMath, from_max), 4, BOX_TEXTURE_PARAM_F32 },
            { "to_min", offsetof(TexMath, to_min), 4, BOX_TEXTURE_PARAM_F32 },
            { "to_max", offsetof(TexMath, to_max), 4, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "box_tex_math",
        .source = box_texture_math_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
<<<<<<< HEAD
        .component = ecs_id(TexCells),
        .output = TexVector,
        .inputs = {
            { "coords", offsetof(TexCells, coords), TexVector, .coords = true },
            { "evolve", offsetof(TexCells, evolve), TexScalar, "evolve_value",
                offsetof(TexCells, evolve_value) }
        },
        .params = {
            { "count", offsetof(TexCells, count), 4, BOX_TEXTURE_PARAM_I32 },
            { "jitter", offsetof(TexCells, jitter), 4, BOX_TEXTURE_PARAM_F32 },
            { "seed", offsetof(TexCells, seed), 4, BOX_TEXTURE_PARAM_I32 },
            { "distance", offsetof(TexCells, distance), 4, BOX_TEXTURE_PARAM_I32 },
            { "edge", offsetof(TexCells, edge), 4, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "box_tex_cells",
        .source = box_texture_cells_wgsl
=======
        .component = ecs_id(TexSplit),
        .output = TexScalar,
        .inputs = {
            { "input", offsetof(TexSplit, input), TexVector, "value", offsetof(TexSplit, value) }
        },
        .params = {{ "channel", offsetof(TexSplit, channel), 4, BOX_TEXTURE_PARAM_I32 }},
        .function = "box_tex_split",
        .source = box_texture_split_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCombine),
        .output = TexVector,
        .inputs = {
            { "r", offsetof(TexCombine, r), TexScalar, "r_value", offsetof(TexCombine, r_value) },
            { "g", offsetof(TexCombine, g), TexScalar, "g_value", offsetof(TexCombine, g_value) },
            { "b", offsetof(TexCombine, b), TexScalar, "b_value", offsetof(TexCombine, b_value) },
            { "a", offsetof(TexCombine, a), TexScalar, "a_value", offsetof(TexCombine, a_value) }
        },
        .function = "box_tex_combine",
        .source = box_texture_combine_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexWarp),
        .output = TexVector,
        .inputs = {
            { "coords", offsetof(TexWarp, coords), TexVector, NULL, 0, true },
            { "offset", offsetof(TexWarp, offset), TexVector, "offset_value",
                offsetof(TexWarp, offset_value) },
            { "amount", offsetof(TexWarp, amount), TexScalar, "amount_value",
                offsetof(TexWarp, amount_value) }
        },
        .function = "box_tex_warp",
        .source = box_texture_warp_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexTransform),
        .output = TexVector,
        .inputs = {
            { "coords", offsetof(TexTransform, coords), TexVector, NULL, 0, true },
            { "angle", offsetof(TexTransform, angle), TexScalar, "angle_value",
                offsetof(TexTransform, angle_value) }
        },
        .params = {
            { "axis", offsetof(TexTransform, axis), 12, BOX_TEXTURE_PARAM_F32 },
            { "translate", offsetof(TexTransform, translate), 12, BOX_TEXTURE_PARAM_F32 },
            { "scale", offsetof(TexTransform, scale), 12, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "box_tex_transform",
        .source = box_texture_transform_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexTime),
        .output = TexScalar,
        .params = {
            { "scale", offsetof(TexTime, scale), 4, BOX_TEXTURE_PARAM_F32 },
            { "offset", offsetof(TexTime, offset), 4, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "box_tex_time",
        .source = box_texture_time_wgsl,
        .flags = BOX_TEXTURE_NODE_ANIMATED
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexSplat),
        .output = TexVector,
        .inputs = {
            { "in", offsetof(TexSplat, in), TexScalar, "in_value", offsetof(TexSplat, in_value) }
        },
        .function = "box_tex_splat",
        .source = box_texture_splat_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexRamp),
        .output = TexVector,
        .inputs = {
            { "input", offsetof(TexRamp, input), TexScalar, "input_value",
                offsetof(TexRamp, input_value) }
        },
        .array = { "keys", offsetof(TexRamp, keys), ECS_SIZEOF(TextureGenKey),
            BOX_TEXTURE_RAMP_KEYS, box_texture_ramp_pack },
        .function = "box_tex_ramp",
        .source = box_texture_ramp_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCurve),
        .output = TexScalar,
        .inputs = {
            { "input", offsetof(TexCurve, input), TexScalar, "input_value",
                offsetof(TexCurve, input_value) }
        },
        .array = { "curve.keys", offsetof(TexCurve, curve.keys), ECS_SIZEOF(CurveKey),
            BOX_TEXTURE_CURVE_KEYS, box_texture_curve_pack },
        .function = "box_tex_curve",
        .source = box_texture_curve_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexBlocks),
        .output = TexVector,
        .inputs = {{ "coords", offsetof(TexBlocks, coords), TexVector, NULL, 0, true }},
        .params = {
            { "courses", offsetof(TexBlocks, courses), 4, BOX_TEXTURE_PARAM_I32 },
            { "per_course", offsetof(TexBlocks, per_course), 4, BOX_TEXTURE_PARAM_I32 },
            { "tones", offsetof(TexBlocks, tones), 4, BOX_TEXTURE_PARAM_I32 },
            { "tone_order", offsetof(TexBlocks, tone_order), 4, BOX_TEXTURE_PARAM_I32 },
            { "gap", offsetof(TexBlocks, gap), 4, BOX_TEXTURE_PARAM_F32 },
            { "jitter", offsetof(TexBlocks, jitter), 4, BOX_TEXTURE_PARAM_F32 },
            { "stagger", offsetof(TexBlocks, stagger), 4, BOX_TEXTURE_PARAM_F32 },
            { "softness", offsetof(TexBlocks, softness), 4, BOX_TEXTURE_PARAM_F32 },
            { "seed", offsetof(TexBlocks, seed), 4, BOX_TEXTURE_PARAM_I32 },
            { "layer_seed", offsetof(TexBlocks, layer_seed), 4, BOX_TEXTURE_PARAM_I32 }
        },
        .function = "box_tex_blocks",
        .source = box_texture_blocks_wgsl,
        .flags = BOX_TEXTURE_NODE_PLANE
>>>>>>> ec4963cf181bd9d1a403187fb2437ae201c7c186
    });
}

void box_texture_graphs_init(
    ecs_world_t *world,
    box_textures_t *t)
{
    ecs_vec_init_t(NULL, &t->node_kinds, box_texture_node_kind_t*, 0);
    ecs_vec_init_t(NULL, &t->graph_queued, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &t->graph_released, uint32_t, 0);
    ecs_vec_init_t(NULL, &t->reload, ecs_entity_t, 0);
    box_texture_nodes_register(world);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextureGraphBuild" }),
        .phase = EcsOnStore,
        .run = BoxTextureGraphBuild
    });
}
