/**
 * @file TextureGraphCache.c
 * @brief Tests for the animation and the cache of TextureGraph on the GPU backend: animated graphs
 * that follow the world clock at their rate while drawn, static stages that run once, and the
 * content hash cache of materialized values shared by graphs, evicted under a budget.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** A quad whose emission samples an animated graph of the world time */
#define GRAPH_ANIMATED "test/assets/textures/graph/animated.flecs"

/** Frames a test waits at most for an outcome */
#define TG_FRAMES (40)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TG_TOLERANCE_16 (2e-3f)

/** Difference a half float channel of a value up to 8 may have from its value */
#define TG_TOLERANCE_TIME (1e-2f)

/** Bytes of a cached 8x8 RGBA16Float value */
#define TG_ENTRY_BYTES (8 * 8 * 8)

/** Node of a neighbourhood kind whose value is its input at the texel to the right */
typedef struct TgShift {
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TgShift;

/** WGSL of TgShift, which reads its materialized input with the shared helpers */
static const char *tg_shift_wgsl =
    "fn tg_shift(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    t0: texture_2d<f32>) -> vec4f {\n"
    "    return box_texgraph_load(t0, texel + vec2i(1, 0));\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tg_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tg_world(void)
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
static void tg_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run a script from the repository.
 *
 * @param world The world
 * @param file The script, from the repository root
 */
static void tg_run_file(
    ecs_world_t *world,
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
}

/** Run frames until a texture is ready with at least a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 * @return The status
 */
static const TextureStatus* tg_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TG_FRAMES; i ++) {
        tg_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state == TextureFailed) {
            break;
        }

        if (status && status->state == TextureReady && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("error: %s\n", status->error);
    }

    test_int(status->state, TextureReady);
    test_int(status->revision, revision);

    return status;
}

/** Read back the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tg_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tg_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TG_FRAMES && !result; i ++) {
        tg_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
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

/** Read back the first channel of the first texel of the 8x8 image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The channel
 */
static float tg_read_value(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    float *texels = tg_read(world, tex, 8, 8);
    float result = texels[0];
    ecs_os_free(texels);

    return result;
}

/** Check a value against the expected one within a tolerance.
 *
 * @param value The value
 * @param expect The expected value
 * @param tolerance Largest difference
 */
static void tg_near(
    float value,
    float expect,
    float tolerance)
{
    if (fabsf(value - expect) > tolerance) {
        printf("value %f, expected %f\n", value, expect);
    }

    test_assert(fabsf(value - expect) <= tolerance);
}

/** Get the statistics of the GPU backend.
 *
 * @param world The world
 * @return The statistics
 */
static box_render_gpu_stats_t tg_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t result;
    test_bool(box_render_gpu_stats(world, &result), true);

    return result;
}

/** Register the neighbourhood kind TgShift.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tg_register_shift(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TgShift" }),
        .type.size = ECS_SIZEOF(TgShift),
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexVector,
        .inputs = {{ "in", offsetof(TgShift, in), TexVector, "in_value",
            offsetof(TgShift, in_value) }},
        .function = "tg_shift",
        .source = tg_shift_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    }) > 0);

    return c;
}

/** Create a static graph of the coordinates read a texel to the right, times a constant: the
 * coordinates are materialized for the shift in a first stage, which the output stage reads.
 *
 * @param world The world
 * @param shift_c The TgShift component
 * @param scale Scale of the coordinates
 * @param factor The constant of the multiply
 * @param uv Receives the coordinates node
 * @param math Receives the multiply node, the output
 * @return The texture entity, 8x8 GraphHdr
 */
static ecs_entity_t tg_shifted(
    ecs_world_t *world,
    ecs_entity_t shift_c,
    float scale,
    float factor,
    ecs_entity_t *uv,
    ecs_entity_t *math)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t shift = ecs_new(world);
    TgShift s = {0};
    *uv = ecs_new(world);
    *math = ecs_new(world);
    s.in = *uv;
    ecs_set(world, *uv, TexCoords, {.scale = scale});
    ecs_set_id(world, shift, shift_c, ECS_SIZEOF(TgShift), &s);
    ecs_set(world, *math, TexMultiply, {.a = shift,
        .b_value = {factor, factor, factor, factor}});
    ecs_set(world, tex, TextureGraph, {.output = *math, .width = 8, .height = 8,
        .format = GraphHdr});

    return tex;
}

/** Check the image of a graph of tg_shifted at a texel.
 *
 * @param world The world
 * @param tex The texture entity
 * @param scale Scale of the coordinates
 * @param factor The constant of the multiply
 */
