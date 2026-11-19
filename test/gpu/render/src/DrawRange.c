/**
 * @file DrawRange.c
 * @brief Tests for the draw distance of shapes (DrawRange): instances out of range are culled by
 * the GPU cull pass of each view and its shadow cascades, and the fade bands draw a dithered
 * share of the pixels.
 *
 * The camera sits at the origin and looks down -z into a 64x64 image. The shapes are unlit, so
 * a white shape shows white. A 20x20 wall 10 units in front of the camera covers the image; its
 * distance to the camera is the distance to its origin, 10.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define RANGE_SIZE (64)

/** Create a headless world that renders with the GPU on the main thread, with the blend modes.
 *
 * @return The world
 */
static ecs_world_t* range_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.blend_modes = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create the camera at the origin looking down -z and its view.
 *
 * @param world The world
 * @param lod Levels of detail of the view
 * @return The view
 */
static ecs_entity_t range_view(
    ecs_world_t *world,
    LodSettings lod)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {.camera = camera,
        .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lod = lod});

    return result;
}

/** Create a wall that covers the image.
 *
 * @param world The world
 * @param z Its z position, negative in front of the camera
 * @param color Its color
 * @return The wall
 */
static ecs_entity_t range_wall(
    ecs_world_t *world,
    float z,
    Rgb color)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {0, 0, z});
    ecs_set(world, result, Box, {20, 20, 0.1f});
    ecs_set_ptr(world, result, Rgb, &color);

    return result;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels, freed with stbi_image_free
 */
static unsigned char* range_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = RANGE_SIZE, .height = RANGE_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, RANGE_SIZE);
    test_int(h, RANGE_SIZE);
    ecs_os_free(path);

    return px;
}

/** Count the pixels of an image whose red, green and blue are above 200 where the mask has a
 * bit set: 1 red, 2 green, 4 blue.
 *
 * @param px The pixels
 * @param mask The channels that must be lit
 * @return The pixels with exactly those channels lit, the others below 30
 */
static int32_t range_count(
    const unsigned char *px,
    int32_t mask)
{
    int32_t i, c, result = 0;
    for (i = 0; i < RANGE_SIZE * RANGE_SIZE; i ++) {
        bool match = true;
        for (c = 0; c < 3; c ++) {
            int32_t v = px[i * 4 + c];
            match = match && ((mask & (1 << c)) ? v > 200 : v < 30);
        }

        result += match;
    }

    return result;
}

/** Run frames until the view has a CullStatus of a frame after a frame.
 *
 * @param world The world
 * @param view The view
 * @param after Frame the status must be newer than, -1 for any
 * @return The status
 */
static const CullStatus* range_status(
    ecs_world_t *world,
    ecs_entity_t view,
    int64_t after)
{
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 50; i ++) {
        const CullStatus *status = ecs_get(world, view, CullStatus);
        if (status && status->frame > after) {
            break;
        }

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    const CullStatus *result = ecs_get(world, view, CullStatus);
    test_assert(result != NULL);
    test_assert(result->frame > after);

    return result;
}

