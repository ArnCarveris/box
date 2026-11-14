/**
 * @file TextureNodesMath.c
 * @brief Tests for the arithmetic and channel nodes of TextureGraph on the GPU backend: every
 * math node kind on Vector and Scalar inputs and broadcast between them against a reference in
 * both domains, their edge cases, and TexSplit and TexCombine round trips.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** A swizzle of the coordinates with TexSplit, TexCombine and a remap */
#define GRAPH_CHANNELS "test/assets/textures/graph/channels.flecs"

/** Frames a test waits at most for an outcome */
#define TM_FRAMES (40)

/** Width of the images of the tests */
#define TM_W (16)

/** Height of the images of the tests */
#define TM_H (8)

/** Pi */
#define TM_PI (3.14159265358979f)

/** Largest expected value a texel is checked against, beyond which half floats lose it */
#define TM_LARGEST (1e4f)

/** Operation of a math node of the tests, one per kind with TmRemapClamped for a TexRemap
 * with clamp */
typedef enum tm_op_t {
    TmAdd = 0,          /**< TexAdd */
    TmSubtract,         /**< TexSubtract */
    TmMultiply,         /**< TexMultiply */
    TmMin,              /**< TexMin */
    TmMax,              /**< TexMax */
    TmMix,              /**< TexMix */
    TmClamp,            /**< TexClamp */
    TmDivide,           /**< TexDivide */
    TmPower,            /**< TexPower */
    TmAbs,              /**< TexAbs */
    TmNegate,           /**< TexNegate */
    TmFloor,            /**< TexFloor */
    TmFract,            /**< TexFract */
    TmSin,              /**< TexSin */
    TmCos,              /**< TexCos */
    TmStep,             /**< TexStep */
    TmSmoothstep,       /**< TexSmoothstep */
    TmRemap,            /**< TexRemap */
    TmRemapClamped,     /**< TexRemap with clamp */
    TmDot,              /**< TexDot */
    TmLength,           /**< TexLength */
    TmNormalize,        /**< TexNormalize */
    TmCross,            /**< TexCross */
    TmOps               /**< Number of operations */
} tm_op_t;

/** Number of operations of the sweeps */
#define TM_OPS (TmOps)

/** A vec4 initializer from an array of four floats */
#define TM_V(v) {(v)[0], (v)[1], (v)[2], (v)[3]}

/** How the inputs of the math node of a sweep are connected */
typedef enum tm_mode_t {
    TmVector = 0,       /**< a, b and c are Vector nodes */
    TmScalar = 1,       /**< a, b and c are Scalar nodes */
    TmMixed = 2,        /**< a is a Scalar node, b and c are Vector nodes */
    TmScalarConst = 3   /**< a is a Scalar node, b and c are constants */
} tm_mode_t;

/** Factors of the coordinates of the three input vectors of a sweep */
static const float tm_scale[3][4] = {
    {4.3f, -3.1f, 2.7f, 0.0f},
    {-2.9f, 3.7f, 1.9f, 0.0f},
    {2.3f, 1.7f, -3.3f, 0.0f}
};

/** Offsets of the three input vectors of a sweep: the fourth channels are constants, b.w 0 */
static const float tm_offset[3][4] = {
    {-2.05f, 1.3f, -1.15f, -1.5f},
    {1.1f, -1.6f, 0.35f, 0.0f},
    {0.4f, 0.9f, 1.2f, 2.0f}
};

/** Constants of b and c of TmScalarConst */
static const float tm_const[3][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.7f, -0.3f, 1.4f, 0.2f},
    {1.6f, 2.5f, -0.5f, 3.0f}
};

/** Remap range of the sweeps: from_min, from_max, to_min, to_max */
static const float tm_range[4] = {-1.0f, 2.0f, 3.0f, -1.0f};

/** Ticket of the next readback */
static uint32_t tm_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tm_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});

    return world;
}

/** Run frames and wait for each to render.
 *
 * @param world The world
 * @param count Frames to run
 */
static void tm_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready, and check that it is.
 *
 * @param world The world
 * @param tex The texture entity
 */
static void tm_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TM_FRAMES; i ++) {
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state != TextureLoading && status->revision >= 1) {
            break;
        }

        if (status && status->state == TextureFailed) {
            break;
        }

        tm_frames(world, 1);
    }

    test_assert(status != NULL);
    if (status->state != TextureReady) {
        printf("graph failed: %s\n", status->error ? status->error : "(no error)");
    }

    test_int(status->state, TextureReady);
}

