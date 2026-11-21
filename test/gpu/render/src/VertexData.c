/**
 * @file VertexData.c
 * @brief Tests for the per vertex data streams of meshes and the VertexReveal material
 * component, checked on PNG captures and on the counters of the GPU backend.
 *
 * The cases look straight down from 12 units above a white floor into a 128x128 capture: image
 * right is +x and image down is +z. One Mesh3 holds four flat cards one unit above the floor,
 * one unit square each with their centers at x -3.5, -1.5, 0.5 and 2.5; a sun at 45 degrees
 * casts their shadows one unit along +z. Every vertex of card i has data0.x (i + 0.5) / 4 and
 * data1.y 1 minus that, so a threshold hides whole cards.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the captures in pixels */
#define DATA_SIZE (128)

/** Seconds every frame of the cases advances the world */
#define DATA_DT (1.0f / 60.0f)

/** Cards of the mesh */
#define DATA_CARDS (4)

/** Pixels of one unit at the height of the cards, 11 units below the camera */
#define DATA_CARD_UNIT ((float)DATA_SIZE / (22.0f * tanf(GLM_PIf / 6.0f)))

/** Pixels of one unit on the floor, 12 units below the camera */
#define DATA_FLOOR_UNIT ((float)DATA_SIZE / (24.0f * tanf(GLM_PIf / 6.0f)))

/** Create the world of the cases: the camera above the origin looking down, a white floor, a sun
 * at 45 degrees that shadows toward +z, a little ambient light and shadows, in a scene with
 * Scene.alpha_cutout.
 *
 * @return The world
 */
static ecs_world_t* data_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.2f});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 12, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    return world;
}

/** Value of data0.x of the vertices of a card.
 *
 * @param card The card
 * @return The value
 */
static float data_value(
    int32_t card)
{
    return ((float)card + 0.5f) / (float)DATA_CARDS;
}

/** Set the Mesh3 of the cards on an entity, each card two triangles of its own four vertices
 * facing up.
 *
 * @param world The world
 * @param e The entity
 * @param data Whether the vertices have data0 and data1
 */
static void data_cards_set(
    ecs_world_t *world,
    ecs_entity_t e,
    bool data)
{
    static const float corners[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
    static const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
    Mesh3 value = {0}, *m = &value;
    int32_t count = DATA_CARDS * 4, i, k;
    ecs_vec_init_t(NULL, &m->vertices, vec3, count);
    ecs_vec_init_t(NULL, &m->normals, vec3, count);
    ecs_vec_init_t(NULL, &m->indices, uint32_t, DATA_CARDS * 6);
    ecs_vec_init_t(NULL, &m->data0, vec4, count);
    ecs_vec_init_t(NULL, &m->data1, vec4, count);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, count);
    ecs_vec_set_count_t(NULL, &m->normals, vec3, count);
    ecs_vec_set_count_t(NULL, &m->indices, uint32_t, DATA_CARDS * 6);
    ecs_vec_set_count_t(NULL, &m->data0, vec4, data ? count : 0);
    ecs_vec_set_count_t(NULL, &m->data1, vec4, data ? count : 0);
    for (i = 0; i < DATA_CARDS; i ++) {
        float x = -4.0f + 2.0f * (float)i;
        for (k = 0; k < 4; k ++) {
            int32_t v = i * 4 + k;
            glm_vec3_copy((vec3){ x + corners[k][0], 0.0f, corners[k][1] - 0.5f },
                *ecs_vec_get_t(&m->vertices, vec3, v));
            glm_vec3_copy((vec3){ 0, 1, 0 }, *ecs_vec_get_t(&m->normals, vec3, v));
            if (data) {
                glm_vec4_copy((vec4){ data_value(i), 0, 0, 0 },
                    *ecs_vec_get_t(&m->data0, vec4, v));
                glm_vec4_copy((vec4){ 0, 1.0f - data_value(i), 0, 0 },
                    *ecs_vec_get_t(&m->data1, vec4, v));
            }
        }

        for (k = 0; k < 6; k ++) {
            *ecs_vec_get_t(&m->indices, uint32_t, i * 6 + k) = (uint32_t)(i * 4) + quad[k];
        }
    }

    ecs_set_ptr(world, e, Mesh3, m);
    ecs_vec_fini_t(NULL, &m->vertices, vec3);
    ecs_vec_fini_t(NULL, &m->normals, vec3);
    ecs_vec_fini_t(NULL, &m->indices, uint32_t);
    ecs_vec_fini_t(NULL, &m->data0, vec4);
    ecs_vec_fini_t(NULL, &m->data1, vec4);
}

