/**
 * @file texture/graph_math.c
 * @brief The math node kinds of texture graphs, one per operation: TexAdd, TexMultiply, ...
 */

#include "texture.h"

/* Each function reads the types word of its inputs, a bit j for a Scalar node at input j and
 * a bit 4 + j for a Vector node, broadcasts a Scalar input node to the four channels and checks
 * the word where it differs on Scalars. */

/** WGSL of the TexAdd node: a + b */
static const char *box_texture_add_wgsl =
    "fn box_tex_add(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return a + b;\n"
    "}\n";

/** WGSL of the TexSubtract node: a - b */
static const char *box_texture_subtract_wgsl =
    "fn box_tex_subtract(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return a - b;\n"
    "}\n";

/** WGSL of the TexMultiply node: a * b */
static const char *box_texture_multiply_wgsl =
    "fn box_tex_multiply(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return a * b;\n"
    "}\n";

/** WGSL of the TexDivide node: a / b with 0 where b is 0 */
static const char *box_texture_divide_wgsl =
    "fn box_tex_divide(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    let zero = b == vec4f(0.0);\n"
    "    return select(a / select(b, vec4f(1.0), zero), vec4f(0.0), zero);\n"
    "}\n";

/** WGSL of the TexMin node: min(a, b) */
static const char *box_texture_min_wgsl =
    "fn box_tex_min(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return min(a, b);\n"
    "}\n";

/** WGSL of the TexMax node: max(a, b) */
static const char *box_texture_max_wgsl =
    "fn box_tex_max(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return max(a, b);\n"
    "}\n";

/** WGSL of the TexPower node: a to the power b with its sign and zero rules */
static const char *box_texture_power_wgsl =
    "fn box_tex_power(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    let r = pow(max(abs(a), vec4f(1e-30)), b);\n"
    "    let whole = fract(b) == vec4f(0.0);\n"
    "    let odd = whole & (fract(b * 0.5) != vec4f(0.0));\n"
    "    let negative = a < vec4f(0.0);\n"
    "    var v = select(r, -r, negative & odd);\n"
    "    v = select(v, vec4f(0.0), (negative & !whole) | (a == vec4f(0.0)));\n"
    "    return select(v, vec4f(1.0), b == vec4f(0.0));\n"
    "}\n";

/** WGSL of the TexDot node: the dot product of the first three channels, a * b when the inputs
 * are Scalars */
static const char *box_texture_dot_wgsl =
    "fn box_tex_dot(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    if ((types & (3u << 4u)) == 0u && (types & 3u) != 0u) {\n"
    "        return vec4f(a.x * b.x, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "\n"
    "    return vec4f(dot(a.xyz, b.xyz), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexCross node: the cross product of the first three channels with 0 as the fourth */
static const char *box_texture_cross_wgsl =
    "fn box_tex_cross(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return vec4f(cross(a.xyz, b.xyz), 0.0);\n"
    "}\n";

/** WGSL of the TexMix node: mix(a, b, factor) */
static const char *box_texture_mix_wgsl =
    "fn box_tex_mix(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    let c = select(i[2], i[2].xxxx, (types & (1u << 2u)) != 0u);\n"
    "    return mix(a, b, c);\n"
    "}\n";

/** WGSL of the TexClamp node: its input limited to min..max */
static const char *box_texture_clamp_wgsl =
    "fn box_tex_clamp(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    let c = select(i[2], i[2].xxxx, (types & (1u << 2u)) != 0u);\n"
    "    return min(max(a, b), c);\n"
    "}\n";

/** WGSL of the TexStep node: 1 where its input is at least edge, else 0 */
static const char *box_texture_step_wgsl =
    "fn box_tex_step(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    return select(vec4f(0.0), vec4f(1.0), a >= b);\n"
    "}\n";

/** WGSL of the TexSmoothstep node: the smoothstep from edge0 to edge1, a step at edge0 where
 * they are equal */
static const char *box_texture_smoothstep_wgsl =
    "fn box_tex_smoothstep(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let b = select(i[1], i[1].xxxx, (types & (1u << 1u)) != 0u);\n"
    "    let c = select(i[2], i[2].xxxx, (types & (1u << 2u)) != 0u);\n"
    "    let d = c - b;\n"
    "    let same = d == vec4f(0.0);\n"
    "    let t = clamp((a - b) / select(d, vec4f(1.0), same), vec4f(0.0), vec4f(1.0));\n"
    "    let s = t * t * (3.0 - 2.0 * t);\n"
    "    return select(s, select(vec4f(0.0), vec4f(1.0), a >= b), same);\n"
    "}\n";

/** WGSL of the TexAbs node: abs(input) */
static const char *box_texture_abs_wgsl =
    "fn box_tex_abs(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return abs(a);\n"
    "}\n";

/** WGSL of the TexNegate node: -input */
static const char *box_texture_negate_wgsl =
    "fn box_tex_negate(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return -a;\n"
    "}\n";

