/**
 * @file texture/graph_flow.c
 * @brief The TexFlow node kind of texture graphs: line integral convolution along a field, and
 * the TexTransmittance node kind: the light of a point source through a density field.
 */

#include "texture.h"

/** WGSL of the TexFlow node: the gaussian weighted average of the input along the streamline
 * through the texel, traced both ways in at most BOX_TEX_FLOW_TAPS midpoint steps a side.
 *
 * The field is read again at every step and a side stops where it vanishes. On the plane a
 * position is in texels, wrapped. On the sphere it is a unit direction that a step turns along
 * the great circle of the field projected on the tangent plane. */
static const char *box_texture_flow_wgsl =
    "const BOX_TEX_FLOW_TAPS: i32 = 32;\n"
    "const BOX_TEX_FLOW_EPSILON: f32 = 1e-6;\n"
    "\n"
    "// Field at a position in texels of the plane, its constant without a node\n"
    "fn box_tex_flow_field_plane(t: texture_2d<f32>, has: bool, constant: vec4f, pos: vec2f)\n"
    "    -> vec2f {\n"
    "    if (has) {\n"
    "        return box_texgraph_bilinear(t, pos).xy;\n"
    "    }\n"
    "    return constant.xy;\n"
    "}\n"
    "\n"
    "// Unit tangent of the field at a direction of the sphere, 0 where the field vanishes\n"
    "fn box_tex_flow_field_sphere(t: texture_2d<f32>, has: bool, constant: vec4f, d: vec3f)\n"
    "    -> vec3f {\n"
    "    var f = constant.xyz;\n"
    "    if (has) {\n"
    "        f = box_texgraph_bilinear(t, box_texgraph_sphere_position(d)).xyz;\n"
    "    }\n"
    "    let tangent = f - d * dot(f, d);\n"
    "    let len = length(tangent);\n"
    "    if (len <= BOX_TEX_FLOW_EPSILON) {\n"
    "        return vec3f(0.0);\n"
    "    }\n"
    "    return tangent / len;\n"
    "}\n"
    "\n"
    "// Direction d turned by angle along the unit tangent of the sphere t\n"
    "fn box_tex_flow_turn(d: vec3f, t: vec3f, angle: f32) -> vec3f {\n"
    "    return normalize(d * cos(angle) + t * sin(angle));\n"
    "}\n"
    "\n"
    "// Weighted sum of the input along one side of the streak on the plane, xyzw the sum and the\n"
    "// weights in the w of the second\n"
    "fn box_tex_flow_side_plane(t0: texture_2d<f32>, t1: texture_2d<f32>, has_field: bool,\n"
    "    field: vec4f, start: vec2f, sign: f32, taps: i32, step: f32, sigma: f32)\n"
    "    -> array<vec4f, 2> {\n"
    "    var pos = start;\n"
    "    var sum = vec4f(0.0);\n"
    "    var total = 0.0;\n"
    "    for (var k = 1; k <= BOX_TEX_FLOW_TAPS; k ++) {\n"
    "        if (k > taps) {\n"
    "            break;\n"
    "        }\n"
    "        let a = box_tex_flow_field_plane(t1, has_field, field, pos);\n"
    "        if (length(a) <= BOX_TEX_FLOW_EPSILON) {\n"
    "            break;\n"
    "        }\n"
    "        let mid = pos + normalize(a) * sign * step * 0.5;\n"
    "        var b = box_tex_flow_field_plane(t1, has_field, field, mid);\n"
    "        if (length(b) <= BOX_TEX_FLOW_EPSILON) {\n"
    "            b = a;\n"
    "        }\n"
    "        pos += normalize(b) * sign * step;\n"
    "        let o = f32(k) * step;\n"
    "        let w = exp(-0.5 * o * o / (sigma * sigma));\n"
    "        sum += w * box_texgraph_bilinear(t0, pos);\n"
    "        total += w;\n"
    "    }\n"
    "    return array<vec4f, 2>(sum, vec4f(0.0, 0.0, 0.0, total));\n"
    "}\n"
    "\n"
    "// Weighted sum of the input along one side of the streak on the sphere, as on the plane\n"
    "fn box_tex_flow_side_sphere(t0: texture_2d<f32>, t1: texture_2d<f32>, has_field: bool,\n"
    "    field: vec4f, start: vec3f, sign: f32, taps: i32, angle: f32, sigma: f32)\n"
    "    -> array<vec4f, 2> {\n"
    "    var d = start;\n"
    "    var sum = vec4f(0.0);\n"
    "    var total = 0.0;\n"
    "    for (var k = 1; k <= BOX_TEX_FLOW_TAPS; k ++) {\n"
    "        if (k > taps) {\n"
    "            break;\n"
    "        }\n"
    "        let a = box_tex_flow_field_sphere(t1, has_field, field, d);\n"
    "        if (dot(a, a) == 0.0) {\n"
    "            break;\n"
    "        }\n"
    "        let mid = box_tex_flow_turn(d, a * sign, angle * 0.5);\n"
    "        let m = box_tex_flow_field_sphere(t1, has_field, field, mid);\n"
    "        var b = m - d * dot(m, d);\n"
    "        if (length(b) <= BOX_TEX_FLOW_EPSILON) {\n"
    "            b = a;\n"
    "        }\n"
    "        d = box_tex_flow_turn(d, normalize(b) * sign, angle);\n"
    "        let o = f32(k);\n"
    "        let w = exp(-0.5 * o * o / (sigma * sigma));\n"
    "        sum += w * box_texgraph_bilinear(t0, box_texgraph_sphere_position(d));\n"
    "        total += w;\n"
    "    }\n"
    "    return array<vec4f, 2>(sum, vec4f(0.0, 0.0, 0.0, total));\n"
    "}\n"
    "\n"
    "fn box_tex_flow(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    t0: texture_2d<f32>, t1: texture_2d<f32>, t2: texture_2d<f32>) -> vec4f {\n"
    "    if (any(textureDimensions(t0) != box_texgraph_stage.size)) {\n"
    "        return i[0];\n"
    "    }\n"
    "    let center = box_texgraph_load(t0, texel);\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let reach = max(bitcast<f32>(p[0].x), 0.0) * clamp(i[2].x, 0.0, 1.0);\n"
    "    let texels = reach * size.x / 512.0;\n"
    "    if (!(texels > 0.0)) {\n"
    "        return center;\n"
    "    }\n"
    "    let has_field = all(textureDimensions(t1) == box_texgraph_stage.size);\n"
    "    let taps = clamp(i32(ceil(texels)), 1, BOX_TEX_FLOW_TAPS);\n"
    "    let sigma = f32(taps) / 3.0;\n"
    "    var fwd: array<vec4f, 2>;\n"
    "    var bwd: array<vec4f, 2>;\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        let d = coords.xyz;\n"
    "        let f = box_tex_flow_field_sphere(t1, has_field, i[1], d);\n"
    "        if (dot(f, f) == 0.0) {\n"
    "            return center;\n"
    "        }\n"
    "        let angle = reach * 2.0 * BOX_TEXGRAPH_PI / 512.0 / f32(taps);\n"
    "        fwd = box_tex_flow_side_sphere(t0, t1, has_field, i[1], d, 1.0, taps, angle, sigma);\n"
    "        bwd = box_tex_flow_side_sphere(t0, t1, has_field, i[1], d, -1.0, taps, angle,\n"
    "            sigma);\n"
    "    } else {\n"
    "        let pos = vec2f(texel) + 0.5;\n"
    "        if (length(box_tex_flow_field_plane(t1, has_field, i[1], pos)) <=\n"
    "            BOX_TEX_FLOW_EPSILON) {\n"
    "            return center;\n"
    "        }\n"
    "        let step = texels / f32(taps);\n"
    "        let s = sigma * step;\n"
    "        fwd = box_tex_flow_side_plane(t0, t1, has_field, i[1], pos, 1.0, taps, step, s);\n"
    "        bwd = box_tex_flow_side_plane(t0, t1, has_field, i[1], pos, -1.0, taps, step, s);\n"
    "    }\n"
    "    return (center + fwd[0] + bwd[0]) / (1.0 + fwd[1].w + bwd[1].w);\n"
    "}\n";

