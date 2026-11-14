/**
 * @file TextureGraphInterpolate.c
 * @brief Tests for TextureGraph.interpolate on the GPU backend: an animated graph with a rate
 * evaluates its keys k / rate one key ahead and crossfades them into its image every frame.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Quads whose emission samples interpolated graphs of the world time at a rate of 2 */
#define GRAPH_INTERPOLATED "test/assets/textures/graph/interpolated.flecs"

/** A quad whose emission samples an animated graph of the world time */
#define GRAPH_ANIMATED "test/assets/textures/graph/animated.flecs"

/** Frames a test waits at most for an outcome */
#define TI_FRAMES (40)

/** Difference a half float channel of a value up to 8 may have from its value */
#define TI_TOLERANCE_TIME (1e-2f)

/** Difference a display encoded RGBA8 channel may have from its value */
#define TI_TOLERANCE_8 (2.5f / 255.0f)

/** Updates per second of the graphs of GRAPH_INTERPOLATED */
#define TI_RATE (2.0)

/** Bytes of the two RGBA16Float keys of an 8x8 interpolated graph */
#define TI_KEY_BYTES_HDR (2 * 8 * 8 * 8)

/** Bytes of the two RGBA8Unorm keys of an 8x8 interpolated graph */
#define TI_KEY_BYTES_COLOR (2 * 8 * 8 * 4)

/** Ticket of the next readback */
static uint32_t ti_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* ti_world(void)
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
static void ti_frames(
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
static void ti_run_file(
    ecs_world_t *world,
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
}

/** Run frames until a texture is ready with a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 */
static void ti_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TI_FRAMES; i ++) {
        ti_frames(world, 1);
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
}

/** Read back the first channel of the first texel of the 8x8 image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The channel
 */
static float ti_read_value(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    uint32_t ticket = ti_ticket ++;
    float result = 0;
    bool found = false;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TI_FRAMES && !found; i ++) {
        ti_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !found) {
                test_assert(r->texels != NULL);
                test_int(r->width, 8);
                test_int(r->height, 8);
                result = r->texels[0];
                found = true;
            }

            ecs_os_free(r->texels);
        }
    }

    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
    test_assert(found);

    return result;
}

/** Check a value against the expected one within a tolerance.
 *
 * @param value The value
 * @param expect The expected value
 * @param tolerance Largest difference
 */
static void ti_near(
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
static box_render_gpu_stats_t ti_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t result;
    test_bool(box_render_gpu_stats(world, &result), true);

    return result;
}

/** Get the seconds of the world clock.
 *
 * @param world The world
 * @return The seconds
 */
static double ti_time(
    ecs_world_t *world)
{
    return ecs_get_world_info(world)->world_time_total;
}

/** Run frames until the world clock is between 0.35 and 0.65 of the way from one key to the
 * next, then pause it and run frames so the paused image is written.
 *
 * @param world The world
 * @return The seconds of the paused clock
 */
static double ti_pause_mid_key(
    ecs_world_t *world)
{
    int32_t i;
    double f = 0;
    for (i = 0; i < TI_FRAMES; i ++) {
        double t = ti_time(world) * TI_RATE;
        f = t - floor(t);
        if (f > 0.35 && f < 0.65) {
            break;
        }

        ti_frames(world, 1);
    }

    test_assert(f > 0.35 && f < 0.65);
    ecs_set_time_scale(world, 0);
    ti_frames(world, 3);

    return ti_time(world);
}

/** Display encode a linear channel.
 *
 * @param c The channel, 0..1
 * @return The encoded channel
 */
