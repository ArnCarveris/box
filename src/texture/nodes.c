/**
 * @file texture/nodes.c
 * @brief The built in node kinds of texture graphs: their WGSL functions and registration.
 */

#include "texture.h"

/** WGSL of the TexConstant node: its value */
static const char *box_texture_constant_wgsl =
    "fn box_tex_constant(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return bitcast<vec4f>(p[0]);\n"
    "}\n";

/** WGSL of the TexCoords node: the coordinates times its scale */
static const char *box_texture_coords_wgsl =
    "fn box_tex_coords(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(coords.xyz * bitcast<f32>(p[0].x), coords.w);\n"
    "}\n";

/** WGSL of the TexBlur node: a gaussian along x in pass 0, along y in pass 1, normalized by
 * the sum of its weights. At most BOX_TEX_BLUR_TAPS taps on each side; a larger reach spaces
 * them further apart, between texels. */
static const char *box_texture_blur_wgsl =
    "const BOX_TEX_BLUR_TAPS: i32 = 64;\n"
    "\n"
    "fn box_tex_blur_tap(t: texture_2d<f32>, texel: vec2i, axis: u32, offset: f32) -> vec4f {\n"
    "    let o = floor(offset);\n"
    "    let f = offset - o;\n"
    "    let dir = select(vec2i(0, 1), vec2i(1, 0), axis == 0u);\n"
    "    let a = texel + dir * i32(o);\n"
    "    return mix(box_texgraph_load_poles(t, a), box_texgraph_load_poles(t, a + dir), f);\n"
    "}\n"
    "\n"
    "fn box_tex_blur_axis(t: texture_2d<f32>, texel: vec2i, axis: u32, radius: f32) -> vec4f {\n"
    "    if (radius <= 0.0) {\n"
    "        return box_texgraph_load_poles(t, texel);\n"
    "    }\n"
    "    let taps = clamp(i32(ceil(radius)), 1, BOX_TEX_BLUR_TAPS);\n"
    "    let spacing = radius / f32(taps);\n"
    "    let sigma = radius / 3.0;\n"
    "    var sum = vec4f(0.0);\n"
    "    var total = 0.0;\n"
    "    for (var k = -taps; k <= taps; k ++) {\n"
    "        let o = f32(k) * spacing;\n"
    "        let w = exp(-0.5 * o * o / (sigma * sigma));\n"
    "        sum += w * box_tex_blur_tap(t, texel, axis, o);\n"
    "        total += w;\n"
    "    }\n"
    "    return sum / total;\n"
    "}\n"
    "\n"
    "fn box_tex_blur(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    pass_index: u32, prev: texture_2d<f32>, t0: texture_2d<f32>) -> vec4f {\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let radius = max(bitcast<f32>(p[0].x), 0.0) * size.x / 512.0;\n"
    "    let sphere = box_texgraph_stage.domain == 1u;\n"
    "    if (pass_index == 0u) {\n"
    "        if (any(textureDimensions(t0) != box_texgraph_stage.size)) {\n"
    "            return i[0];\n"
    "        }\n"
    "        var r = radius;\n"
    "        if (sphere) {\n"
    "            let s = sin((f32(texel.y) + 0.5) / size.y * BOX_TEXGRAPH_PI);\n"
    "            r = min(radius / max(s, 1e-6), size.x * 0.5);\n"
    "        }\n"
    "        return box_tex_blur_axis(t0, texel, 0u, r);\n"
    "    }\n"
    "    return box_tex_blur_axis(prev, texel, 1u,\n"
    "        select(radius, radius * 2.0 * size.y / size.x, sphere));\n"
    "}\n";

/** WGSL of the TexDistance node, a jump flood in BOX_TEX_DISTANCE_PASSES passes whose textures
 * hold the index y * width + x of the nearest seed found, -1 for none.
 *
 * Pass 0 seeds the texels that have a neighbour across the 0.5 contour. The passes before
 * BOX_TEX_DISTANCE_LAST look at the seeds of the texels a step away, halving the step from the
 * reach of the range.
 *
 * The last takes a step of one texel and measures the distance to the contour points of the
 * seeds it finds: where the input, interpolated linearly along an edge between the seed and a
 * neighbour across the contour, is 0.5. */