/** WGSL of the TexFloor node: floor(input) */
static const char *box_texture_floor_wgsl =
    "fn box_tex_floor(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return floor(a);\n"
    "}\n";

/** WGSL of the TexFract node: input - floor(input) */
static const char *box_texture_fract_wgsl =
    "fn box_tex_fract(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return a - floor(a);\n"
    "}\n";

/** WGSL of the TexSin node: sin(input) */
static const char *box_texture_sin_wgsl =
    "fn box_tex_sin(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return sin(a);\n"
    "}\n";

/** WGSL of the TexCos node: cos(input) */
static const char *box_texture_cos_wgsl =
    "fn box_tex_cos(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    return cos(a);\n"
    "}\n";

/** WGSL of the TexLength node: the length of the first three channels, abs on a Scalar */
static const char *box_texture_length_wgsl =
    "fn box_tex_length(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    if ((types & (1u << 4u)) == 0u && (types & 1u) != 0u) {\n"
    "        return vec4f(abs(a.x), 0.0, 0.0, 0.0);\n"
    "    }\n"
    "\n"
    "    return vec4f(length(a.xyz), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of the TexNormalize node: the first three channels scaled to length 1, the sign on a
 * Scalar */
static const char *box_texture_normalize_wgsl =
    "fn box_tex_normalize(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[0].x;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    if ((types & (1u << 4u)) == 0u && (types & 1u) != 0u) {\n"
    "        return sign(a);\n"
    "    }\n"
    "\n"
    "    let l = length(a.xyz);\n"
    "    return vec4f(select(a.xyz / l, vec3f(0.0), l == 0.0), a.w);\n"
    "}\n";

/** WGSL of the TexRemap node: its input mapped from the from range to the to range, limited to
 * it with clamp */
static const char *box_texture_remap_wgsl =
    "fn box_tex_remap(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let types = p[1].y;\n"
    "    let a = select(i[0], i[0].xxxx, (types & (1u << 0u)) != 0u);\n"
    "    let range = bitcast<vec4f>(p[0]);\n"
    "    let d = range.y - range.x;\n"
    "    var t = select((a - range.x) / d, vec4f(0.0), d == 0.0);\n"
    "    if (p[1].x != 0u) {\n"
    "        t = clamp(t, vec4f(0.0), vec4f(1.0));\n"
    "    }\n"
    "\n"
    "    return range.z + t * (range.w - range.z);\n"
    "}\n";

/** Register the math node kinds of the inputs a and b: TexAdd to TexCross and TexMix.
 *
 * @param world The world
 */
static void box_texture_math_register_binary(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexAdd),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexAdd, a), TexAny, "a_value", offsetof(TexAdd, a_value) },
            { "b", offsetof(TexAdd, b), TexAny, "b_value", offsetof(TexAdd, b_value) }
        },
        .function = "box_tex_add",
        .source = box_texture_add_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexSubtract),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexSubtract, a), TexAny, "a_value", offsetof(TexSubtract, a_value) },
            { "b", offsetof(TexSubtract, b), TexAny, "b_value", offsetof(TexSubtract, b_value) }
        },
        .function = "box_tex_subtract",
        .source = box_texture_subtract_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMultiply),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexMultiply, a), TexAny, "a_value", offsetof(TexMultiply, a_value) },
            { "b", offsetof(TexMultiply, b), TexAny, "b_value", offsetof(TexMultiply, b_value) }
        },
        .function = "box_tex_multiply",
        .source = box_texture_multiply_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexDivide),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexDivide, a), TexAny, "a_value", offsetof(TexDivide, a_value) },
            { "b", offsetof(TexDivide, b), TexAny, "b_value", offsetof(TexDivide, b_value) }
        },
        .function = "box_tex_divide",
        .source = box_texture_divide_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMin),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexMin, a), TexAny, "a_value", offsetof(TexMin, a_value) },
            { "b", offsetof(TexMin, b), TexAny, "b_value", offsetof(TexMin, b_value) }
        },
        .function = "box_tex_min",
        .source = box_texture_min_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMax),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexMax, a), TexAny, "a_value", offsetof(TexMax, a_value) },
            { "b", offsetof(TexMax, b), TexAny, "b_value", offsetof(TexMax, b_value) }
        },
        .function = "box_tex_max",
        .source = box_texture_max_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexPower),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexPower, a), TexAny, "a_value", offsetof(TexPower, a_value) },
            { "b", offsetof(TexPower, b), TexAny, "b_value", offsetof(TexPower, b_value) }
        },
        .function = "box_tex_power",
        .source = box_texture_power_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexDot),
        .output = TexScalar,
        .inputs = {
            { "a", offsetof(TexDot, a), TexAny, "a_value", offsetof(TexDot, a_value) },
            { "b", offsetof(TexDot, b), TexAny, "b_value", offsetof(TexDot, b_value) }
        },
        .function = "box_tex_dot",
        .source = box_texture_dot_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCross),
        .output = TexVector,
        .inputs = {
            { "a", offsetof(TexCross, a), TexAny, "a_value", offsetof(TexCross, a_value) },
            { "b", offsetof(TexCross, b), TexAny, "b_value", offsetof(TexCross, b_value) }
        },
        .function = "box_tex_cross",
        .source = box_texture_cross_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMix),
        .output = TexAny,
        .inputs = {
            { "a", offsetof(TexMix, a), TexAny, "a_value", offsetof(TexMix, a_value) },
            { "b", offsetof(TexMix, b), TexAny, "b_value", offsetof(TexMix, b_value) },
            { "factor", offsetof(TexMix, factor), TexAny, "factor_value",
                offsetof(TexMix, factor_value) }
        },
        .function = "box_tex_mix",
        .source = box_texture_mix_wgsl
    });
}

