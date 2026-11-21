/**
 * @file texture/graph_erosion.c
 * @brief The TexErosion node kind of texture graphs: gullies and ridges that run down the
 * slope of a height, the look of terrain worn by water, as a function of the point.
 */

#include "texture.h"
#include <math.h>

/** Most octaves of a TexErosion node, the vec4 elements its window function writes */
#define BOX_TEX_EROSION_OCTAVES (8)

/** WGSL of the TexErosion node.
 *
 * Every octave is a lattice of jittered points. Each point carries a wave cos(2 pi dot(d, dir))
 * of the offset d from it, whose crests run along the slope: dir is the slope turned a quarter.
 * The waves of the 16 nearest points are blended by a gaussian of their distance.
 *
 * An octave also returns the derivative of its wave, and the next octave adds the sum of those,
 * turned a quarter, to its direction, so its gullies run into the gullies of the octaves before.
 * The lattice wraps on the plane and is shifted by the window on the world, as TexNoise. */
static const char *box_texture_erosion_wgsl =
    "// Offset -0.5..0.5 of the point of a lattice cell, x and y wrapped on the plane\n"
    "fn box_tex_erosion_point(seed: u32, c: vec2i, period: vec2i) -> vec2f {\n"
    "    var x = bitcast<u32>(c.x);\n"
    "    var y = bitcast<u32>(c.y);\n"
    "    if (period.x > 0) {\n"
    "        x = bitcast<u32>(((c.x % period.x) + period.x) % period.x);\n"
    "    }\n"
    "    if (period.y > 0) {\n"
    "        y = bitcast<u32>(((c.y % period.y) + period.y) % period.y);\n"
    "    }\n"
    "    let h = box_texgraph_hash(seed ^ box_texgraph_hash(x ^ box_texgraph_hash(y)));\n"
    "    return vec2f(box_texgraph_random(h),\n"
    "        box_texgraph_random(box_texgraph_hash(h ^ 0x9e3779b9u))) - 0.5;\n"
    "}\n"
    "\n"
    "// One octave at q in lattice cells: the height of the waves along dir in x and its\n"
    "// derivative in yz\n"
    "fn box_tex_erosion_octave(seed: u32, q: vec2f, dir: vec2f, period: vec2i, shift: vec2i)\n"
    "    -> vec3f {\n"
    "    let b = floor(q);\n"
    "    let cell = vec2i(b);\n"
    "    let f = q - b;\n"
    "    var sum = vec3f(0.0);\n"
    "    var weight = 0.0;\n"
    "    for (var y = -2; y <= 1; y ++) {\n"
    "        for (var x = -2; x <= 1; x ++) {\n"
    "            let o = vec2i(x, y);\n"
    "            let d = f + vec2f(o) - box_tex_erosion_point(seed, cell - o + shift, period);\n"
    "            let w = exp(-2.0 * dot(d, d));\n"
    "            let phase = 6.283185307179586 * dot(d, dir);\n"
    "            sum += vec3f(cos(phase), -sin(phase) * dir) * w;\n"
    "            weight += w;\n"
    "        }\n"
    "    }\n"
    "    return sum / weight;\n"
    "}\n"
    "\n"
    "fn box_tex_erosion(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, window: u32)\n"
    "    -> vec4f {\n"
    "    let frequency = bitcast<f32>(p[0].x);\n"
    "    let octaves = clamp(bitcast<i32>(p[0].y), 1, 8);\n"
    "    let lacunarity = bitcast<f32>(p[0].z);\n"
    "    let gain = bitcast<f32>(p[0].w);\n"
    "    let seed = box_texgraph_hash(p[1].x);\n"
    "    let scale = bitcast<f32>(p[1].y);\n"
    "    let branch = bitcast<f32>(p[1].z);\n"
    "    let plane = box_texgraph_stage.domain == 0u;\n"
    "    let world = box_texgraph_stage.domain == 2u;\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let slope = i[1].xy * scale;\n"
    "    let dir = vec2f(slope.y, -slope.x);\n"
    "    var h = vec3f(0.0);\n"
    "    var f = frequency;\n"
    "    var rel = 1.0;\n"
    "    var amp = 1.0;\n"
    "    var norm = 0.0;\n"
    "    for (var o = 0; o < 8; o ++) {\n"
    "        if (o >= octaves) {\n"
    "            break;\n"
    "        }\n"
    "        var q = i[0].xy * f;\n"
    "        var period = vec2i(0);\n"
    "        var shift = vec2i(0);\n"
    "        if (plane) {\n"
    "            let px = select(clamp(i32(floor(f + 0.5)), 1, 4096), 0, f <= 0.0);\n"
    "            let py = select(clamp(i32(floor(f32(px) * size.y / size.x + 0.5)), 1, 4096), 0,\n"
    "                px == 0);\n"
    "            period = vec2i(px, py);\n"
    "            q = i[0].xy * vec2f(f32(px), f32(py));\n"
    "        } else if (world) {\n"
    "            let w = box_texgraph_params[window + u32(o)];\n"
    "            shift = bitcast<vec2i>(w.xy);\n"
    "            q += bitcast<vec2f>(w.zw);\n"
    "        }\n"
    "        let s = box_texgraph_hash(seed ^ box_texgraph_hash(u32(o) + 0x51ed270bu));\n"
    "        let e = box_tex_erosion_octave(s, q, dir + vec2f(h.z, -h.y) * branch, period,\n"
    "            shift);\n"
    "        h += e * amp * vec3f(1.0, rel, rel);\n"
    "        norm += amp;\n"
    "        amp *= gain;\n"
    "        f *= lacunarity;\n"
    "        rel *= lacunarity;\n"
    "    }\n"
    "    let fade = clamp(2.0 * length(dir), 0.0, 1.0);\n"
    "    return vec4f(fade * h.x / norm, 0.0, 0.0, 0.0);\n"
    "}\n";