/** Get the type of the value of a TexFlow node: the type of its input, not of its field.
 *
 * @param node The TexFlow
 * @param inputs Type of the node of each input, TexAny for none
 * @return The type
 */
static TextureValueType box_texture_flow_type(
    const void *node,
    const TextureValueType *inputs)
{
    (void)node;

    return box_texture_graph_follow(inputs, 1u);
}

/** WGSL of the TexTransmittance node: exp(-absorption * optical depth) along the path from the
 * texel toward the source, lengths in texels of a 512 wide image (2 pi / 512 rad on the sphere).
 *
 * The density is read between texels at the midpoints of steps equal parts of the path, or at
 * a hash of the texel within each part when the path is longer than steps texels.
 *
 * On the plane the path is the straight segment to the uv of the source, wrapped. On the
 * sphere it is the great circle arc to its direction, through the nearest pole from the
 * opposite direction, where every arc reaches it. */
static const char *box_texture_transmittance_wgsl =
    "const BOX_TEX_TRANSMITTANCE_STEPS: i32 = 256;\n"
    "const BOX_TEX_TRANSMITTANCE_EPSILON: f32 = 1e-6;\n"
    "\n"
    "// Position in texels of a direction of the sphere, longitude 0 at the poles where it has\n"
    "// none\n"
    "fn box_tex_transmittance_position(d: vec3f) -> vec2f {\n"
    "    if (d.x * d.x + d.z * d.z < 1e-12) {\n"
    "        let v = select(f32(box_texgraph_stage.size.y), 0.0, d.y > 0.0);\n"
    "        return vec2f(0.5 * f32(box_texgraph_stage.size.x), v);\n"
    "    }\n"
    "    return box_texgraph_sphere_position(d);\n"
    "}\n"
    "\n"
    "fn box_tex_transmittance(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    texel: vec2i, t0: texture_2d<f32>) -> vec4f {\n"
    "    let absorption = max(bitcast<f32>(p[0].w), 0.0);\n"
    "    if (!(absorption > 0.0)) {\n"
    "        return vec4f(1.0, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let has = all(textureDimensions(t0) == box_texgraph_stage.size);\n"
    "    if (!has && !(i[0].x != 0.0)) {\n"
    "        return vec4f(1.0, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let light = bitcast<vec3f>(p[0].xyz);\n"
    "    let range = bitcast<f32>(p[1].x);\n"
    "    let steps = clamp(bitcast<i32>(p[1].y), 1, BOX_TEX_TRANSMITTANCE_STEPS);\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let sphere = box_texgraph_stage.domain == 1u;\n"
    "\n"
    "    // Path in texels of the image and its direction: a texel offset on the plane, the unit\n"
    "    // tangent of the arc on the sphere\n"
    "    var d = vec3f(0.0);\n"
    "    var t = vec3f(0.0);\n"
    "    let start = vec2f(texel) + 0.5;\n"
    "    var dir = vec2f(0.0);\n"
    "    var texels = 0.0;\n"
    "    if (sphere) {\n"
    "        d = coords.xyz;\n"
    "        let len = length(light);\n"
    "        if (!(len > BOX_TEX_TRANSMITTANCE_EPSILON)) {\n"
    "            return vec4f(1.0, 0.0, 0.0, 0.0);\n"
    "        }\n"
    "        let l = light / len;\n"
    "        let c = clamp(dot(d, l), -1.0, 1.0);\n"
    "        t = l - d * c;\n"
    "        var tl = length(t);\n"
    "        if (tl <= BOX_TEX_TRANSMITTANCE_EPSILON) {\n"
    "            if (c > 0.0) {\n"
    "                return vec4f(1.0, 0.0, 0.0, 0.0);\n"
    "            }\n"
    "            t = vec3f(0.0, 1.0, 0.0) - d * d.y;\n"
    "            if (d.y < 0.0) {\n"
    "                t = -t;\n"
    "            }\n"
    "            tl = length(t);\n"
    "            if (tl <= BOX_TEX_TRANSMITTANCE_EPSILON) {\n"
    "                t = vec3f(1.0, 0.0, 0.0);\n"
    "                tl = 1.0;\n"
    "            }\n"
    "        }\n"
    "        t = t / tl;\n"
    "        texels = atan2(length(cross(d, l)), c) * size.x / (2.0 * BOX_TEXGRAPH_PI);\n"
    "    } else {\n"
    "        let delta = light.xy * size - start;\n"
    "        texels = length(delta);\n"
    "        if (texels > 0.0) {\n"
    "            dir = delta / texels;\n"
    "        }\n"
    "    }\n"
    "    let scale = size.x / 512.0;\n"
    "    if (range > 0.0) {\n"
    "        texels = min(texels, range * scale);\n"
    "    }\n"
    "    if (!(texels > 0.0)) {\n"
    "        return vec4f(1.0, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "\n"
    "    let part = texels / f32(steps);\n"
    "    var offset = 0.5;\n"
    "    if (texels > f32(steps)) {\n"
    "        offset = box_texgraph_random(box_texgraph_hash2(bitcast<vec2u>(texel)));\n"
    "    }\n"
    "    var sum = 0.0;\n"
    "    if (!has) {\n"
    "        sum = i[0].x * f32(steps);\n"
    "    } else {\n"
    "        let angle = 2.0 * BOX_TEXGRAPH_PI / size.x;\n"
    "        for (var k = 0; k < BOX_TEX_TRANSMITTANCE_STEPS; k ++) {\n"
    "            if (k >= steps) {\n"
    "                break;\n"
    "            }\n"
    "            let s = (f32(k) + offset) * part;\n"
    "            var pos = start + dir * s;\n"
    "            if (sphere) {\n"
    "                let a = s * angle;\n"
    "                pos = box_tex_transmittance_position(d * cos(a) + t * sin(a));\n"
    "            }\n"
    "            sum += box_texgraph_bilinear(t0, pos).x;\n"
    "        }\n"
    "    }\n"
    "    let depth = sum * part / scale;\n"
    "    return vec4f(exp(-absorption * depth), 0.0, 0.0, 0.0);\n"
    "}\n";