static const char *box_texture_distance_wgsl =
    "const BOX_TEX_DISTANCE_LAST: u32 = 11u;\n"
    "const BOX_TEX_DISTANCE_STEP_MAX: u32 = 512u;\n"
    "\n"
    "fn box_tex_distance_seed(index: f32) -> vec2i {\n"
    "    let w = i32(box_texgraph_stage.size.x);\n"
    "    let k = i32(index);\n"
    "    return vec2i(k % w, k / w);\n"
    "}\n"
    "\n"
    "fn box_tex_distance_dir(pos: vec2f) -> vec3f {\n"
    "    let uv = pos / vec2f(box_texgraph_stage.size);\n"
    "    let phi = (uv.x - 0.5) * 2.0 * BOX_TEXGRAPH_PI;\n"
    "    let theta = clamp(uv.y, 0.0, 1.0) * BOX_TEXGRAPH_PI;\n"
    "    return vec3f(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi));\n"
    "}\n"
    "\n"
    "fn box_tex_distance_between(texel: vec2i, pos: vec2f) -> f32 {\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let c = vec2f(texel) + 0.5;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        let chord = length(box_tex_distance_dir(pos) - box_tex_distance_dir(c));\n"
    "        return 2.0 * asin(min(chord * 0.5, 1.0)) * 256.0 / BOX_TEXGRAPH_PI;\n"
    "    }\n"
    "    var d = pos - c;\n"
    "    d -= size * round(d / size);\n"
    "    return length(d) * 512.0 / size.x;\n"
    "}\n"
    "\n"
    "fn box_tex_distance_contour(t: texture_2d<f32>, texel: vec2i, seed: vec2i) -> f32 {\n"
    "    var dirs = array<vec2i, 4>(vec2i(1, 0), vec2i(-1, 0), vec2i(0, 1), vec2i(0, -1));\n"
    "    let v = box_texgraph_load_poles(t, seed).x;\n"
    "    let inside = v >= 0.5;\n"
    "    var best = 3.4e38;\n"
    "    for (var k = 0; k < 4; k ++) {\n"
    "        let n = box_texgraph_load_poles(t, seed + dirs[k]).x;\n"
    "        if ((n >= 0.5) == inside) {\n"
    "            continue;\n"
    "        }\n"
    "        let f = clamp((v - 0.5) / (v - n), 0.0, 1.0);\n"
    "        let at = vec2f(seed) + 0.5 + f * vec2f(dirs[k]);\n"
    "        best = min(best, box_tex_distance_between(texel, at));\n"
    "    }\n"
    "    return best;\n"
    "}\n"
    "\n"
    "fn box_tex_distance(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    pass_index: u32, prev: texture_2d<f32>, t0: texture_2d<f32>) -> vec4f {\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let range = max(bitcast<f32>(p[0].x), 0.0);\n"
    "    let has_input = all(textureDimensions(t0) == box_texgraph_stage.size);\n"
    "    if (pass_index == 0u) {\n"
    "        if (!has_input) {\n"
    "            return vec4f(-1.0, 0.0, 0.0, 0.0);\n"
    "        }\n"
    "        let inside = box_texgraph_load_poles(t0, texel).x >= 0.5;\n"
    "        let e = box_texgraph_load_poles(t0, texel + vec2i(1, 0)).x >= 0.5;\n"
    "        let w = box_texgraph_load_poles(t0, texel - vec2i(1, 0)).x >= 0.5;\n"
    "        let s = box_texgraph_load_poles(t0, texel + vec2i(0, 1)).x >= 0.5;\n"
    "        let n = box_texgraph_load_poles(t0, texel - vec2i(0, 1)).x >= 0.5;\n"
    "        let edge = (e != inside) || (w != inside) || (s != inside) || (n != inside);\n"
    "        let index = f32(texel.y * i32(box_texgraph_stage.size.x) + texel.x);\n"
    "        return vec4f(select(-1.0, index, edge), 0.0, 0.0, 0.0);\n"
    "    }\n"
    "\n"
    "    let last = pass_index == BOX_TEX_DISTANCE_LAST;\n"
    "    var stride = 1u;\n"
    "    if (!last) {\n"
    "        let reach = range * size.x / 512.0;\n"
    "        var s = 1u;\n"
    "        while (f32(s) < reach && s < BOX_TEX_DISTANCE_STEP_MAX) {\n"
    "            s = s * 2u;\n"
    "        }\n"
    "        stride = s >> (pass_index - 1u);\n"
    "        if (stride == 0u) {\n"
    "            return textureLoad(prev, texel, 0);\n"
    "        }\n"
    "    }\n"
    "\n"
    "    let sy = i32(stride);\n"
    "    var sx = sy;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        let s = sin((f32(texel.y) + 0.5) / size.y * BOX_TEXGRAPH_PI);\n"
    "        sx = max(i32(min(round(f32(stride) / max(s, 1e-6)), size.x * 0.5)), 1);\n"
    "    }\n"
    "\n"
    "    var best = -1.0;\n"
    "    var best_d = 3.4e38;\n"
    "    for (var dy = -1; dy <= 1; dy ++) {\n"
    "        for (var dx = -1; dx <= 1; dx ++) {\n"
    "            let index = box_texgraph_load_poles(prev, texel + vec2i(dx * sx, dy * sy)).x;\n"
    "            if (index < 0.0) {\n"
    "                continue;\n"
    "            }\n"
    "            let seed = box_tex_distance_seed(index);\n"
    "            var d = 0.0;\n"
    "            if (last) {\n"
    "                d = box_tex_distance_contour(t0, texel, seed);\n"
    "            } else {\n"
    "                d = box_tex_distance_between(texel, vec2f(seed) + 0.5);\n"
    "            }\n"
    "            if (d < best_d) {\n"
    "                best_d = d;\n"
    "                best = index;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    if (!last) {\n"
    "        return vec4f(best, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "\n"
    "    var v = i[0].x;\n"
    "    if (has_input) {\n"
    "        v = box_texgraph_load_poles(t0, texel).x;\n"
    "    }\n"
    "    let side = select(-1.0, 1.0, v >= 0.5);\n"
    "    if (best < 0.0) {\n"
    "        return vec4f(side * range, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    return vec4f(side * min(best_d, range), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexNormal node: the normal of the differences of its height in the UV frame of
 * the domain, encoded as GenNormal stores it */