/** Write the window elements of a TexErosion node: per octave the lattice cell of the origin
 * of the window as two i32 and its fraction as two f32, as TexNoise does.
 *
 * @param node The TexErosion
 * @param origin World x and z of the origin of the window
 * @param out Receives BOX_TEX_EROSION_OCTAVES vec4
 */
static void box_texture_erosion_window(
    const void *node,
    const double origin[2],
    uint32_t *out)
{
    const TexErosion *erosion = node;
    int32_t octaves = erosion->octaves < 1 ? 1 : erosion->octaves > BOX_TEX_EROSION_OCTAVES
        ? BOX_TEX_EROSION_OCTAVES : erosion->octaves, o, k;
    float f = erosion->frequency;
    for (o = 0; o < octaves; o ++) {
        for (k = 0; k < 2; k ++) {
            double at = origin[k] * (double)f;
            double cell = floor(at);
            float frac = (float)(at - cell);
            if (!(frac < 1.0f)) {
                frac = 0;
                cell += 1;
            }

            if (!isfinite(cell) || f <= 0) {
                cell = 0;
                frac = 0;
            }

            out[o * 4 + k] = (uint32_t)(int64_t)fmod(cell, 4294967296.0);
            ecs_os_memcpy(&out[o * 4 + 2 + k], &frac, ECS_SIZEOF(float));
        }

        f *= erosion->lacunarity;
    }
}

void box_texture_erosion_register(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexErosion),
        .output = TexScalar,
        .inputs = {
            { "coords", offsetof(TexErosion, coords), TexVector, NULL, 0, true },
            { "slope", offsetof(TexErosion, slope), TexVector, "slope_value",
                offsetof(TexErosion, slope_value) }
        },
        .params = {
            { "frequency", offsetof(TexErosion, frequency), 4, BOX_TEXTURE_PARAM_F32 },
            { "octaves", offsetof(TexErosion, octaves), 4, BOX_TEXTURE_PARAM_I32 },
            { "lacunarity", offsetof(TexErosion, lacunarity), 4, BOX_TEXTURE_PARAM_F32 },
            { "gain", offsetof(TexErosion, gain), 4, BOX_TEXTURE_PARAM_F32 },
            { "seed", offsetof(TexErosion, seed), 4, BOX_TEXTURE_PARAM_I32 },
            { "scale", offsetof(TexErosion, scale), 4, BOX_TEXTURE_PARAM_F32 },
            { "branch", offsetof(TexErosion, branch), 4, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "box_tex_erosion",
        .source = box_texture_erosion_wgsl,
        .window = box_texture_erosion_window,
        .window_size = BOX_TEX_EROSION_OCTAVES
    });
}