static void tg_expect_shifted(
    ecs_world_t *world,
    ecs_entity_t tex,
    float scale,
    float factor)
{
    float *texels = tg_read(world, tex, 8, 8);
    int32_t x = 2, y = 5;
    float u = ((float)((x + 1) % 8) + 0.5f) / 8.0f;
    float v = ((float)y + 0.5f) / 8.0f;
    const float *t = &texels[(y * 8 + x) * 4];
    tg_near(t[0], u * scale * factor, TG_TOLERANCE_16 * 4);
    tg_near(t[1], v * scale * factor, TG_TOLERANCE_16 * 4);
    tg_near(t[3], factor, TG_TOLERANCE_16 * 4);
    ecs_os_free(texels);
}

/** Set the scale of a TexCoords node.
 *
 * @param world The world
 * @param uv The node
 * @param scale The scale
 */
static void tg_set_scale(
    ecs_world_t *world,
    ecs_entity_t uv,
    float scale)
{
    ecs_set(world, uv, TexCoords, {.scale = scale});
}

/** An animated graph follows the world clock: its image changes between frames, holds the time
 * twice plus a half and stays the same, without updates, while the clock is paused. */
void TextureGraphCache_animated_changes(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    test_assert(tex != 0);
    tg_wait(world, tex, 1);
    tg_frames(world, 2);

    float a = tg_read_value(world, tex);
    tg_frames(world, 10);
    float b = tg_read_value(world, tex);
    if (b <= a) {
        printf("a %f, b %f\n", a, b);
    }

    test_assert(b > a);

    ecs_set_time_scale(world, 0);
    tg_frames(world, 3);
    double t = ecs_get_world_info(world)->world_time_total;
    box_render_gpu_stats_t before = tg_stats(world);
    float c = tg_read_value(world, tex);
    tg_frames(world, 10);
    float d = tg_read_value(world, tex);
    box_render_gpu_stats_t after = tg_stats(world);
    test_assert(c == d);
    test_int(after.texture_graph_updates, before.texture_graph_updates);
    tg_near(c, (float)(2 * t + 0.5), TG_TOLERANCE_TIME);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);

    ecs_set_time_scale(world, 1);
    tg_frames(world, 3);
    float e = tg_read_value(world, tex);
    test_assert(e > d);
    ecs_fini(world);
}

/** TextureGraph.rate limits the updates of an animated graph: every frame without one, ten per
 * second of the world clock at a rate of 10. */
void TextureGraphCache_rate_limits_updates(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    test_assert(tex != 0);
    tg_wait(world, tex, 1);
    tg_frames(world, 2);

    box_render_gpu_stats_t before = tg_stats(world);
    tg_frames(world, 60);
    box_render_gpu_stats_t after = tg_stats(world);
    int32_t updates = after.texture_graph_updates - before.texture_graph_updates;
    if (updates < 55 || updates > 60) {
        printf("updates every frame: %d\n", updates);
    }

    test_assert(updates >= 55 && updates <= 60);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, updates);

    TextureGraph *graph = ecs_get_mut(world, tex, TextureGraph);
    graph->rate = 10;
    ecs_modified(world, tex, TextureGraph);
    tg_wait(world, tex, 2);
    tg_frames(world, 2);

    before = tg_stats(world);
    tg_frames(world, 60);
    after = tg_stats(world);
    updates = after.texture_graph_updates - before.texture_graph_updates;
    if (updates < 9 || updates > 11) {
        printf("updates at rate 10: %d\n", updates);
    }

    test_assert(updates >= 9 && updates <= 11);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    ecs_fini(world);
}

/** An animated graph whose image no draw samples does not update and keeps its image; once a
 * draw samples it again it catches up with the world clock. */
void TextureGraphCache_undrawn_skips_update(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    ecs_entity_t quad = ecs_lookup(world, "graph_animated.quad");
    test_assert(tex != 0);
    test_assert(quad != 0);
    tg_wait(world, tex, 1);

    ecs_set(world, quad, PbrTextures, {0});
    tg_frames(world, 3);
    box_render_gpu_stats_t before = tg_stats(world);
    float a = tg_read_value(world, tex);
    tg_frames(world, 10);
    float b = tg_read_value(world, tex);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_updates, before.texture_graph_updates);
    test_int(after.texture_graph_dispatches, before.texture_graph_dispatches);
    test_assert(a == b);

    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    tg_frames(world, 3);
    box_render_gpu_stats_t drawn = tg_stats(world);
    test_assert(drawn.texture_graph_updates > after.texture_graph_updates);
    float c = tg_read_value(world, tex);
    double t = ecs_get_world_info(world)->world_time_total;
    if (c <= b) {
        printf("b %f, c %f\n", b, c);
    }

    test_assert(c > b);
    test_assert(c <= (float)(2 * t + 0.5) + TG_TOLERANCE_TIME);
    test_assert(c >= (float)(2 * (t - 0.25) + 0.5) - TG_TOLERANCE_TIME);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    ecs_fini(world);
}