/** Register the math node kinds of an input: TexClamp, TexStep, TexSmoothstep, the
 * functions of one value such as TexSin and TexRemap.
 *
 * @param world The world
 */
static void box_texture_math_register_input(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexClamp),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexClamp, input), TexAny, "input_value",
                offsetof(TexClamp, input_value) },
            { "min", offsetof(TexClamp, min), TexAny, "min_value", offsetof(TexClamp, min_value) },
            { "max", offsetof(TexClamp, max), TexAny, "max_value", offsetof(TexClamp, max_value) }
        },
        .function = "box_tex_clamp",
        .source = box_texture_clamp_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexStep),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexStep, input), TexAny, "input_value",
                offsetof(TexStep, input_value) },
            { "edge", offsetof(TexStep, edge), TexAny, "edge_value", offsetof(TexStep, edge_value) }
        },
        .function = "box_tex_step",
        .source = box_texture_step_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexSmoothstep),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexSmoothstep, input), TexAny, "input_value",
                offsetof(TexSmoothstep, input_value) },
            { "edge0", offsetof(TexSmoothstep, edge0), TexAny, "edge0_value",
                offsetof(TexSmoothstep, edge0_value) },
            { "edge1", offsetof(TexSmoothstep, edge1), TexAny, "edge1_value",
                offsetof(TexSmoothstep, edge1_value) }
        },
        .function = "box_tex_smoothstep",
        .source = box_texture_smoothstep_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexAbs),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexAbs, input), TexAny, "input_value",
                offsetof(TexAbs, input_value) }
        },
        .function = "box_tex_abs",
        .source = box_texture_abs_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexNegate),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexNegate, input), TexAny, "input_value",
                offsetof(TexNegate, input_value) }
        },
        .function = "box_tex_negate",
        .source = box_texture_negate_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexFloor),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexFloor, input), TexAny, "input_value",
                offsetof(TexFloor, input_value) }
        },
        .function = "box_tex_floor",
        .source = box_texture_floor_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexFract),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexFract, input), TexAny, "input_value",
                offsetof(TexFract, input_value) }
        },
        .function = "box_tex_fract",
        .source = box_texture_fract_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexSin),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexSin, input), TexAny, "input_value",
                offsetof(TexSin, input_value) }
        },
        .function = "box_tex_sin",
        .source = box_texture_sin_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCos),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexCos, input), TexAny, "input_value",
                offsetof(TexCos, input_value) }
        },
        .function = "box_tex_cos",
        .source = box_texture_cos_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexLength),
        .output = TexScalar,
        .inputs = {
            { "input", offsetof(TexLength, input), TexAny, "input_value",
                offsetof(TexLength, input_value) }
        },
        .function = "box_tex_length",
        .source = box_texture_length_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexNormalize),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexNormalize, input), TexAny, "input_value",
                offsetof(TexNormalize, input_value) }
        },
        .function = "box_tex_normalize",
        .source = box_texture_normalize_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexRemap),
        .output = TexAny,
        .inputs = {
            { "input", offsetof(TexRemap, input), TexAny, "input_value",
                offsetof(TexRemap, input_value) }
        },
        .params = {
            { "from_min", offsetof(TexRemap, from_min), 4, BOX_TEXTURE_PARAM_F32 },
            { "from_max", offsetof(TexRemap, from_max), 4, BOX_TEXTURE_PARAM_F32 },
            { "to_min", offsetof(TexRemap, to_min), 4, BOX_TEXTURE_PARAM_F32 },
            { "to_max", offsetof(TexRemap, to_max), 4, BOX_TEXTURE_PARAM_F32 },
            { "clamp", offsetof(TexRemap, clamp), ECS_SIZEOF(bool), BOX_TEXTURE_PARAM_BOOL }
        },
        .function = "box_tex_remap",
        .source = box_texture_remap_wgsl
    });
}

void box_texture_math_register(
    ecs_world_t *world)
{
    box_texture_math_register_binary(world);
    box_texture_math_register_input(world);
}
