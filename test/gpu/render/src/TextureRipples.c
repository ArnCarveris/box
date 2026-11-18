/**
 * @file TextureRipples.c
 * @brief Tests for the TexRipples node of TextureGraph on the GPU backend: the image tiles, a
 * density of 0 is flat, fewer drops fall at a lower density, the rings move with the time and
 * the height stays in its range.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames to wait for an evaluation or a read back */
#define TR_FRAMES (40)

/** Tolerance of values that should be equal */
#define TR_TOLERANCE (3e-3f)

/** Size of the images */
#define TR_SIZE (64)

/** Ticket of the next read back */
static uint32_t tr_ticket = 1;

/** Create a headless world that renders on the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* tr_world(void)
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

/** Run frames at 60 frames per second.
 *
 * @param world The world
 * @param count Number of frames
 */
static void tr_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is evaluated or failed.
 *
 * @param world The world
 * @param tex The texture entity
 * @return Its state
 */
static TextureState tr_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TR_FRAMES; i ++) {
        tr_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && (status->state == TextureFailed ||
            (status->state == TextureReady && status->revision >= 1)))
        {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("texture failed: %s\n", status->error ? status->error : "");
    }

    return status->state;
}

/** Read back the image of a graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tr_read(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    uint32_t ticket = tr_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TR_FRAMES && !result; i ++) {
        tr_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_int(r->width, TR_SIZE);
                test_int(r->height, TR_SIZE);
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

/** Evaluate a graph of one TexRipples node and read back its image.
 *
 * @param world The world
 * @param ripples The node
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tr_eval(
    ecs_world_t *world,
    const TexRipples *ripples)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, TexRipples, ripples);
    ecs_set(world, tex, TextureGraph, {.output = e, .width = TR_SIZE, .height = TR_SIZE,
        .format = GraphHdr});
    test_int(tr_wait(world, tex), TextureReady);

    return tr_read(world, tex);
}

/** Get a node of ripples with large rings at a fixed time.
 *
 * @param density The density
 * @param time The time in seconds
 * @return The node
 */
static TexRipples tr_node(
    float density,
    float time)
{
    return (TexRipples){.count = 16, .speed = 1.2f, .lifetime = 1, .width = 0.2f, .seed = 3,
        .time_value = time, .density_value = density};
}

/** Sum the absolute heights of an image.
 *
 * @param texels The texels
 * @return The sum
 */
static double tr_energy(
    const float *texels)
{
    double result = 0;
    int32_t i;
    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        result += fabs(texels[i * 4]);
    }

    return result;
}

/** The image repeats with the coordinates moved by whole domains, and the step from the last
 * column or row to the first is no larger than the steps inside the image. */
void TextureRipples_plane_tiles(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    TexRipples ripples = tr_node(1, 0.45f);
    float *base = tr_eval(world, &ripples);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shifted = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, shifted, TexAdd, {.a = uv, .b_value = {1, -2, 0, 0}});
    ripples.coords = shifted;
    float *moved = tr_eval(world, &ripples);
    int32_t x, y, n = TR_SIZE;
    float inner = 0, wrap = 0;
    for (y = 0; y < n; y ++) {
        for (x = 0; x < n; x ++) {
            float v = base[(y * n + x) * 4];
            test_assert(fabsf(v - moved[(y * n + x) * 4]) <= TR_TOLERANCE);
            float right = fabsf(v - base[(y * n + (x + 1) % n) * 4]);
            float down = fabsf(v - base[(((y + 1) % n) * n + x) * 4]);
            if (x == n - 1) {
                wrap = right > wrap ? right : wrap;
            } else {
                inner = right > inner ? right : inner;
            }

            if (y == n - 1) {
                wrap = down > wrap ? down : wrap;
            } else {
                inner = down > inner ? down : inner;
            }
        }
    }

    test_assert(inner > 0);
    test_assert(wrap <= inner + TR_TOLERANCE);

    ecs_os_free(base);
    ecs_os_free(moved);
    ecs_fini(world);
}

/** A density of 0 lets no drop fall: the surface is flat */
void TextureRipples_density_zero(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    TexRipples ripples = tr_node(0, 0.45f);
    float *texels = tr_eval(world, &ripples);
    int32_t i;
    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        test_assert(fabsf(texels[i * 4]) <= 1e-6f);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** A lower density lets fewer drops fall: less of the surface moves, and at density 1 the
 * heights stay within the overlap of a few rings */
void TextureRipples_density_follows(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    TexRipples ripples = tr_node(1, 0.45f);
    float *full = tr_eval(world, &ripples);
    ripples = tr_node(0.3f, 0.45f);
    float *light = tr_eval(world, &ripples);
    double e_full = tr_energy(full), e_light = tr_energy(light);
    test_assert(e_light > 0);
    test_assert(e_light < e_full * 0.7);

    float peak = 0;
    int32_t i;
    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        float v = fabsf(full[i * 4]);
        peak = v > peak ? v : peak;
    }

    test_assert(peak > 0.2f);
    test_assert(peak < 4.0f);

    ecs_os_free(full);
    ecs_os_free(light);
    ecs_fini(world);
}

/** The rings move with the time: the image of another time differs, the same time repeats */
void TextureRipples_time_moves(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    TexRipples ripples = tr_node(1, 0.3f);
    float *a = tr_eval(world, &ripples);
    float *again = tr_eval(world, &ripples);
    ripples = tr_node(1, 0.5f);
    float *b = tr_eval(world, &ripples);
    int32_t i, changed = 0;
    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        test_assert(fabsf(a[i * 4] - again[i * 4]) <= TR_TOLERANCE);
        changed += fabsf(a[i * 4] - b[i * 4]) > 0.05f;
    }

    test_assert(changed > TR_SIZE * TR_SIZE / 20);

    ecs_os_free(a);
    ecs_os_free(again);
    ecs_os_free(b);
    ecs_fini(world);
}