/** An edit of a parameter after a materialized node evaluates only the stages after it: one
 * dispatch, no new cached value; an edit before it runs both stages and caches a new value. */
void TextureGraphCache_downstream_edit_keeps_upstream(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = tg_register_shift(world);
    ecs_entity_t uv, math;
    tg_frames(world, 1);
    box_render_gpu_stats_t before = tg_stats(world);
    ecs_entity_t tex = tg_shifted(world, shift_c, 1, 0.5f, &uv, &math);
    tg_wait(world, tex, 1);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 2);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 0);
    test_int(after.texture_graph_cached, 1);
    test_int(after.texture_graph_cached_bytes, TG_ENTRY_BYTES);
    tg_expect_shifted(world, tex, 1, 0.5f);

    before = tg_stats(world);
    TexMultiply *m = ecs_get_mut(world, math, TexMultiply);
    m->b_value[0] = m->b_value[1] = m->b_value[2] = m->b_value[3] = 0.25f;
    ecs_modified(world, math, TexMultiply);
    tg_wait(world, tex, 2);
    after = tg_stats(world);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 1);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 1);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 0);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 0);
    test_int(after.texture_graph_cached, 1);
    tg_expect_shifted(world, tex, 1, 0.25f);

    before = tg_stats(world);
    tg_set_scale(world, uv, 2);
    tg_wait(world, tex, 3);
    after = tg_stats(world);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 2);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_cached, 2);
    tg_expect_shifted(world, tex, 2, 0.25f);
    ecs_fini(world);
}

/** Two graphs with an identical subgraph, as two textures of one template, share its cached
 * value: the second takes it from the cache and dispatches only its output stage. */
void TextureGraphCache_shared_subgraph(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = tg_register_shift(world);
    ecs_entity_t a_uv, a_math, b_uv, b_math;
    ecs_entity_t a = tg_shifted(world, shift_c, 1, 0.5f, &a_uv, &a_math);
    tg_wait(world, a, 1);
    box_render_gpu_stats_t before = tg_stats(world);
    test_int(before.texture_graph_cached, 1);

    ecs_entity_t b = tg_shifted(world, shift_c, 1, 0.25f, &b_uv, &b_math);
    tg_wait(world, b, 1);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 1);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 0);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 1);
    test_int(after.texture_graph_cached, 1);
    test_int(after.texture_graphs, 2);
    tg_expect_shifted(world, a, 1, 0.5f);
    tg_expect_shifted(world, b, 1, 0.25f);

    ecs_delete(world, a);
    tg_frames(world, 3);
    after = tg_stats(world);
    test_int(after.texture_graph_cached, 1);
    test_int(after.texture_graph_evictions, 0);
    tg_expect_shifted(world, b, 1, 0.25f);
    ecs_fini(world);
}

/** An edit undone takes the earlier value from the cache while it is still there: no stage
 * before the output runs. */
void TextureGraphCache_toggle_back_reuses(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = tg_register_shift(world);
    ecs_entity_t uv, math;
    ecs_entity_t tex = tg_shifted(world, shift_c, 1, 0.5f, &uv, &math);
    tg_wait(world, tex, 1);

    box_render_gpu_stats_t before = tg_stats(world);
    tg_set_scale(world, uv, 2);
    tg_wait(world, tex, 2);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_cached, 2);
    tg_expect_shifted(world, tex, 2, 0.5f);

    before = tg_stats(world);
    tg_set_scale(world, uv, 1);
    tg_wait(world, tex, 3);
    after = tg_stats(world);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 1);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 0);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 1);
    test_int(after.texture_graph_evictions - before.texture_graph_evictions, 0);
    test_int(after.texture_graph_cached, 2);
    tg_expect_shifted(world, tex, 1, 0.5f);
    ecs_fini(world);
}

/** Under a budget smaller than one value the value in use stays, the one an edit released is
 * evicted, and undoing the edit computes it again; with the default budget it is reused. */