void box_texture_flow_register(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexFlow),
        .output = TexAny,
        .output_type = box_texture_flow_type,
        .inputs = {
            { "input", offsetof(TexFlow, input), TexAny, "input_value",
                offsetof(TexFlow, input_value) },
            { "field", offsetof(TexFlow, field), TexVector, "field_value",
                offsetof(TexFlow, field_value) },
            { "length", offsetof(TexFlow, length), TexScalar, "length_value",
                offsetof(TexFlow, length_value) }
        },
        .params = {{ "reach", offsetof(TexFlow, reach), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_flow",
        .source = box_texture_flow_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexTransmittance),
        .output = TexScalar,
        .inputs = {
            { "density", offsetof(TexTransmittance, density), TexScalar, "density_value",
                offsetof(TexTransmittance, density_value) }
        },
        .params = {
            { "light", offsetof(TexTransmittance, light), 12, BOX_TEXTURE_PARAM_F32 },
            { "absorption", offsetof(TexTransmittance, absorption), 4,
                BOX_TEXTURE_PARAM_F32 },
            { "range", offsetof(TexTransmittance, range), 4, BOX_TEXTURE_PARAM_F32 },
            { "steps", offsetof(TexTransmittance, steps), 4, BOX_TEXTURE_PARAM_I32 }
        },
        .function = "box_tex_transmittance",
        .source = box_texture_transmittance_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    });
}