/** Read back the image of a texture graph or the value of one of its nodes. The value of the
 * output node is the image.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The node entity, 0 for the image
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tm_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tm_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    const TextureGraph *graph = ecs_get(world, tex, TextureGraph);
    if (graph && graph->output == node) {
        node = 0;
    }

    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TM_FRAMES && !result; i ++) {
        tm_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_uint(r->texture, tex);
                test_uint(r->node, node);
                test_int(r->width, width);
                test_int(r->height, height);
                result = r->texels;
            } else {
                ecs_os_free(r->texels);
            }
        }
    }

    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
    test_assert(result != NULL);

    return result;
}

/** Check a channel against its expected value within a tolerance relative to the value: the
 * values of Vector nodes read back at half precision.
 *
 * @param what Description of the check, printed when it fails
 * @param x Column
 * @param y Row
 * @param c Channel
 * @param value The value
 * @param expect The expected value
 */
static void tm_expect(
    const char *what,
    int32_t x,
    int32_t y,
    int32_t c,
    float value,
    float expect)
{
    float tolerance = 2e-3f + 2e-3f * fabsf(expect);
    if (!(fabsf(value - expect) <= tolerance)) {
        printf("%s: texel %d, %d channel %d: %f, expected %f\n", what, x, y, c, value,
            expect);
    }

    test_assert(fabsf(value - expect) <= tolerance);
}

/** Get the coordinates of the center of a texel in a domain.
 *
 * @param domain The domain
 * @param x Column
 * @param y Row
 * @param out Receives the coordinates
 */
static void tm_coords(
    TextureGraphDomain domain,
    int32_t x,
    int32_t y,
    float out[4])
{
    float u = ((float)x + 0.5f) / (float)TM_W;
    float v = ((float)y + 0.5f) / (float)TM_H;
    if (domain == DomainSphere) {
        float phi = (u - 0.5f) * 2.0f * TM_PI, theta = v * TM_PI;
        out[0] = sinf(theta) * sinf(phi);
        out[1] = cosf(theta);
        out[2] = -sinf(theta) * cosf(phi);
    } else {
        out[0] = u;
        out[1] = v;
        out[2] = 0;
    }

    out[3] = 1;
}

/** Get input vector k of a sweep at a texel: the coordinates times tm_scale plus tm_offset.
 *
 * @param domain The domain
 * @param x Column
 * @param y Row
 * @param k The input
 * @param out Receives the vector
 */
static void tm_input(
    TextureGraphDomain domain,
    int32_t x,
    int32_t y,
    int32_t k,
    float out[4])
{
    float coords[4];
    int32_t c;
    tm_coords(domain, x, y, coords);
    for (c = 0; c < 4; c ++) {
        out[c] = coords[c] * tm_scale[k][c] + tm_offset[k][c];
    }
}

/** Create the node of input vector k of a sweep.
 *
 * @param world The world
 * @param uv The coordinates node
 * @param k The input
 * @return The node
 */
static ecs_entity_t tm_vector(
    ecs_world_t *world,
    ecs_entity_t uv,
    int32_t k)
{
    ecs_entity_t scaled = ecs_new(world);
    ecs_entity_t result = ecs_new(world);
    const float *s = tm_scale[k], *o = tm_offset[k];
    ecs_set(world, scaled, TexMultiply, {.a = uv, .b_value = {s[0], s[1], s[2], s[3]}});
    ecs_set(world, result, TexAdd, {.a = scaled, .b_value = {o[0], o[1], o[2], o[3]}});

    return result;
}

/** Create a TexSplit node.
 *
 * @param world The world
 * @param input The Vector node
 * @param channel The channel
 * @return The node
 */
static ecs_entity_t tm_split(
    ecs_world_t *world,
    ecs_entity_t input,
    TextureChannel channel)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexSplit, {.input = input, .channel = channel});

    return result;
}

/** Set the math node of an operation on a node entity. Inputs a, b and c are the inputs of the
 * kind in order, such as input, edge0 and edge1 of TexSmoothstep.
 *
 * @param world The world
 * @param node The node entity
 * @param op The operation
 * @param in The input nodes a, b and c, 0 for their value
 * @param b Value of b without a node
 * @param c Value of c without a node
 * @param range Remap range: from_min, from_max, to_min, to_max
 */