/** Add the cards one unit above the floor.
 *
 * @param world The world
 * @param data Whether the vertices have data0 and data1
 * @return The cards
 */
static ecs_entity_t data_cards(
    ecs_world_t *world,
    bool data)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 1, 0});
    ecs_set(world, e, Rgb, {200, 60, 40});
    data_cards_set(world, e, data);

    return e;
}

/** Advance the world by frames of DATA_DT.
 *
 * @param world The world
 * @param frames Frames
 */
static void data_frames(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, DATA_DT);
        box_render_sync(world);
    }
}

/** Read the counters of the GPU backend.
 *
 * @param world The world
 * @return The counters
 */
static box_render_gpu_stats_t data_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);

    return stats;
}

/** Render two frames into a PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* data_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = DATA_SIZE, .height = DATA_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    data_frames(world, 2);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, DATA_SIZE);
    test_int(h, DATA_SIZE);

    return px;
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The RGBA pixel
 */
static const unsigned char* data_pixel(
    const unsigned char *px,
    int x,
    int y)
{
    return &px[(y * DATA_SIZE + x) * 4];
}

/** Check whether a pixel shows a card: red, not the gray of the floor.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Whether it is red
 */
static bool data_red(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = data_pixel(px, x, y);

    return p[0] > 2 * p[1] + 20 && p[0] > 60;
}

/** Brightness of a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Red plus green plus blue
 */
static int data_sum(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = data_pixel(px, x, y);

    return p[0] + p[1] + p[2];
}

/** Check whether a card is drawn: the center of the card is red.
 *
 * @param px The pixels
 * @param card The card
 * @return Whether it is drawn
 */
static bool data_card_drawn(
    const unsigned char *px,
    int32_t card)
{
    float x = -3.5f + 2.0f * (float)card;
    int col = DATA_SIZE / 2 + (int)lroundf(x * DATA_CARD_UNIT);

    return data_red(px, col, DATA_SIZE / 2);
}

/** Check whether a card casts its shadow: the floor one unit along +z of the card, which no
 * card covers in the image, is darker than the lit floor between the shadows.
 *
 * @param px The pixels
 * @param card The card
 * @return Whether its shadow is drawn
 */
static bool data_card_shadowed(
    const unsigned char *px,
    int32_t card)
{
    float x = -3.5f + 2.0f * (float)card;
    int col = DATA_SIZE / 2 + (int)lroundf(x * DATA_FLOOR_UNIT);
    int lit_col = DATA_SIZE / 2 + (int)lroundf((x + 1.0f) * DATA_FLOOR_UNIT);
    int row = DATA_SIZE / 2 + (int)lroundf(1.1f * DATA_FLOOR_UNIT);

    return data_sum(px, col, row) * 2 < data_sum(px, lit_col, row);
}

/** Check the cards drawn and shadowed in a capture against a mask of the cards expected.
 *
 * @param px The pixels
 * @param drawn Bit i set when card i is drawn and casts its shadow
 */
static void data_expect(
    const unsigned char *px,
    uint32_t drawn)
{
    int32_t i;
    for (i = 0; i < DATA_CARDS; i ++) {
        test_bool(data_card_drawn(px, i), (drawn >> i) & 1u);
        test_bool(data_card_shadowed(px, i), (drawn >> i) & 1u);
    }
}