static const char *box_texture_normal_wgsl =
    "fn box_tex_normal(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    height: texture_2d<f32>) -> vec4f {\n"
    "    let d = box_texgraph_gradient(height, texel, bitcast<f32>(p[0].x))\n"
    "        * box_texgraph_normal_scale(texel);\n"
    "    let n = vec3f(-d.x, d.y, 1.0) * inverseSqrt(dot(d, d) + 1.0);\n"
    "    return vec4f(0.5 + 0.5 * n, 1.0);\n"
    "}\n";

/** WGSL of the TexSlope node: the length of the differences of its height */
static const char *box_texture_slope_wgsl =
    "fn box_tex_slope(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    height: texture_2d<f32>) -> vec4f {\n"
    "    let d = box_texgraph_gradient(height, texel, bitcast<f32>(p[0].x));\n"
    "    return vec4f(length(d), 0.0, 0.0, 0.0);\n"
    "}\n";

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
    "    let count = f32(max(bitcast<i32>(p[0].x), 1));\n"
    "    let jitter = clamp(bitcast<f32>(p[0].y), 0.0, 1.0);\n"
    "    let seed = p[0].z;\n"
    "    let mode = bitcast<i32>(p[0].w);\n"
    "    let edge = bitcast<f32>(p[1].x);\n"
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
    "        if (edge > 0.0) {\n"
    "            d = smoothstep(0.0, edge, f2 - f1);\n"
    "        } else {\n"
    "            d = select(0.0, 1.0, f2 - f1 > 0.0);\n"
    "        }\n"
    "    }\n"
    "\n"
    "    return vec4f(d, box_tex_cells_random(near, 6u), box_tex_cells_random(near, 7u),\n"
    "        box_tex_cells_random(near, 8u));\n"
    "}\n";

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
    "    let courses = clamp(counts.x, 1, 4096);\n"
    "    let n = clamp(counts.y, 1, 4096);\n"
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
    "    let bevel = select(1.3 * gap, softness, softness >= 0.0);\n"
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
    "    let scale = bitcast<vec3f>(p[2].xyz);\n"
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
    "    return vec4f(box_texgraph_stage.time * scale + offset, 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexSplat node: its scalar input in every channel */
static const char *box_texture_splat_wgsl =
    "fn box_tex_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x);\n"
    "}\n";

/** WGSL of the TexImage node: its texture at the uv of coords on the plane, at the
 * equirectangular uv of the direction of coords on the sphere, wrapped or clamped */