void TextureGraphCache_eviction_under_budget(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = tg_register_shift(world);
    ecs_entity_t uv, math;
    ecs_singleton_set(world, TextureGraphCache, {.budget = 1});
    ecs_entity_t tex = tg_shifted(world, shift_c, 1, 0.5f, &uv, &math);
    tg_wait(world, tex, 1);
    box_render_gpu_stats_t before = tg_stats(world);
    test_int(before.texture_graph_cached, 1);
    test_int(before.texture_graph_cached_bytes, TG_ENTRY_BYTES);
    test_int(before.texture_graph_evictions, 0);

    tg_set_scale(world, uv, 2);
    tg_wait(world, tex, 2);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_evictions - before.texture_graph_evictions, 1);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_cached, 1);
    tg_expect_shifted(world, tex, 2, 0.5f);

    before = tg_stats(world);
    tg_set_scale(world, uv, 1);
    tg_wait(world, tex, 3);
    after = tg_stats(world);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 0);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_evictions - before.texture_graph_evictions, 1);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 2);
    test_int(after.texture_graph_cached, 1);
    tg_expect_shifted(world, tex, 1, 0.5f);

    ecs_singleton_set(world, TextureGraphCache, {.budget = 0});
    tg_set_scale(world, uv, 2);
    tg_wait(world, tex, 4);
    before = tg_stats(world);
    test_int(before.texture_graph_cached, 2);
    tg_set_scale(world, uv, 1);
    tg_wait(world, tex, 5);
    after = tg_stats(world);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 1);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 0);
    test_int(after.texture_graph_evictions - before.texture_graph_evictions, 0);
    tg_expect_shifted(world, tex, 1, 0.5f);
    ecs_fini(world);
}

/** An animated graph with a static part runs its static stages once: each update dispatches
 * only the animated output stage, reading the cached static value, and the revision of the
 * texture stays the same however many frames it updates. */
void TextureGraphCache_animated_static_part(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = tg_register_shift(world);
    tg_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    test_assert(tex != 0);
    tg_wait(world, tex, 1);

    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shift = ecs_new(world);
    ecs_entity_t time = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    ecs_entity_t math = ecs_new(world);
    TgShift s = {.in = uv};
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set_id(world, shift, shift_c, ECS_SIZEOF(TgShift), &s);
    ecs_set(world, time, TexTime, {.scale = 1});
    ecs_set(world, splat, TexSplat, {.in = time});
    ecs_set(world, math, TexAdd, {.a = shift, .b = splat});
    box_render_gpu_stats_t before = tg_stats(world);
    ecs_set(world, tex, TextureGraph, {.output = math, .width = 8, .height = 8,
        .format = GraphHdr});
    tg_wait(world, tex, 2);
    box_render_gpu_stats_t after = tg_stats(world);
    test_assert(after.texture_graph_misses - before.texture_graph_misses >= 2);
    tg_frames(world, 2);

    before = tg_stats(world);
    tg_frames(world, 20);
    after = tg_stats(world);
    int32_t updates = after.texture_graph_updates - before.texture_graph_updates;
    if (updates < 15) {
        printf("updates: %d\n", updates);
    }

    test_assert(updates >= 15);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, updates);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, updates);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 0);
    test_int(after.texture_graph_hits - before.texture_graph_hits, 0);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);

    ecs_set_time_scale(world, 0);
    tg_frames(world, 3);
    double t = ecs_get_world_info(world)->world_time_total;
    float *texels = tg_read(world, tex, 8, 8);
    int32_t x = 3, y = 1;
    float u = ((float)(x + 1) + 0.5f) / 8.0f;
    float v = ((float)y + 0.5f) / 8.0f;
    const float *texel = &texels[(y * 8 + x) * 4];
    tg_near(texel[0], u + (float)t, TG_TOLERANCE_TIME);
    tg_near(texel[1], v + (float)t, TG_TOLERANCE_TIME);
    tg_near(texel[2], (float)t, TG_TOLERANCE_TIME);
    ecs_os_free(texels);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    ecs_fini(world);
}

/** A TexTime of scale 0 holds its offset: its image does not follow the world clock and the
 * graph is not animated, so it makes no updates while the clock runs. */
void TextureGraphCache_time_zero_scale_still(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    ecs_entity_t time = ecs_lookup(world, "graph_animated.image.time");
    test_assert(tex != 0 && time != 0);
    ecs_set(world, time, TexTime, {.scale = 0, .offset = 0.5f});
    tg_wait(world, tex, 1);
    tg_frames(world, 30);

    box_render_gpu_stats_t before = tg_stats(world);
    float a = tg_read_value(world, tex);
    tg_frames(world, 30);
    float b = tg_read_value(world, tex);
    box_render_gpu_stats_t after = tg_stats(world);
    test_assert(ecs_get_world_info(world)->world_time_total > 0.5);
    tg_near(a, 0.5f, TG_TOLERANCE_TIME);
    tg_near(b, 0.5f, TG_TOLERANCE_TIME);
    test_int(after.texture_graph_updates, before.texture_graph_updates);
    test_int(after.texture_graph_evaluations, before.texture_graph_evaluations);
    ecs_fini(world);
}
