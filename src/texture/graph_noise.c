/**
 * @file texture/graph_noise.c
 * @brief The TexNoise node kind of texture graphs: fbm, ridged and billow gradient noise.
 */

#include "texture.h"

/** WGSL of the TexNoise node: fbm, ridged or billow gradient noise per channel.
 *
 * The lattice is four dimensional, with 32 gradients of three components +-1 and one 0. A point
 * with w 0 needs only the eight corners at w 0, so without evolve the noise is three dimensional.
 *
 * At w 0 that noise is the evolved noise, so it continues into it. On the plane x and y wrap
 * with the whole number of cells across of each octave. */
static const char *box_texture_noise_wgsl =
    "// Gradient of a corner of the noise lattice dotted with the offset d from it\n"
    "fn box_tex_noise_grad(h: u32, d: vec4f) -> f32 {\n"
    "    let s = vec3f(select(1.0, -1.0, (h & 1u) != 0u), select(1.0, -1.0, (h & 2u) != 0u),\n"
    "        select(1.0, -1.0, (h & 4u) != 0u));\n"
    "    switch (h >> 3u) & 3u {\n"
    "        case 0u: { return dot(s, d.yzw); }\n"
    "        case 1u: { return dot(s, d.xzw); }\n"
    "        case 2u: { return dot(s, d.xyw); }\n"
    "        default: { return dot(s, d.xyz); }\n"
    "    }\n"
    "}\n"
    "\n"
    "// Lattice index wrapped into 0..period - 1, as it is without a period\n"
    "fn box_tex_noise_wrap(i: i32, period: i32) -> u32 {\n"
    "    if (period <= 0) {\n"
    "        return bitcast<u32>(i);\n"
    "    }\n"
    "    return bitcast<u32>(((i % period) + period) % period);\n"
    "}\n"
    "\n"
    "// Hash of a corner of the lattice, x and y wrapped\n"
    "fn box_tex_noise_hash(seed: u32, c: vec4i, period: vec2i) -> u32 {\n"
    "    let x = box_tex_noise_wrap(c.x, period.x);\n"
    "    let y = box_tex_noise_wrap(c.y, period.y);\n"
    "    return box_texgraph_hash(seed ^ box_texgraph_hash(x ^ box_texgraph_hash(y ^\n"
    "        box_texgraph_hash(bitcast<u32>(c.z) ^ box_texgraph_hash(bitcast<u32>(c.w))))));\n"
    "}\n"
    "\n"
    "// Noise of the eight corners of the cell i at the layer iw, d.w the offset from the layer\n"
    "fn box_tex_noise_layer(seed: u32, i: vec3i, f: vec3f, u: vec3f, iw: i32, dw: f32,\n"
    "    period: vec2i) -> f32 {\n"
    "    var v: array<f32, 8>;\n"
    "    for (var c = 0; c < 8; c ++) {\n"
    "        let o = vec3i(c & 1, (c >> 1) & 1, (c >> 2) & 1);\n"
    "        let h = box_tex_noise_hash(seed, vec4i(i + o, iw), period);\n"
    "        v[c] = box_tex_noise_grad(h, vec4f(f - vec3f(o), dw));\n"
    "    }\n"
    "    let y0 = mix(mix(v[0], v[1], u.x), mix(v[2], v[3], u.x), u.y);\n"
    "    let y1 = mix(mix(v[4], v[5], u.x), mix(v[6], v[7], u.x), u.y);\n"
    "    return mix(y0, y1, u.z);\n"
    "}\n"
    "\n"
    "// Gradient noise at a point, about -1..1, three dimensional where w is 0\n"
    "fn box_tex_noise_gradient(seed: u32, p: vec4f, period: vec2i) -> f32 {\n"
    "    let b = floor(p.xyz);\n"
    "    let i = vec3i(b);\n"
    "    let f = p.xyz - b;\n"
    "    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
    "    if (p.w == 0.0) {\n"
    "        return box_tex_noise_layer(seed, i, f, u, 0, 0.0, period);\n"
    "    }\n"
    "    let bw = floor(p.w);\n"
    "    let iw = i32(bw);\n"
    "    let fw = p.w - bw;\n"
    "    let uw = fw * fw * fw * (fw * (fw * 6.0 - 15.0) + 10.0);\n"
    "    return mix(box_tex_noise_layer(seed, i, f, u, iw, fw, period),\n"
    "        box_tex_noise_layer(seed, i, f, u, iw + 1, fw - 1.0, period), uw);\n"
    "}\n"
    "\n"
    "// Fractal of the octaves of one channel, 0..1\n"
    "fn box_tex_noise_fractal(p: array<vec4u, 4>, seed: u32, coords: vec4f, evolve: f32)\n"
    "    -> f32 {\n"
    "    let frequency = bitcast<f32>(p[0].x);\n"
    "    let octaves = clamp(bitcast<i32>(p[0].y), 1, 16);\n"
    "    let lacunarity = bitcast<f32>(p[0].z);\n"
    "    let gain = bitcast<f32>(p[0].w);\n"
    "    let fractal = bitcast<i32>(p[1].y);\n"
    "    let offset = bitcast<f32>(p[1].z);\n"
    "    let plane = box_texgraph_stage.domain == 0u;\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    var f = frequency;\n"
    "    var time = evolve;\n"
    "    var amp = 1.0;\n"
    "    var norm = 0.0;\n"
    "    var sum = 0.0;\n"
    "    var weight = 1.0;\n"
    "    for (var o = 0; o < 16; o ++) {\n"
    "        if (o >= octaves) {\n"
    "            break;\n"
    "        }\n"
    "        var q = coords.xyz * f;\n"
    "        var period = vec2i(0);\n"
    "        if (plane) {\n"
    "            let px = select(clamp(i32(floor(f + 0.5)), 1, 4096), 0, f <= 0.0);\n"
    "            let py = select(clamp(i32(floor(f32(px) * size.y / size.x + 0.5)), 1, 4096), 0,\n"
    "                px == 0);\n"
    "            period = vec2i(px, py);\n"
    "            q = coords.xyz * vec3f(f32(px), f32(py), f32(px));\n"
    "        }\n"
    "        let s = box_texgraph_hash(seed ^ box_texgraph_hash(u32(o) + 0x68e31da4u));\n"
    "        let n = clamp(box_tex_noise_gradient(s, vec4f(q, time), period), -1.0, 1.0);\n"
    "        switch fractal {\n"
    "            case 1: {\n"
    "                var signal = offset - abs(n);\n"
    "                signal = signal * signal * weight;\n"
    "                weight = clamp(signal, 0.0, 1.0);\n"
    "                sum += amp * signal;\n"
    "            }\n"
    "            case 2: {\n"
    "                sum += amp * abs(n);\n"
    "            }\n"
    "            default: {\n"
    "                sum += amp * n;\n"
    "            }\n"
    "        }\n"
    "        norm += amp;\n"
    "        amp *= gain;\n"
    "        f *= lacunarity;\n"
    "        time *= lacunarity;\n"
    "    }\n"
    "    let v = sum / norm;\n"
    "    return clamp(select(v, 0.5 + 0.5 * v, fractal != 1 && fractal != 2), 0.0, 1.0);\n"
    "}\n"
    "\n"
    "fn box_tex_noise(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let channels = bitcast<i32>(p[1].w);\n"
    "    if (channels <= 1) {\n"
    "        let n = box_tex_noise_fractal(p, box_texgraph_hash(p[1].x), i[0], i[1].x);\n"
    "        return vec4f(n, n, n, 1.0);\n"
    "    }\n"
    "    var r = vec4f(0.0, 0.0, 0.0, 1.0);\n"
    "    for (var c = 0; c < 4; c ++) {\n"
    "        if (c >= channels) {\n"
    "            break;\n"
    "        }\n"
    "        r[c] = box_tex_noise_fractal(p, box_texgraph_hash(p[1].x + u32(c) * 0x9e3779b9u),\n"
    "            i[0], i[1].x);\n"
    "    }\n"
    "    return r;\n"
    "}\n";