/** A mesh with per vertex data that nothing reads draws, with its shadows, exactly as the same
 * mesh without data */
void VertexData_draws_same_without(void) {
    // geometry/vertex_data
    ecs_world_t *world = data_world();
    ecs_entity_t cards = data_cards(world, false);
    data_frames(world, 3);
    unsigned char *plain = data_render(world, "vertex_data_plain.png");
    data_expect(plain, 0xF);

    data_cards_set(world, cards, true);
    data_frames(world, 3);
    unsigned char *data = data_render(world, "vertex_data_with.png");
    test_assert(!memcmp(plain, data, DATA_SIZE * DATA_SIZE * 4));
    test_int(data_stats(world).errors, 0);

    stbi_image_free(plain);
    stbi_image_free(data);
    ecs_fini(world);
}

/** A mesh without per vertex data binds no data stream in any pass; a mesh with data binds it */
void VertexData_no_data_binds_nothing(void) {
    // geometry/vertex_data
    ecs_world_t *world = data_world();
    ecs_entity_t cards = data_cards(world, false);
    ecs_set(world, cards, VertexReveal, {.channel = 0, .threshold = 0.5f});
    data_frames(world, 3);

    /* A VertexReveal without data reveals everything */
    unsigned char *px = data_render(world, "vertex_data_reveal_without.png");
    data_expect(px, 0xF);
    stbi_image_free(px);

    box_render_gpu_stats_t before = data_stats(world);
    px = data_render(world, "vertex_data_reveal_without.png");
    stbi_image_free(px);
    box_render_gpu_stats_t after = data_stats(world);
    test_assert(after.draws > before.draws);
    test_int(after.data_binds, 0);
    test_int(after.errors, 0);

    data_cards_set(world, cards, true);
    data_frames(world, 3);
    before = data_stats(world);
    px = data_render(world, "vertex_data_reveal_with.png");
    stbi_image_free(px);
    after = data_stats(world);
    test_assert(after.data_binds > before.data_binds);
    test_int(after.errors, 0);
    ecs_fini(world);
}

/** A VertexReveal hides the cards whose data0.x is below its threshold in the color and the
 * shadow passes; writing the threshold animates them */
void VertexData_reveal_thresholds(void) {
    // materials/vertex_reveal
    ecs_world_t *world = data_world();
    ecs_entity_t cards = data_cards(world, true);
    ecs_set(world, cards, VertexReveal, {.channel = 0, .threshold = 0});
    data_frames(world, 3);

    const float thresholds[5] = { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f };
    const uint32_t expected[5] = { 0xF, 0xE, 0xC, 0x8, 0x0 };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        VertexReveal *r = ecs_get_mut(world, cards, VertexReveal);
        r->threshold = thresholds[i];
        ecs_modified(world, cards, VertexReveal);
        data_frames(world, 2);
        char name[64];
        snprintf(name, sizeof(name), "vertex_data_threshold_%d.png", (int)i);
        unsigned char *px = data_render(world, name);
        data_expect(px, expected[i]);
        stbi_image_free(px);
    }

    test_int(data_stats(world).errors, 0);
    ecs_fini(world);
}

/** A VertexReveal reads the channel it names, data1.y as channel 5, and invert hides above the
 * threshold */
void VertexData_reveal_channel_invert(void) {
    // materials/vertex_reveal
    ecs_world_t *world = data_world();
    ecs_entity_t cards = data_cards(world, true);
    ecs_set(world, cards, VertexReveal, {.channel = 5, .threshold = 0.5f});
    data_frames(world, 3);
    unsigned char *px = data_render(world, "vertex_data_channel.png");
    data_expect(px, 0x3);
    stbi_image_free(px);

    ecs_set(world, cards, VertexReveal, {.channel = 0, .threshold = 0.5f, .invert = true});
    data_frames(world, 3);
    px = data_render(world, "vertex_data_invert.png");
    data_expect(px, 0x3);
    stbi_image_free(px);

    ecs_set(world, cards, VertexReveal, {.channel = 5, .threshold = 0.5f, .invert = true});
    data_frames(world, 3);
    px = data_render(world, "vertex_data_channel_invert.png");
    data_expect(px, 0xC);
    stbi_image_free(px);

    test_int(data_stats(world).errors, 0);
    ecs_fini(world);
}