static float ti_srgb(
    float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/** Between two keys the image of an interpolated graph is the crossfade of the keys: for a
 * graph linear in time it is the value at the time of the clock, far from either key, and its
 * two key images are reported. Blended frames keep the revision. */
void TextureGraphInterpolate_blend_between_keys(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_interpolated.hdr");
    test_assert(tex != 0);
    ti_wait(world, tex, 1);
    ti_frames(world, 2);

    box_render_gpu_stats_t before = ti_stats(world);
    ti_frames(world, 10);
    box_render_gpu_stats_t after = ti_stats(world);
    test_assert(after.texture_graph_blends - before.texture_graph_blends >= 10);
    test_int(after.texture_graph_key_bytes, TI_KEY_BYTES_HDR + TI_KEY_BYTES_COLOR);

    double t = ti_pause_mid_key(world);
    float value = ti_read_value(world, tex);
    double k = floor(t * TI_RATE);
    float key0 = (float)(2 * k / TI_RATE + 0.5), key1 = (float)(2 * (k + 1) / TI_RATE + 0.5);
    ti_near(value, (float)(2 * t + 0.5), TI_TOLERANCE_TIME);
    test_assert(value > key0 + 0.3f);
    test_assert(value < key1 - 0.3f);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    ecs_fini(world);
}

/** A GraphColor graph on the sphere crossfades its keys in linear light: the stored value is
 * the display encoding of the linear value at the time of the clock. */
void TextureGraphInterpolate_color_linear_blend(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_interpolated.color");
    test_assert(tex != 0);
    ti_wait(world, tex, 1);

    double t = ti_pause_mid_key(world);
    test_assert(t * 0.25 < 1);
    float value = ti_read_value(world, tex);
    ti_near(value, ti_srgb((float)(0.25 * t)), TI_TOLERANCE_8);
    ecs_fini(world);
}

/** A paused clock shows a constant image: no key evaluations and no crossfades while it stays
 * paused. */
void TextureGraphInterpolate_paused_constant(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_interpolated.hdr");
    test_assert(tex != 0);
    ti_wait(world, tex, 1);
    ti_frames(world, 5);

    double t = ti_pause_mid_key(world);
    box_render_gpu_stats_t before = ti_stats(world);
    float a = ti_read_value(world, tex);
    ti_frames(world, 10);
    float b = ti_read_value(world, tex);
    box_render_gpu_stats_t after = ti_stats(world);
    test_assert(a == b);
    ti_near(a, (float)(2 * t + 0.5), TI_TOLERANCE_TIME);
    test_int(after.texture_graph_blends, before.texture_graph_blends);
    test_int(after.texture_graph_evaluations, before.texture_graph_evaluations);

    ecs_set_time_scale(world, 1);
    ti_frames(world, 3);
    float c = ti_read_value(world, tex);
    test_assert(c > b);
    ecs_fini(world);
}

/** A parameter change between two keys evaluates both keys of the new graph: the image is the
 * new graph at the time of the clock, with nothing of the old one blended in. */
void TextureGraphInterpolate_param_change_no_old_blend(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_interpolated.hdr");
    ecs_entity_t time = ecs_lookup(world, "graph_interpolated.hdr.time");
    test_assert(tex != 0 && time != 0);
    ti_wait(world, tex, 1);
    ti_frames(world, 5);

    double t = ti_pause_mid_key(world);
    box_render_gpu_stats_t before = ti_stats(world);
    ecs_set(world, time, TexTime, {.scale = 2, .offset = 4.5f});
    ti_wait(world, tex, 2);
    box_render_gpu_stats_t after = ti_stats(world);
    test_assert(after.texture_graph_evaluations - before.texture_graph_evaluations >= 2);
    float value = ti_read_value(world, tex);
    ti_near(value, (float)(2 * t + 4.5), TI_TOLERANCE_TIME);
    ecs_fini(world);
}

/** A jump of the clock past the keys evaluates both keys again at the new time, so the image
 * follows the clock at once. */
void TextureGraphInterpolate_clock_jump(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_interpolated.hdr");
    test_assert(tex != 0);
    ti_wait(world, tex, 1);
    ti_frames(world, 2);

    box_render_gpu_stats_t before = ti_stats(world);
    ecs_progress(world, 3.25f);
    box_render_sync(world);
    box_render_gpu_stats_t after = ti_stats(world);
    test_assert(after.texture_graph_evaluations - before.texture_graph_evaluations >= 2);
    ecs_set_time_scale(world, 0);
    ti_frames(world, 3);
    double t = ti_time(world);
    float value = ti_read_value(world, tex);
    ti_near(value, (float)(2 * t + 0.5), TI_TOLERANCE_TIME);
    ecs_fini(world);
}

/** An interpolated graph no draw samples neither evaluates its keys nor crossfades them; once a
 * draw samples it again it follows the clock. */
void TextureGraphInterpolate_undrawn_skips(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_INTERPOLATED);
    ecs_entity_t hdr = ecs_lookup(world, "graph_interpolated.hdr");
    ecs_entity_t color = ecs_lookup(world, "graph_interpolated.color");
    ecs_entity_t hdr_quad = ecs_lookup(world, "graph_interpolated.hdr_quad");
    ecs_entity_t color_quad = ecs_lookup(world, "graph_interpolated.color_quad");
    test_assert(hdr != 0 && color != 0 && hdr_quad != 0 && color_quad != 0);
    ti_wait(world, hdr, 1);
    ti_wait(world, color, 1);

    ecs_set(world, hdr_quad, PbrTextures, {0});
    ecs_set(world, color_quad, PbrTextures, {0});
    ti_frames(world, 3);
    box_render_gpu_stats_t before = ti_stats(world);
    ti_frames(world, 30);
    box_render_gpu_stats_t after = ti_stats(world);
    test_int(after.texture_graph_blends, before.texture_graph_blends);
    test_int(after.texture_graph_evaluations, before.texture_graph_evaluations);

    ecs_set(world, hdr_quad, PbrTextures, {.emissive = hdr});
    ti_frames(world, 3);
    box_render_gpu_stats_t drawn = ti_stats(world);
    test_assert(drawn.texture_graph_blends > after.texture_graph_blends);
    double t = ti_pause_mid_key(world);
    float value = ti_read_value(world, hdr);
    ti_near(value, (float)(2 * t + 0.5), TI_TOLERANCE_TIME);
    ecs_fini(world);
}