static void tm_node(
    ecs_world_t *world,
    ecs_entity_t node,
    tm_op_t op,
    const ecs_entity_t in[3],
    const float b[4],
    const float c[4],
    const float range[4])
{
    switch (op) {
    case TmAdd:
        ecs_set(world, node, TexAdd, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmSubtract:
        ecs_set(world, node, TexSubtract, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmMultiply:
        ecs_set(world, node, TexMultiply, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmMin:
        ecs_set(world, node, TexMin, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmMax:
        ecs_set(world, node, TexMax, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmMix:
        ecs_set(world, node, TexMix, {.a = in[0], .b = in[1], .factor = in[2],
            .b_value = TM_V(b), .factor_value = TM_V(c)});
        break;
    case TmClamp:
        ecs_set(world, node, TexClamp, {.input = in[0], .min = in[1], .max = in[2],
            .min_value = TM_V(b), .max_value = TM_V(c)});
        break;
    case TmDivide:
        ecs_set(world, node, TexDivide, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmPower:
        ecs_set(world, node, TexPower, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmAbs:
        ecs_set(world, node, TexAbs, {.input = in[0]});
        break;
    case TmNegate:
        ecs_set(world, node, TexNegate, {.input = in[0]});
        break;
    case TmFloor:
        ecs_set(world, node, TexFloor, {.input = in[0]});
        break;
    case TmFract:
        ecs_set(world, node, TexFract, {.input = in[0]});
        break;
    case TmSin:
        ecs_set(world, node, TexSin, {.input = in[0]});
        break;
    case TmCos:
        ecs_set(world, node, TexCos, {.input = in[0]});
        break;
    case TmStep:
        ecs_set(world, node, TexStep, {.input = in[0], .edge = in[1], .edge_value = TM_V(b)});
        break;
    case TmSmoothstep:
        ecs_set(world, node, TexSmoothstep, {.input = in[0], .edge0 = in[1], .edge1 = in[2],
            .edge0_value = TM_V(b), .edge1_value = TM_V(c)});
        break;
    case TmRemap:
    case TmRemapClamped:
        ecs_set(world, node, TexRemap, {.input = in[0], .from_min = range[0],
            .from_max = range[1], .to_min = range[2], .to_max = range[3],
            .clamp = op == TmRemapClamped});
        break;
    case TmDot:
        ecs_set(world, node, TexDot, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    case TmLength:
        ecs_set(world, node, TexLength, {.input = in[0]});
        break;
    case TmNormalize:
        ecs_set(world, node, TexNormalize, {.input = in[0]});
        break;
    case TmCross:
        ecs_set(world, node, TexCross, {.a = in[0], .b = in[1], .b_value = TM_V(b)});
        break;
    default:
        test_assert(false);
        break;
    }
}

/** Get the inputs an operation reads.
 *
 * @param op The operation
 * @return Bit j set when it reads input j
 */
static uint32_t tm_used(
    tm_op_t op)
{
    switch (op) {
    case TmAbs:
    case TmNegate:
    case TmFloor:
    case TmFract:
    case TmSin:
    case TmCos:
    case TmRemap:
    case TmRemapClamped:
    case TmLength:
    case TmNormalize:
        return 1;
    case TmMix:
    case TmClamp:
    case TmSmoothstep:
        return 7;
    default:
        return 3;
    }
}

/** Check whether an operation computes on Scalars in a mode: every input node it reads is a
 * Scalar.
 *
 * @param op The operation
 * @param mode The mode
 * @return Whether it does
 */
static bool tm_scalar_mode(
    tm_op_t op,
    tm_mode_t mode)
{
    switch (mode) {
    case TmVector:
        return false;
    case TmMixed:
        return tm_used(op) == 1;
    default:
        return true;
    }
}

/** Check whether the math node of an operation outputs a Scalar.
 *
 * @param op The operation
 * @param scalar Whether it computes on Scalars
 * @return Whether it does
 */
static bool tm_scalar_output(
    tm_op_t op,
    bool scalar)
{
    return op == TmDot || op == TmLength || (op != TmCross && scalar);
}

/** Power as TexPower defines it.
 *
 * @param a The base
 * @param b The exponent
 * @return The power
 */
static float tm_pow(
    float a,
    float b)
{
    float result;
    bool whole = b == floorf(b);
    if (b == 0) {
        result = 1;
    } else if (a == 0 || (a < 0 && !whole)) {
        result = 0;
    } else {
        result = powf(fabsf(a), b);
        if (a < 0 && fmodf(b, 2.0f) != 0) {
            result = -result;
        }
    }

    return result;
}

/** Smoothstep as TexSmoothstep defines it.
 *
 * @param e0 The edge of 0
 * @param e1 The edge of 1
 * @param x The value
 * @return The step
 */
static float tm_smoothstep(
    float e0,
    float e1,
    float x)
{
    float t;
    if (e1 == e0) {
        return x >= e0 ? 1.0f : 0.0f;
    }

    t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : t > 1 ? 1 : t;

    return t * t * (3.0f - 2.0f * t);
}

/** Remap as TexRemap defines it.
 *
 * @param a The value
 * @param range from_min, from_max, to_min, to_max
 * @param clamped Whether the result is limited to the to range
 * @return The remapped value
 */
static float tm_remap(
    float a,
    const float range[4],
    bool clamped)
{
    float d = range[1] - range[0];
    float t = d == 0 ? 0 : (a - range[0]) / d;
    if (clamped) {
        t = t < 0 ? 0 : t > 1 ? 1 : t;
    }

    return range[2] + t * (range[3] - range[2]);
}

/** Compute an operation on the CPU, the reference of the tests.
 *
 * @param op The operation
 * @param a Input a, broadcast
 * @param b Input b, broadcast
 * @param c Input c, broadcast
 * @param range Remap range
 * @param scalar Whether it computes on Scalars
 * @param out Receives the value, a Scalar in out[0]
 */
static void tm_ref(
    tm_op_t op,
    const float a[4],
    const float b[4],
    const float c[4],
    const float range[4],
    bool scalar,
    float out[4])
{
    int32_t i;
    float l;
    switch (op) {
    case TmDot:
        out[0] = scalar ? a[0] * b[0] : a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        out[1] = out[2] = out[3] = 0;
        return;
    case TmLength:
        out[0] = scalar ? fabsf(a[0]) : sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        out[1] = out[2] = out[3] = 0;
        return;
    case TmNormalize:
        if (scalar) {
            for (i = 0; i < 4; i ++) {
                out[i] = a[i] > 0 ? 1.0f : a[i] < 0 ? -1.0f : 0.0f;
            }
        } else {
            l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
            for (i = 0; i < 3; i ++) {
                out[i] = l == 0 ? 0 : a[i] / l;
            }

            out[3] = a[3];
        }
        return;
    case TmCross:
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
        out[3] = 0;
        return;
    default:
        break;
    }

    for (i = 0; i < 4; i ++) {
        float x = a[i], y = b[i], z = c[i];
        switch (op) {
        case TmSubtract: out[i] = x - y; break;
        case TmMultiply: out[i] = x * y; break;
        case TmMin: out[i] = fminf(x, y); break;
        case TmMax: out[i] = fmaxf(x, y); break;
        case TmMix: out[i] = x + (y - x) * z; break;
        case TmClamp: out[i] = fminf(fmaxf(x, y), z); break;
        case TmDivide: out[i] = y == 0 ? 0 : x / y; break;
        case TmPower: out[i] = tm_pow(x, y); break;
        case TmAbs: out[i] = fabsf(x); break;
        case TmNegate: out[i] = -x; break;
        case TmFloor: out[i] = floorf(x); break;
        case TmFract: out[i] = x - floorf(x); break;
        case TmSin: out[i] = sinf(x); break;
        case TmCos: out[i] = cosf(x); break;
        case TmStep: out[i] = x >= y ? 1.0f : 0.0f; break;
        case TmSmoothstep: out[i] = tm_smoothstep(y, z, x); break;
        case TmRemap: out[i] = tm_remap(x, range, false); break;
        case TmRemapClamped: out[i] = tm_remap(x, range, true); break;
        default: out[i] = x + y; break;
        }
    }
}

/** Check whether a channel of an operation is too close to a discontinuity of the operation
 * for the precision of the GPU to give the value of the reference.
 *
 * @param op The operation
 * @param a Input a, broadcast
 * @param b Input b, broadcast
 * @param c Input c, broadcast
 * @param scalar Whether it computes on Scalars
 * @param i The channel
 * @return Whether it is
 */
static bool tm_fragile(
    tm_op_t op,
    const float a[4],
    const float b[4],
    const float c[4],
    bool scalar,
    int32_t i)
{
    float l;
    switch (op) {
    case TmFloor:
    case TmFract:
        return fabsf(a[i] - roundf(a[i])) < 1e-3f;
    case TmStep:
        return fabsf(a[i] - b[i]) < 1e-3f;
    case TmDivide:
        return b[i] != 0 && fabsf(b[i]) < 0.1f;
    case TmPower:
        return fabsf(a[i]) < 0.05f ||
            (b[i] != roundf(b[i]) && fabsf(b[i] - roundf(b[i])) < 1e-3f);
    case TmSmoothstep:
        return c[i] != b[i] && fabsf(c[i] - b[i]) < 0.05f;
    case TmNormalize:
        l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        return scalar ? fabsf(a[i]) < 1e-3f : l < 0.05f;
    default:
        return false;
    }
}

/** A math node of a sweep in its own graph */
typedef struct tm_case_t {
    ecs_entity_t tex;   /**< The texture entity */
    ecs_entity_t node;  /**< The math node */
    bool scalar;        /**< It computes on Scalars */
    bool output;        /**< It outputs a Scalar */
} tm_case_t;

/** Check every operation on the inputs of a sweep in a mode and a domain against the
 * reference, reading back the value of the math node of each.
 *
 * @param mode How the inputs are connected
 * @param domain The domain
 */
static void tm_sweep(
    tm_mode_t mode,
    TextureGraphDomain domain)
{
    ecs_world_t *world = tm_world();
    tm_case_t cases[TM_OPS];
    int32_t op, x, y, c, k;
    char what[64];
    for (op = 0; op < TM_OPS; op ++) {
        tm_case_t *t = &cases[op];
        ecs_entity_t uv = ecs_new(world);
        ecs_entity_t in[3], output;
        ecs_set(world, uv, TexCoords, {.scale = 1});
        for (k = 0; k < 3; k ++) {
            bool scalar = mode == TmScalar || (k == 0 && mode != TmVector);
            in[k] = 0;
            if (k && mode == TmScalarConst) {
                continue;
            }

            in[k] = tm_vector(world, uv, k);
            if (scalar) {
                in[k] = tm_split(world, in[k], (TextureChannel)k);
            }
        }

        t->scalar = tm_scalar_mode((tm_op_t)op, mode);
        t->output = tm_scalar_output((tm_op_t)op, t->scalar);
        t->tex = ecs_new(world);
        t->node = ecs_new(world);
        tm_node(world, t->node, (tm_op_t)op, in, tm_const[1], tm_const[2], tm_range);
        output = t->node;
        if (t->output) {
            output = ecs_new(world);
            ecs_set(world, output, TexCombine, {.r = t->node, .a_value = 1});
        }

        ecs_set(world, t->tex, TextureGraph, {.output = output, .width = TM_W,
            .height = TM_H, .domain = domain, .format = GraphHdr});
    }

    for (op = 0; op < TM_OPS; op ++) {
        tm_case_t *t = &cases[op];
        tm_wait(world, t->tex);
        float *texels = tm_read(world, t->tex, t->node, TM_W, TM_H);
        ecs_os_snprintf(what, ECS_SIZEOF(what), "op %d mode %d domain %d", op, (int)mode,
            (int)domain);
        for (y = 0; y < TM_H; y ++) {
            for (x = 0; x < TM_W; x ++) {
                float v[3][4], a[4], b[4], cc[4], expect[4];
                const float *texel = &texels[(y * TM_W + x) * 4];
                for (k = 0; k < 3; k ++) {
                    tm_input(domain, x, y, k, v[k]);
                }

                for (c = 0; c < 4; c ++) {
                    a[c] = mode == TmVector ? v[0][c] : v[0][0];
                    b[c] = mode == TmScalar ? v[1][1]
                        : mode == TmScalarConst ? tm_const[1][c] : v[1][c];
                    cc[c] = mode == TmScalar ? v[2][2]
                        : mode == TmScalarConst ? tm_const[2][c] : v[2][c];
                }

                tm_ref((tm_op_t)op, a, b, cc, tm_range, t->scalar, expect);
                if (t->output) {
                    expect[1] = 0;
                    expect[2] = 0;
                    expect[3] = 1;
                }

                for (c = 0; c < 4; c ++) {
                    int32_t at = t->output ? 0 : c;
                    if (tm_fragile((tm_op_t)op, a, b, cc, t->scalar, at) ||
                        !isfinite(expect[c]) || fabsf(expect[c]) > TM_LARGEST)
                    {
                        continue;
                    }

                    tm_expect(what, x, y, c, texel[c], expect[c]);
                }
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

void TextureNodesMath_vector_plane(void) {
    // textures/graph
    tm_sweep(TmVector, DomainPlane);
}

void TextureNodesMath_vector_sphere(void) {
    // textures/graph
    tm_sweep(TmVector, DomainSphere);
}

void TextureNodesMath_scalar_plane(void) {
    // textures/graph
    tm_sweep(TmScalar, DomainPlane);
}

void TextureNodesMath_scalar_sphere(void) {
    // textures/graph
    tm_sweep(TmScalar, DomainSphere);
}

void TextureNodesMath_broadcast_plane(void) {
    // textures/graph
    tm_sweep(TmMixed, DomainPlane);
}

void TextureNodesMath_broadcast_sphere(void) {
    // textures/graph
    tm_sweep(TmMixed, DomainSphere);
}

void TextureNodesMath_scalar_constants_plane(void) {
    // textures/graph
    tm_sweep(TmScalarConst, DomainPlane);
}

void TextureNodesMath_scalar_constants_sphere(void) {
    // textures/graph
    tm_sweep(TmScalarConst, DomainSphere);
}

/** An edge case of an operation with constant inputs and its expected values */
typedef struct tm_edge_t {
    tm_op_t op;   /**< The operation */
    float a[4];         /**< Input a */
    float b[4];         /**< Input b */
    float c[4];         /**< Input c */
    float range[4];     /**< Remap range */
    float vector[4];    /**< Value on Vector inputs, (x, 0, 0, 1) for a Scalar */
    float scalar[4];    /**< Value on Scalar inputs of each channel of the inputs */
} tm_edge_t;

/** The edge cases */
static const tm_edge_t tm_edges[] = {
    {TmDivide, {3, -2, 0, 5}, {0, 0, 0, 2}, {0}, {0}, {0, 0, 0, 2.5f}, {0, 0, 0, 2.5f}},
    {TmPower, {0, -2, -2, 0}, {-1, 3, 0.5f, 0}, {0}, {0}, {0, -8, 0, 1}, {0, -8, 0, 1}},
    {TmPower, {-2, 2, 4, -3}, {2, -1, 0.5f, 1}, {0}, {0}, {4, 0.5f, 2, -3},
        {4, 0.5f, 2, -3}},
    {TmNormalize, {0, 0, 0, 0.5f}, {0}, {0}, {0}, {0, 0, 0, 0.5f}, {0, 0, 0, 1}},
    {TmNormalize, {3, 0, -4, 2}, {0}, {0}, {0}, {0.6f, 0, -0.8f, 2}, {1, 0, -1, 1}},
    {TmSmoothstep, {0.5f, 0.2f, 0.7f, 0.4f}, {0.5f, 0.5f, 0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f, 0.5f}, {0}, {1, 0, 1, 0}, {1, 0, 1, 0}},
    {TmSmoothstep, {0.25f, 0, 1, 0.75f}, {0, 0, 0, 0}, {1, 1, 1, 1}, {0},
        {0.15625f, 0, 1, 0.84375f}, {0.15625f, 0, 1, 0.84375f}},
    {TmStep, {1, 0.5f, -1, 2}, {1, 1, -1, 3}, {0}, {0}, {1, 0, 1, 0}, {1, 0, 1, 0}},
    {TmFloor, {-0.25f, 1.5f, 2, -3}, {0}, {0}, {0}, {-1, 1, 2, -3}, {-1, 1, 2, -3}},
    {TmFract, {-0.25f, 1.5f, 2, -3}, {0}, {0}, {0}, {0.75f, 0.5f, 0, 0},
        {0.75f, 0.5f, 0, 0}},
    {TmAbs, {-1.5f, 0, 2, -3}, {0}, {0}, {0}, {1.5f, 0, 2, 3}, {1.5f, 0, 2, 3}},
    {TmNegate, {1, -2, 0, 3}, {0}, {0}, {0}, {-1, 2, 0, -3}, {-1, 2, 0, -3}},
    {TmSin, {0, TM_PI / 2, TM_PI, -TM_PI / 2}, {0}, {0}, {0}, {0, 1, 0, -1}, {0, 1, 0, -1}},
    {TmCos, {0, TM_PI / 2, TM_PI, 2 * TM_PI}, {0}, {0}, {0}, {1, 0, -1, 1}, {1, 0, -1, 1}},
    {TmDot, {1, 2, 3, 4}, {4, 5, 6, 7}, {0}, {0}, {32, 0, 0, 1}, {4, 10, 18, 28}},
    {TmLength, {3, 4, 0, -9}, {0}, {0}, {0}, {5, 0, 0, 1}, {3, 4, 0, 9}},
    {TmCross, {1, 0, 0, 5}, {0, 1, 0, 5}, {0}, {0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
    {TmRemap, {-5, 0.5f, 5, 0}, {0}, {0}, {0, 1, 10, 20}, {-40, 15, 60, 10},
        {-40, 15, 60, 10}},
    {TmRemapClamped, {-5, 0.5f, 5, 0}, {0}, {0}, {0, 1, 10, 20}, {10, 15, 20, 10},
        {10, 15, 20, 10}},
    {TmRemap, {0, 1, 2, 3}, {0}, {0}, {1, 1, 2, 3}, {2, 2, 2, 2}, {2, 2, 2, 2}},
    {TmClamp, {-1, 0.5f, 2, 0.25f}, {0, 0, 0, 0.5f}, {1, 1, 1, 1}, {0}, {0, 0.5f, 1, 0.5f},
        {0, 0.5f, 1, 0.5f}}
};

/** Create the graph of an edge case: Vector constant nodes, or TexSplit nodes without an input
 * that take one channel of the constants.
 *
 * @param world The world
 * @param e The edge case
 * @param channel Channel of the Scalar inputs, -1 for Vector inputs
 * @param domain The domain
 * @param node Receives the math node
 * @return The texture entity
 */
static ecs_entity_t tm_edge_graph(
    ecs_world_t *world,
    const tm_edge_t *e,
    int32_t channel,
    TextureGraphDomain domain,
    ecs_entity_t *node)
{
    const float *values[3] = {e->a, e->b, e->c};
    ecs_entity_t in[3], output, tex = ecs_new(world);
    int32_t k;
    for (k = 0; k < 3; k ++) {
        const float *v = values[k];
        in[k] = ecs_new(world);
        if (channel < 0) {
            ecs_set(world, in[k], TexConstant, {.value = {v[0], v[1], v[2], v[3]}});
        } else {
            ecs_set(world, in[k], TexSplit, {.value = {v[0], v[1], v[2], v[3]},
                .channel = (TextureChannel)channel});
        }
    }

    *node = ecs_new(world);
    tm_node(world, *node, e->op, in, tm_const[0], tm_const[0], e->range);
    output = *node;
    if (tm_scalar_output(e->op, channel >= 0)) {
        output = ecs_new(world);
        ecs_set(world, output, TexCombine, {.r = *node, .a_value = 1});
    }

    ecs_set(world, tex, TextureGraph, {.output = output, .width = 4, .height = 4,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Check the edge cases on Vector and Scalar inputs in a domain.
 *
 * @param domain The domain
 */
static void tm_edges_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tm_world();
    int32_t count = (int32_t)(sizeof(tm_edges) / sizeof(tm_edges[0]));
    int32_t i, ch, c, x, y;
    char what[64];
    for (i = 0; i < count; i ++) {
        const tm_edge_t *e = &tm_edges[i];
        for (ch = -1; ch < 4; ch ++) {
            ecs_entity_t node;
            ecs_entity_t tex = tm_edge_graph(world, e, ch, domain, &node);
            float expect[4];
            bool scalar = tm_scalar_output(e->op, ch >= 0);
            if (ch < 0) {
                ecs_os_memcpy(expect, e->vector, ECS_SIZEOF(expect));
            } else if (scalar) {
                expect[0] = e->scalar[ch];
                expect[1] = 0;
                expect[2] = 0;
                expect[3] = 1;
            } else {
                ecs_os_memcpy(expect, e->scalar, ECS_SIZEOF(expect));
            }

            tm_wait(world, tex);
            float *texels = tm_read(world, tex, node, 4, 4);
            ecs_os_snprintf(what, ECS_SIZEOF(what), "edge %d op %d channel %d", i,
                (int)e->op, ch);
            for (y = 0; y < 4; y ++) {
                for (x = 0; x < 4; x ++) {
                    for (c = 0; c < 4; c ++) {
                        tm_expect(what, x, y, c, texels[(y * 4 + x) * 4 + c], expect[c]);
                    }
                }
            }

            ecs_os_free(texels);
        }
    }

    ecs_fini(world);
}

void TextureNodesMath_edge_cases_plane(void) {
    // textures/graph
    tm_edges_in(DomainPlane);
}

void TextureNodesMath_edge_cases_sphere(void) {
    // textures/graph
    tm_edges_in(DomainSphere);
}

/** Split a vector into its four channels and combine them again, in order and reversed: each
 * split reads back as its channel, the combine in order as the vector.
 *
 * @param domain The domain
 */
static void tm_split_round_trip(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tm_world();
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t reversed_tex = ecs_new(world);
    ecs_entity_t combine = ecs_new(world);
    ecs_entity_t reversed = ecs_new(world);
    ecs_entity_t split[4];
    int32_t k, x, y, c;
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_entity_t vector = tm_vector(world, uv, 0);
    for (k = 0; k < 4; k ++) {
        split[k] = tm_split(world, vector, (TextureChannel)k);
    }

    ecs_set(world, combine, TexCombine, {.r = split[0], .g = split[1], .b = split[2],
        .a = split[3]});
    ecs_set(world, reversed, TexCombine, {.r = split[3], .g = split[2], .b = split[1],
        .a = split[0]});
    ecs_set(world, tex, TextureGraph, {.output = combine, .width = TM_W, .height = TM_H,
        .domain = domain, .format = GraphHdr});
    ecs_set(world, reversed_tex, TextureGraph, {.output = reversed, .width = TM_W,
        .height = TM_H, .domain = domain, .format = GraphHdr});
    tm_wait(world, tex);
    tm_wait(world, reversed_tex);

    float *image = tm_read(world, tex, 0, TM_W, TM_H);
    float *back = tm_read(world, reversed_tex, 0, TM_W, TM_H);
    float *channels[4];
    for (k = 0; k < 4; k ++) {
        channels[k] = tm_read(world, tex, split[k], TM_W, TM_H);
    }

    for (y = 0; y < TM_H; y ++) {
        for (x = 0; x < TM_W; x ++) {
            float v[4];
            int32_t at = (y * TM_W + x) * 4;
            tm_input(domain, x, y, 0, v);
            for (c = 0; c < 4; c ++) {
                tm_expect("combined", x, y, c, image[at + c], v[c]);
                tm_expect("reversed", x, y, c, back[at + c], v[3 - c]);
            }

            for (k = 0; k < 4; k ++) {
                const float expect[4] = {v[k], 0, 0, 1};
                for (c = 0; c < 4; c ++) {
                    tm_expect("split", x, y, c, channels[k][at + c], expect[c]);
                }
            }
        }
    }

    for (k = 0; k < 4; k ++) {
        ecs_os_free(channels[k]);
    }

    ecs_os_free(back);
    ecs_os_free(image);
    ecs_fini(world);
}

void TextureNodesMath_split_round_trip_plane(void) {
    // textures/graph
    tm_split_round_trip(DomainPlane);
}

void TextureNodesMath_split_round_trip_sphere(void) {
    // textures/graph
    tm_split_round_trip(DomainSphere);
}

/** Combine two Scalar nodes and two constants into a vector and split each channel out again,
 * combined once more as the output: each split reads back as its Scalar.
 *
 * @param domain The domain
 */
static void tm_combine_round_trip(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tm_world();
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t wave = ecs_new(world);
    ecs_entity_t combine = ecs_new(world);
    ecs_entity_t output = ecs_new(world);
    ecs_entity_t split[4];
    int32_t k, x, y, c;
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_entity_t vector = tm_vector(world, uv, 1);
    ecs_entity_t green = tm_split(world, vector, ChannelG);
    ecs_entity_t blue = tm_split(world, vector, ChannelB);
    ecs_set(world, wave, TexSin, {.input = blue});
    ecs_set(world, combine, TexCombine, {.r = green, .g = wave, .b_value = 0.25f,
        .a_value = 0.75f});
    for (k = 0; k < 4; k ++) {
        split[k] = tm_split(world, combine, (TextureChannel)k);
    }

    ecs_set(world, output, TexCombine, {.r = split[0], .g = split[1], .b = split[2],
        .a = split[3]});
    ecs_set(world, tex, TextureGraph, {.output = output, .width = TM_W, .height = TM_H,
        .domain = domain, .format = GraphHdr});
    tm_wait(world, tex);

    float *image = tm_read(world, tex, 0, TM_W, TM_H);
    float *combined = tm_read(world, tex, combine, TM_W, TM_H);
    float *channels[4];
    for (k = 0; k < 4; k ++) {
        channels[k] = tm_read(world, tex, split[k], TM_W, TM_H);
    }

    for (y = 0; y < TM_H; y ++) {
        for (x = 0; x < TM_W; x ++) {
            float v[4];
            int32_t at = (y * TM_W + x) * 4;
            tm_input(domain, x, y, 1, v);
            const float scalars[4] = {v[1], sinf(v[2]), 0.25f, 0.75f};
            for (c = 0; c < 4; c ++) {
                tm_expect("combined", x, y, c, combined[at + c], scalars[c]);
                tm_expect("recombined", x, y, c, image[at + c], scalars[c]);
            }

            for (k = 0; k < 4; k ++) {
                const float expect[4] = {scalars[k], 0, 0, 1};
                for (c = 0; c < 4; c ++) {
                    tm_expect("split", x, y, c, channels[k][at + c], expect[c]);
                }
            }
        }
    }

    for (k = 0; k < 4; k ++) {
        ecs_os_free(channels[k]);
    }

    ecs_os_free(combined);
    ecs_os_free(image);
    ecs_fini(world);
}

void TextureNodesMath_combine_round_trip_plane(void) {
    // textures/graph
    tm_combine_round_trip(DomainPlane);
}

void TextureNodesMath_combine_round_trip_sphere(void) {
    // textures/graph
    tm_combine_round_trip(DomainSphere);
}

/** TexSplit, TexCombine and a clamped remap authored in a script from test/assets: the
 * coordinates swizzled, (v, u, min(2 * u, 1), 1).
 */
void TextureNodesMath_script(void) {
    // textures/graph
    ecs_world_t *world = tm_world();
    char *path = box_app_locate(GRAPH_CHANNELS);
    int32_t x, y, c;
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tex = ecs_lookup(world, "graph_channels");
    test_assert(tex != 0);
    tm_wait(world, tex);

    float *image = tm_read(world, tex, 0, TM_W, TM_H);
    for (y = 0; y < TM_H; y ++) {
        for (x = 0; x < TM_W; x ++) {
            float uv[4];
            tm_coords(DomainPlane, x, y, uv);
            const float expect[4] = {uv[1], uv[0], fminf(2 * uv[0], 1), 1};
            for (c = 0; c < 4; c ++) {
                tm_expect("swizzle", x, y, c, image[(y * TM_W + x) * 4 + c], expect[c]);
            }
        }
    }

    ecs_os_free(image);
    ecs_fini(world);
}