static const char *box_texture_image_wgsl =
    "fn box_tex_image(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    t: texture_2d<f32>) -> vec4f {\n"
    "    var uv = i[0].xy;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        uv = box_texgraph_equirect(i[0].xyz);\n"
    "    }\n"
    "    return box_texgraph_sample(t, uv, bitcast<i32>(p[0].x) == 0);\n"
    "}\n";

/** WGSL of the TexLut node: its texture at column u and row v, from the center of the first
 * texel to the center of the last one, clamped */
static const char *box_texture_lut_wgsl =
    "fn box_tex_lut(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    t: texture_2d<f32>) -> vec4f {\n"
    "    let size = vec2f(textureDimensions(t, 0));\n"
    "    let uv = clamp(vec2f(i[0].x, i[1].x), vec2f(0.0), vec2f(1.0));\n"
    "    return box_texgraph_sample(t, (uv * (size - 1.0) + 0.5) / size, false);\n"
    "}\n";

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

/** Register the built in node kinds of values and their math: TexConstant, TexCoords, the
 * math kinds such as TexAdd, TexNoise, TexBlur, TexDistance, TexSplit, TexCombine, TexWarp,
 * TexTransform, TexTime and TexSplat.
 *
 * @param world The world
 */
static void box_texture_nodes_register_math(
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

    box_texture_math_register(world);
    box_texture_noise_register(world);

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexBlur),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexBlur, input), TexAny, "input_value",
                offsetof(TexBlur, input_value) }
        },
        .params = {{ "radius", offsetof(TexBlur, radius), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_blur",
        .source = box_texture_blur_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD,
        .passes = 2
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexDistance),
        .output = TexScalar,
        .inputs = {
            { "input", offsetof(TexDistance, input), TexScalar, "input_value",
                offsetof(TexDistance, input_value) }
        },
        .params = {{ "range", offsetof(TexDistance, range), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_distance",
        .source = box_texture_distance_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD,
        .passes = BOX_TEX_DISTANCE_PASSES
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
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
        .flags = BOX_TEXTURE_NODE_ANIMATED | BOX_TEXTURE_NODE_TIME_SCALE
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
}

/** Register the built in node kinds of lookups, patterns and textures: TexRamp, TexCurve,
 * TexBlocks, TexCells, TexImage and TexLut.
 *
 * @param world The world
 */
static void box_texture_nodes_register_lookups(
    ecs_world_t *world)
{
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
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCells),
        .output = TexVector,
        .inputs = {
            { "coords", offsetof(TexCells, coords), TexVector, NULL, 0, true },
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
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexImage),
        .output = TexVector,
        .inputs = {{ "coords", offsetof(TexImage, coords), TexVector, NULL, 0, true }},
        .params = {{ "address", offsetof(TexImage, address), 4, BOX_TEXTURE_PARAM_I32 }},
        .function = "box_tex_image",
        .source = box_texture_image_wgsl,
        .texture = "texture",
        .texture_offset = offsetof(TexImage, texture),
        .linear = "linear",
        .linear_offset = offsetof(TexImage, linear)
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexLut),
        .output = TexVector,
        .inputs = {
            { "u", offsetof(TexLut, u), TexScalar, "u_value", offsetof(TexLut, u_value) },
            { "v", offsetof(TexLut, v), TexScalar, "v_value", offsetof(TexLut, v_value) }
        },
        .function = "box_tex_lut",
        .source = box_texture_lut_wgsl,
        .texture = "texture",
        .texture_offset = offsetof(TexLut, texture),
        .linear = "linear",
        .linear_offset = offsetof(TexLut, linear)
    });
}

/** Register the built in node kinds of the differences of a height: TexNormal and TexSlope.
 *
 * @param world The world
 */
static void box_texture_nodes_register_relief(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexNormal),
        .output = TexVector,
        .inputs = {
            { "height", offsetof(TexNormal, height), TexScalar, "height_value",
                offsetof(TexNormal, height_value) }
        },
        .params = {{ "strength", offsetof(TexNormal, strength), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_normal",
        .source = box_texture_normal_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexSlope),
        .output = TexScalar,
        .inputs = {
            { "height", offsetof(TexSlope, height), TexScalar, "height_value",
                offsetof(TexSlope, height_value) }
        },
        .params = {{ "strength", offsetof(TexSlope, strength), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_slope",
        .source = box_texture_slope_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    });
}

void box_texture_nodes_register(
    ecs_world_t *world)
{
    box_texture_nodes_register_math(world);
    box_texture_nodes_register_lookups(world);
    box_texture_nodes_register_relief(world);
}