void DrawRange_beyond_far_not_drawn(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.far_ = 8});
    unsigned char *px = range_capture(world, "range_far.png");
    test_int(range_count(px, 7), 0);
    test_int(range_count(px, 0), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_inside_range_drawn(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.near_ = 5, .far_ = 20, .fade = 2});
    unsigned char *px = range_capture(world, "range_inside.png");
    test_int(range_count(px, 7), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_nearer_than_near_not_drawn(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.near_ = 15, .fade = 2});
    unsigned char *px = range_capture(world, "range_near.png");
    test_int(range_count(px, 7), 0);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_fade_out_band_draws_share(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.far_ = 9, .fade = 2});
    unsigned char *px = range_capture(world, "range_fade_out.png");
    int32_t lit = range_count(px, 7);
    int32_t dark = range_count(px, 0);
    test_int(lit + dark, RANGE_SIZE * RANGE_SIZE);
    test_assert(lit > RANGE_SIZE * RANGE_SIZE * 15 / 100);
    test_assert(lit < RANGE_SIZE * RANGE_SIZE * 35 / 100);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_fade_in_band_draws_share(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.near_ = 9, .fade = 2});
    unsigned char *px = range_capture(world, "range_fade_in.png");
    int32_t lit = range_count(px, 7);
    test_int(lit + range_count(px, 0), RANGE_SIZE * RANGE_SIZE);
    test_assert(lit > RANGE_SIZE * RANGE_SIZE * 65 / 100);
    test_assert(lit < RANGE_SIZE * RANGE_SIZE * 85 / 100);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_fade_is_stable(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    ecs_set(world, wall, DrawRange, {.far_ = 10, .fade = 2});
    unsigned char *a = range_capture(world, "range_stable_a.png");
    unsigned char *b = range_capture(world, "range_stable_b.png");
    test_assert(!memcmp(a, b, RANGE_SIZE * RANGE_SIZE * 4));
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

void DrawRange_complementary_cover_once(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t near = range_wall(world, -10, (Rgb){255, 0, 0});
    ecs_set(world, near, BlendMode, {Additive});
    ecs_set(world, near, DrawRange, {.far_ = 11, .fade = 2});
    ecs_entity_t far = range_wall(world, -10, (Rgb){0, 0, 255});
    ecs_set(world, far, BlendMode, {Additive});
    ecs_set(world, far, DrawRange, {.near_ = 11, .fade = 2});
    unsigned char *px = range_capture(world, "range_complement.png");
    int32_t red = range_count(px, 1);
    int32_t blue = range_count(px, 4);
    test_int(range_count(px, 5), 0);
    test_int(range_count(px, 0), 0);
    test_int(red + blue, RANGE_SIZE * RANGE_SIZE);
    test_assert(red > RANGE_SIZE * RANGE_SIZE * 65 / 100);
    test_assert(red < RANGE_SIZE * RANGE_SIZE * 85 / 100);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_complementary_opaque_no_holes(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t near = range_wall(world, -10, (Rgb){255, 0, 0});
    ecs_set(world, near, DrawRange, {.far_ = 10, .fade = 3});
    ecs_entity_t far = range_wall(world, -10, (Rgb){0, 0, 255});
    ecs_set(world, far, DrawRange, {.near_ = 10, .fade = 3});
    unsigned char *px = range_capture(world, "range_complement_opaque.png");
    int32_t red = range_count(px, 1);
    int32_t blue = range_count(px, 4);
    test_int(range_count(px, 0), 0);
    test_int(red + blue, RANGE_SIZE * RANGE_SIZE);
    test_assert(red > RANGE_SIZE * RANGE_SIZE * 35 / 100);
    test_assert(blue > RANGE_SIZE * RANGE_SIZE * 35 / 100);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_inherited_from_prefab(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {2, 2, 0.1f});
    ecs_set(world, prefab, Rgb, {255, 255, 255});
    ecs_set(world, prefab, DrawRange, {.far_ = 6});
    ecs_entity_t near = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, near, Position, {-1.5f, 0, -5});
    ecs_entity_t far = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, far, Position, {1.5f, 0, -8});
    unsigned char *px = range_capture(world, "range_prefab.png");
    /* x = -1.5 at 5 units lands on column 32 - 1.5 / (5 * tan 30) * 32, about 15 */
    test_assert(px[(32 * RANGE_SIZE + 15) * 4] > 200);
    /* x = 1.5 at 8 units lands on column 32 + 1.5 / (8 * tan 30) * 32, about 42 */
    test_assert(px[(32 * RANGE_SIZE + 42) * 4] < 30);
    stbi_image_free(px);

    ecs_set(world, prefab, DrawRange, {.far_ = 20});
    px = range_capture(world, "range_prefab_set.png");
    test_assert(px[(32 * RANGE_SIZE + 15) * 4] > 200);
    test_assert(px[(32 * RANGE_SIZE + 42) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_inherited_from_parent(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, -10});
    ecs_set(world, parent, DrawRange, {.far_ = 8});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Box, {20, 20, 0.1f});
    ecs_set(world, child, Rgb, {255, 255, 255});
    unsigned char *px = range_capture(world, "range_parent.png");
    test_int(range_count(px, 7), 0);
    stbi_image_free(px);

    ecs_remove(world, parent, DrawRange);
    px = range_capture(world, "range_parent_removed.png");
    test_int(range_count(px, 7), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_own_range_overrides_parent(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, -10});
    ecs_set(world, parent, DrawRange, {.far_ = 8});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Box, {20, 20, 0.1f});
    ecs_set(world, child, Rgb, {255, 255, 255});
    ecs_set(world, child, DrawRange, {.far_ = 30});
    unsigned char *px = range_capture(world, "range_override.png");
    test_int(range_count(px, 7), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_set_and_removed_after_drawn(void) {
    ecs_world_t *world = range_world();
    range_view(world, (LodSettings){0});
    ecs_entity_t wall = range_wall(world, -10, (Rgb){255, 255, 255});
    unsigned char *px = range_capture(world, "range_before.png");
    test_int(range_count(px, 7), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);

    ecs_set(world, wall, DrawRange, {.far_ = 5});
    px = range_capture(world, "range_set.png");
    test_int(range_count(px, 7), 0);
    stbi_image_free(px);

    ecs_remove(world, wall, DrawRange);
    px = range_capture(world, "range_removed.png");
    test_int(range_count(px, 7), RANGE_SIZE * RANGE_SIZE);
    stbi_image_free(px);
    ecs_fini(world);
}

void DrawRange_stats_count_range_culled(void) {
    ecs_world_t *world = range_world();
    ecs_entity_t view = range_view(world, (LodSettings){0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = RANGE_SIZE, .height = RANGE_SIZE,
        .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, -5.0f - (float)i * 4.0f});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_set(world, e, DrawRange, {.far_ = 15});
    }

    /* The boxes at 5, 9 and 13 units draw, the 7 from 17 units on are out of range */
    const CullStatus *status = range_status(world, view, -1);
    status = range_status(world, view, status->frame);
    test_int(status->instances, 10);
    test_int(status->visible, 3);
    test_int(status->range_culled, 7);
    test_int(status->frustum_culled, 0);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 10);
    test_int(stats.visible, 3);
    ecs_fini(world);
}