/** A softness dithers the edge: on a card whose data0.x runs from 0 at its left edge to 1 at its
 * right edge, nothing is drawn well below the threshold, everything above the threshold plus the
 * softness and a part of the pixels in between */
void VertexData_reveal_soft_edge(void) {
    // materials/vertex_reveal
    ecs_world_t *world = data_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 1, 0});
    ecs_set(world, e, Rgb, {200, 60, 40});
    static const float corners[4][2] = { { -4, -2 }, { -4, 2 }, { 4, 2 }, { 4, -2 } };
    static const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
    Mesh3 value = {0}, *m = &value;
    int32_t i;
    ecs_vec_init_t(NULL, &m->vertices, vec3, 4);
    ecs_vec_init_t(NULL, &m->normals, vec3, 4);
    ecs_vec_init_t(NULL, &m->data0, vec4, 4);
    ecs_vec_init_t(NULL, &m->indices, uint32_t, 6);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, 4);
    ecs_vec_set_count_t(NULL, &m->normals, vec3, 4);
    ecs_vec_set_count_t(NULL, &m->data0, vec4, 4);
    ecs_vec_set_count_t(NULL, &m->indices, uint32_t, 6);
    for (i = 0; i < 4; i ++) {
        glm_vec3_copy((vec3){ corners[i][0], 0, corners[i][1] },
            *ecs_vec_get_t(&m->vertices, vec3, i));
        glm_vec3_copy((vec3){ 0, 1, 0 }, *ecs_vec_get_t(&m->normals, vec3, i));
        glm_vec4_copy((vec4){ corners[i][0] > 0 ? 1.0f : 0.0f, 0, 0, 0 },
            *ecs_vec_get_t(&m->data0, vec4, i));
    }

    for (i = 0; i < 6; i ++) {
        *ecs_vec_get_t(&m->indices, uint32_t, i) = quad[i];
    }

    ecs_set_ptr(world, e, Mesh3, m);
    ecs_vec_fini_t(NULL, &m->vertices, vec3);
    ecs_vec_fini_t(NULL, &m->normals, vec3);
    ecs_vec_fini_t(NULL, &m->data0, vec4);
    ecs_vec_fini_t(NULL, &m->indices, uint32_t);
    ecs_set(world, e, VertexReveal, {.channel = 0, .threshold = 0.25f, .softness = 0.5f});
    data_frames(world, 3);
    unsigned char *px = data_render(world, "vertex_data_soft.png");

    /* Columns of the card where the value is 0.1, 0.5 and 0.9 */
    int bands[3] = { 0, 0, 0 };
    const float values[3] = { 0.1f, 0.5f, 0.9f };
    int32_t b, row;
    for (b = 0; b < 3; b ++) {
        float x = -4.0f + 8.0f * values[b];
        int col = DATA_SIZE / 2 + (int)lroundf(x * DATA_CARD_UNIT);
        for (row = DATA_SIZE / 2 - 10; row < DATA_SIZE / 2 + 10; row ++) {
            int dx;
            for (dx = -2; dx <= 2; dx ++) {
                bands[b] += data_red(px, col + dx, row);
            }
        }
    }

    test_int(bands[0], 0);
    test_int(bands[2], 100);
    test_assert(bands[1] > 15 && bands[1] < 85);
    test_int(data_stats(world).errors, 0);
    stbi_image_free(px);
    ecs_fini(world);
}