/** Get the type of the value of a TexNoise node: a Scalar with one channel, else a Vector.
 *
 * @param node The TexNoise
 * @param inputs Type of the node of each input, TexAny for none
 * @return The type
 */
static TextureValueType box_texture_noise_type(
    const void *node,
    const TextureValueType *inputs)
{
    const TexNoise *noise = node;
    (void)inputs;

    return noise->channels > 1 ? TexVector : TexScalar;
}

void box_texture_noise_register(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexNoise),
        .output = TexAny,
        .output_type = box_texture_noise_type,
        .inputs = {
            { "coords", offsetof(TexNoise, coords), TexVector, NULL, 0, true },
            { "evolve", offsetof(TexNoise, evolve), TexScalar }
        },
        .params = {
            { "frequency", offsetof(TexNoise, frequency), 4, BOX_TEXTURE_PARAM_F32 },
            { "octaves", offsetof(TexNoise, octaves), 4, BOX_TEXTURE_PARAM_I32 },
            { "lacunarity", offsetof(TexNoise, lacunarity), 4, BOX_TEXTURE_PARAM_F32 },
            { "gain", offsetof(TexNoise, gain), 4, BOX_TEXTURE_PARAM_F32 },
            { "seed", offsetof(TexNoise, seed), 4, BOX_TEXTURE_PARAM_I32 },
            { "fractal", offsetof(TexNoise, fractal), 4, BOX_TEXTURE_PARAM_I32 },
            { "offset", offsetof(TexNoise, offset), 4, BOX_TEXTURE_PARAM_F32 },
            { "channels", offsetof(TexNoise, channels), 4, BOX_TEXTURE_PARAM_I32 }
        },
        .function = "box_tex_noise",
        .source = box_texture_noise_wgsl
    });
}