/** A graph with a rate and without interpolate is unchanged: it updates at its rate, steps
 * between updates, crossfades nothing and keeps no key images. */
void TextureGraphInterpolate_off_unchanged(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    test_assert(tex != 0);
    TextureGraph *graph = ecs_get_mut(world, tex, TextureGraph);
    graph->rate = (float)TI_RATE;
    ecs_modified(world, tex, TextureGraph);
    ti_wait(world, tex, 1);
    ti_frames(world, 2);

    box_render_gpu_stats_t before = ti_stats(world);
    ti_frames(world, 60);
    box_render_gpu_stats_t after = ti_stats(world);
    int32_t updates = after.texture_graph_updates - before.texture_graph_updates;
    if (updates < 1 || updates > 3) {
        printf("updates at rate 2: %d\n", updates);
    }

    test_assert(updates >= 1 && updates <= 3);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, updates);
    test_int(after.texture_graph_blends, 0);
    test_int(after.texture_graph_key_bytes, 0);

    double t = ti_pause_mid_key(world);
    float value = ti_read_value(world, tex);
    double k = floor(t * TI_RATE);
    test_assert(value >= (float)(2 * k / TI_RATE + 0.5) - TI_TOLERANCE_TIME);
    test_assert(value < (float)(2 * t + 0.5) - 0.2f);
    ecs_fini(world);
}

/** Turning interpolate on and off again frees the key images. */
void TextureGraphInterpolate_toggle_frees_keys(void) {
    // textures/graph
    ecs_world_t *world = ti_world();
    ti_run_file(world, GRAPH_ANIMATED);
    ecs_entity_t tex = ecs_lookup(world, "graph_animated.image");
    test_assert(tex != 0);
    TextureGraph *graph = ecs_get_mut(world, tex, TextureGraph);
    graph->rate = (float)TI_RATE;
    graph->interpolate = true;
    ecs_modified(world, tex, TextureGraph);
    ti_wait(world, tex, 1);
    ti_frames(world, 2);
    test_int(ti_stats(world).texture_graph_key_bytes, TI_KEY_BYTES_HDR);

    graph = ecs_get_mut(world, tex, TextureGraph);
    graph->interpolate = false;
    ecs_modified(world, tex, TextureGraph);
    ti_wait(world, tex, 2);
    ti_frames(world, 2);
    test_int(ti_stats(world).texture_graph_key_bytes, 0);
    ecs_fini(world);
}