void DrawRange_out_of_frustum_counts_as_range_culled(void) {
    ecs_world_t *world = range_world();
    ecs_entity_t view = range_view(world, (LodSettings){0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = RANGE_SIZE, .height = RANGE_SIZE,
        .resolution_scale = 1});
    ecs_entity_t behind = ecs_new(world);
    ecs_set(world, behind, Position, {0, 0, 30});
    ecs_set(world, behind, Box, {1, 1, 1});
    ecs_set(world, behind, DrawRange, {.far_ = 10});
    ecs_entity_t side = ecs_new(world);
    ecs_set(world, side, Position, {100, 0, -5});
    ecs_set(world, side, Box, {1, 1, 1});
    const CullStatus *status = range_status(world, view, -1);
    status = range_status(world, view, status->frame);
    test_int(status->instances, 2);
    test_int(status->visible, 0);
    test_int(status->range_culled, 1);
    test_int(status->frustum_culled, 1);
    ecs_fini(world);
}

void DrawRange_with_levels_of_detail(void) {
    ecs_world_t *world = range_world();
    ecs_entity_t view = range_view(world,
        (LodSettings){.levels = 3, .distances = {0, 10, 30}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = RANGE_SIZE, .height = RANGE_SIZE,
        .resolution_scale = 1});
    float z[3] = { -5, -40, -80 };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, z[i]});
        ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_set(world, e, DrawRange, {.far_ = 60});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 3);
    test_int(stats.visible, 2);
    test_int(stats.levels[0], 1);
    test_int(stats.levels[1], 0);
    test_int(stats.levels[2], 1);
    ecs_fini(world);
}

/** Create a world lit straight down by a sun, without ambient light, with a white floor on
 * y = 0 seen from 10 units above, and a slab 5 units above x = -3 out of the image whose shadow
 * falls on column 15; column 48 stays lit. The slab is 5.83 units from the camera.
 *
 * @param range The DrawRange of the slab
 * @param file Image file in the temporary folder
 * @return RGBA pixels, freed with stbi_image_free
 */
static unsigned char* range_shadow(
    DrawRange range,
    const char *file)
{
    ecs_world_t *world = range_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .culling.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_set_ptr(world, caster, DrawRange, &range);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    unsigned char *px = range_capture(world, file);
    ecs_fini(world);

    return px;
}

void DrawRange_beyond_far_casts_no_shadow(void) {
    unsigned char *px = range_shadow((DrawRange){.far_ = 4}, "range_shadow_far.png");
    unsigned char *under = &px[(32 * RANGE_SIZE + 15) * 4];
    unsigned char *open = &px[(32 * RANGE_SIZE + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);
    stbi_image_free(px);
}

void DrawRange_inside_range_casts_shadow(void) {
    unsigned char *px = range_shadow((DrawRange){.far_ = 8}, "range_shadow_inside.png");
    unsigned char *under = &px[(32 * RANGE_SIZE + 15) * 4];
    unsigned char *open = &px[(32 * RANGE_SIZE + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    stbi_image_free(px);
}

void DrawRange_fade_band_dims_shadow(void) {
    /* 5.83 units is the middle of the fade out of a far of 5.83: half the texels cast */
    unsigned char *px = range_shadow((DrawRange){.far_ = 5.83f, .fade = 2},
        "range_shadow_fade.png");
    unsigned char *under = &px[(32 * RANGE_SIZE + 15) * 4];
    unsigned char *open = &px[(32 * RANGE_SIZE + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] > 30);
    test_assert(under[0] < open[0] - 30);
    stbi_image_free(px);
}
