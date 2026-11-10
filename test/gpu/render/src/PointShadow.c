/**
 * @file PointShadow.c
 * @brief Tests for the shadows of point lights through octahedral tiles of the shadow atlas,
 * with the GPU backend.
 *
 * Most cases look down from 4 units above a white floor into a 64x64 image with a 90 degree
 * camera, 8 pixels a unit; a point light 10 units above the origin and a 1x1 slab 6 units up,
 * behind the camera, shadow the floor from x = -1.25 to 1.25, where the octant edges meet.
 *
 * The room cases put the light in the middle of a closed 8 unit cube and look at each of its 6
 * inner faces with an orthographic camera that sees the whole face, 8 pixels per unit.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define POINT_SIZE (64)

/** Red channel at most of a shadowed pixel */
#define POINT_DARK (40)

/** Red channel at least of a lit pixel */
#define POINT_LIT (100)

/** Half edge of the room of the room cases */
#define POINT_ROOM (4.0f)

/** Largest channel difference a pixel of two renders may have before it counts as off, as the
 * visual comparison of tools/integration.py */
#define POINT_PIXEL_OFF (16)

/** Largest channel difference over the whole image the visual comparison passes */
#define POINT_MAX_DELTA (24)

/** Pixels off the visual comparison passes, 200 of its 640x360 frame scaled to 64x64 */
#define POINT_OFF_PIXELS (4)

/** Create a headless world on the main thread without ambient light.
 *
 * @param floor Whether to add a white rough floor on y = 0
 * @return The world
 */
static ecs_world_t* point_world(
    bool floor)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    if (floor) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, -0.1f, 0});
        ecs_set(world, e, Box, {40, 0.2f, 40});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_set(world, e, PbrMaterial, {.roughness = 1});
    }

    return world;
}

/** Create a camera looking straight down from a height and a view of it.
 *
 * @param world The world
 * @param height Height of the camera
 * @param shadows Whether the view renders shadows
 * @param camera Receives the camera, NULL for none
 * @return The view
 */
static ecs_entity_t point_view(
    ecs_world_t *world,
    float height,
    bool shadows,
    ecs_entity_t *camera)
{
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Position, {0, height, 0});
    ecs_set(world, c, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, c, Camera, {.fov = 90, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = c;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = shadows;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});
    if (camera) {
        *camera = c;
    }

    return view;
}

/** Create a point light with shadows.
 *
 * @param world The world
 * @param position World position of the light
 * @param range Range of the light
 * @param intensity Intensity of the light
 * @return The light
 */
static ecs_entity_t point_light(
    ecs_world_t *world,
    vec3 position,
    float range,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, PointLight, {
        .intensity = intensity, .range = range, .bake = LightBakeModeMixed,
        .shadows = true, .shadow_resolution = 512
    });

    return e;
}

/** Create a box.
 *
 * @param world The world
 * @param position World position of its center
 * @param size Its edges
 * @return The box
 */
static ecs_entity_t point_box(
    ecs_world_t *world,
    vec3 position,
    vec3 size)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size[0], size[1], size[2]});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.roughness = 1});

    return e;
}

/** Create the scene of most cases: the 90 degree camera 4 units above the floor, a point light
 * 10 units above the origin and the slab 6 units above the floor.
 *
 * @param light Receives the light
 * @param slab Receives the slab
 * @return The world
 */
static ecs_world_t* point_scene(
    ecs_entity_t *light,
    ecs_entity_t *slab)
{
    ecs_world_t *world = point_world(true);
    point_view(world, 4, true, NULL);
    *light = point_light(world, (vec3){0, 10, 0}, 20, 60 * GLM_PIf);
    *slab = point_box(world, (vec3){0, 6, 0}, (vec3){1, 0.1f, 1});

    return world;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a POINT_SIZE square image, freed with stbi_image_free
 */
static unsigned char* point_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = POINT_SIZE, .height = POINT_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, POINT_SIZE);
    test_int(h, POINT_SIZE);
    ecs_os_free(path);

    return px;
}

/** Run frames so the shaders and pipelines the scene needs exist.
 *
 * @param world The world
 */
static void point_warm(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 5; i ++) {
        stbi_image_free(point_capture(world, "point_warm.png"));
    }
}

/** Get the red channel of a pixel.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @return The channel
 */
static int32_t point_red(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return px[(y * POINT_SIZE + x) * 4];
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void point_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** Create the closed room of the room cases with its light in the middle, and a small cube
 * toward each octant at (+-1.2, +-1.2, +-1.2): each shadows the corner of the room in its
 * octant, the region within 1.6 units of the corner on each of the 3 faces that meet there.
 *
 * @param casters Whether to add the cubes
 * @param camera Receives the orthographic camera of the view
 * @return The world
 */
static ecs_world_t* point_room(
    bool casters,
    ecs_entity_t *camera)
{
    ecs_world_t *world = point_world(false);
    float r = POINT_ROOM, t = 0.2f, e = 2.0f * POINT_ROOM + 2.0f * t;
    int32_t i;
    point_box(world, (vec3){0, -r - 0.5f * t, 0}, (vec3){e, t, e});
    point_box(world, (vec3){0, r + 0.5f * t, 0}, (vec3){e, t, e});
    point_box(world, (vec3){-r - 0.5f * t, 0, 0}, (vec3){t, e, e});
    point_box(world, (vec3){r + 0.5f * t, 0, 0}, (vec3){t, e, e});
    point_box(world, (vec3){0, 0, -r - 0.5f * t}, (vec3){e, e, t});
    point_box(world, (vec3){0, 0, r + 0.5f * t}, (vec3){e, e, t});
    for (i = 0; casters && i < 8; i ++) {
        point_box(world, (vec3){
            (i & 1) ? -1.2f : 1.2f, (i & 2) ? -1.2f : 1.2f, (i & 4) ? -1.2f : 1.2f
        }, (vec3){0.6f, 0.6f, 0.6f});
    }

    point_light(world, (vec3){0, 0, 0}, 12, 30 * GLM_PIf);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Camera, {.ortho = true, .ortho_height = 2.0f * POINT_ROOM,
        .near_ = 0.05f, .far_ = 20, .aspect_ratio = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = *camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});

    return world;
}

/** Place the orthographic camera of the room just inside a face, looking at the opposite one.
 *
 * @param world The world
 * @param camera The camera
 * @param face 0 to 5: looking down, up, toward -x, +x, -z and +z
 */
static void point_room_face(
    ecs_world_t *world,
    ecs_entity_t camera,
    int32_t face)
{
    float in = POINT_ROOM - 0.1f, half = (float)GLM_PI_2;
    static const float rotations[6][3] = {
        {-1.0f, 0, 0}, {1.0f, 0, 0}, {0, 1.0f, 0}, {0, -1.0f, 0}, {0, 0, 0}, {0, 2.0f, 0}
    };
    static const float positions[6][3] = {
        {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
    };
    ecs_set(world, camera, Position, {positions[face][0] * in, positions[face][1] * in,
        positions[face][2] * in});
    ecs_set(world, camera, Rotation, {rotations[face][0] * half, rotations[face][1] * half,
        rotations[face][2] * half});
}

void PointShadow_shadows_every_octant(void) {
    // lighting/shadows
    ecs_entity_t camera;
    ecs_world_t *world = point_room(true, &camera);
    int32_t face, k;
    static const int32_t corners[4][2] = {{5, 5}, {58, 5}, {5, 58}, {58, 58}};
    static const int32_t lit[5][2] = {{32, 32}, {32, 5}, {5, 32}, {58, 32}, {32, 58}};
    for (face = 0; face < 6; face ++) {
        char file[32];
        snprintf(file, sizeof(file), "point_octant_%d.png", face);
        point_room_face(world, camera, face);
        unsigned char *px = point_capture(world, file);
        for (k = 0; k < 4; k ++) {
            test_assert(point_red(px, corners[k][0], corners[k][1]) < POINT_DARK);
        }

        for (k = 0; k < 5; k ++) {
            test_assert(point_red(px, lit[k][0], lit[k][1]) > POINT_LIT);
        }

        stbi_image_free(px);
    }

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 0);
    point_no_errors(world);
    ecs_fini(world);
}

/** Check that no pixel of a line is below 90% of its neighbours on that line.
 *
 * @param px The pixels
 * @param x Column of the first pixel
 * @param y Row of the first pixel
 * @param dx Step along the line in columns
 * @param dy Step along the line in rows
 * @param count Pixels of the line
 */
static void point_line_even(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t dx,
    int32_t dy,
    int32_t count)
{
    int32_t i;
    for (i = 1; i < count - 1; i ++) {
        int32_t c = point_red(px, x + i * dx, y + i * dy);
        int32_t a = point_red(px, x + (i - 1) * dx, y + (i - 1) * dy);
        int32_t b = point_red(px, x + (i + 1) * dx, y + (i + 1) * dy);
        test_assert(c * 10 >= a * 9);
        test_assert(c * 10 >= b * 9);
    }
}

/* Rows and columns that cross the octant edges x = 0 and z = 0 under the light, over the
 * middle of the image where the falloff between two pixels stays small */
void PointShadow_octant_seams_lit_evenly(void) {
    // lighting/shadows
    ecs_world_t *world = point_world(true);
    point_view(world, 4, true, NULL);
    point_light(world, (vec3){0, 2.5f, 0}, 10, 4 * GLM_PIf);
    point_warm(world);
    unsigned char *px = point_capture(world, "point_seams.png");
    static const int32_t lines[6] = {12, 20, 28, 36, 44, 52};
    int32_t i;
    test_assert(point_red(px, 32, 32) > POINT_LIT);
    for (i = 0; i < 6; i ++) {
        point_line_even(px, 12, lines[i], 1, 0, 41);
        point_line_even(px, lines[i], 12, 0, 1, 41);
    }

    point_line_even(px, 12, 32, 1, 0, 41);
    point_line_even(px, 32, 12, 0, 1, 41);
    point_line_even(px, 12, 31, 1, 0, 41);
    point_line_even(px, 31, 12, 0, 1, 41);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    point_no_errors(world);
    ecs_fini(world);
}

void PointShadow_core_matches_clip_distances(void) {
    // lighting/shadows
    ecs_entity_t camera;
    ecs_world_t *world = point_room(true, &camera);
    box_render_gpu_stats_t stats;
    point_room_face(world, camera, 0);
    point_warm(world);
    test_assert(box_render_gpu_stats(world, &stats));
    if (!stats.clip_distances) {
        printf("PointShadow.core_matches_clip_distances: skipped, the device has no clip "
            "distances\n");
        goto done;
    }

    ecs_set(world, camera, Position, {0, 1.5f, POINT_ROOM - 0.1f});
    ecs_set(world, camera, Rotation, {-0.6f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 100, .near_ = 0.05f, .far_ = 20});
    unsigned char *clip = point_capture(world, "point_clip.png");
    test_bool(box_render_gpu_shadow_core(world, true), true);
    point_warm(world);
    unsigned char *core = point_capture(world, "point_core.png");
    int32_t i, c, off = 0, max = 0;
    for (i = 0; i < POINT_SIZE * POINT_SIZE; i ++) {
        int32_t worst = 0;
        for (c = 0; c < 3; c ++) {
            int32_t d = abs((int32_t)clip[i * 4 + c] - (int32_t)core[i * 4 + c]);
            worst = d > worst ? d : worst;
        }

        max = worst > max ? worst : max;
        off += worst > POINT_PIXEL_OFF;
    }

    test_assert(max <= POINT_MAX_DELTA || off <= POINT_OFF_PIXELS);
    stbi_image_free(clip);
    stbi_image_free(core);
    test_bool(box_render_gpu_shadow_core(world, false), true);
    point_no_errors(world);
done:
    ecs_fini(world);
}

/* The tile tests the floor and both boxes and keeps the floor and the box inside the range;
 * the tile renders every frame without the shadow cache */
void PointShadow_range_culls_casters(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = point_scene(&light, &slab);
    test_bool(box_render_shadow_cache(world, false), true);
    point_box(world, (vec3){40, 6, 0}, (vec3){1, 0.1f, 1});
    unsigned char *px = point_capture(world, "point_cull.png");
    test_assert(point_red(px, 32, 32) < POINT_DARK);
    test_assert(point_red(px, 56, 32) > POINT_LIT);
    stbi_image_free(px);

    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 1);

    box_render_cull_stats_t tile;
    test_bool(box_render_gpu_cull_tile(world, 0, &tile), true);
    test_int(tile.tested, 24);
    test_int(tile.visible, 8);
    test_bool(box_render_gpu_cull_tile(world, 1, &tile), false);
    point_no_errors(world);
    ecs_fini(world);
}

void PointShadow_shadows_false_unshadowed(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = point_scene(&light, &slab);
    unsigned char *px = point_capture(world, "point_true.png");
    test_assert(point_red(px, 32, 32) < POINT_DARK);
    test_assert(point_red(px, 30, 34) < POINT_DARK);
    test_assert(point_red(px, 56, 32) > POINT_LIT);
    test_assert(point_red(px, 8, 32) > POINT_LIT);
    test_assert(point_red(px, 32, 8) > POINT_LIT);
    stbi_image_free(px);

    PointLight *p = ecs_get_mut(world, light, PointLight);
    p->shadows = false;
    ecs_modified(world, light, PointLight);
    px = point_capture(world, "point_false.png");
    test_assert(point_red(px, 32, 32) > POINT_LIT);
    test_assert(point_red(px, 56, 32) > POINT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 0);
    point_no_errors(world);
    ecs_fini(world);
}

void PointShadow_view_shadows_disabled_unshadowed(void) {
    // lighting/shadows
    ecs_world_t *world = point_world(true);
    point_view(world, 4, false, NULL);
    point_light(world, (vec3){0, 10, 0}, 20, 60 * GLM_PIf);
    point_box(world, (vec3){0, 6, 0}, (vec3){1, 0.1f, 1});
    unsigned char *px = point_capture(world, "point_view_off.png");
    test_assert(point_red(px, 32, 32) > POINT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shadow_tiles, 0);
    point_no_errors(world);
    ecs_fini(world);
}

void PointShadow_baked_light_has_no_tile(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = point_scene(&light, &slab);
    ecs_singleton_set(world, LightBake, {.probe_spacing = 4, .bounces = 0, .capture_size = 8,
        .budget_ms = 2, .bounds_padding = 1, .compute = true});
    PointLight *p = ecs_get_mut(world, light, PointLight);
    p->bake = LightBakeModeBaked;
    ecs_modified(world, light, PointLight);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        stbi_image_free(point_capture(world, "point_baked.png"));
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shadow_tiles, 0);
    point_no_errors(world);
    ecs_fini(world);
}

/* One caster in one batch draws once per octant of the tile: 8 shadow draws per frame, with
 * clip distances and on the core path alike */
/* Without the shadow cache, so the tile renders in every frame */
void PointShadow_draws_per_light(void) {
    // lighting/shadows
    ecs_world_t *world = point_world(false);
    test_bool(box_render_shadow_cache(world, false), true);
    point_view(world, 4, true, NULL);
    point_light(world, (vec3){0, 10, 0}, 20, 60 * GLM_PIf);
    point_box(world, (vec3){0, 6, 0}, (vec3){1, 0.1f, 1});
    point_warm(world);

    int32_t round;
    for (round = 0; round < 2; round ++) {
        box_render_gpu_stats_t before, after;
        test_bool(box_render_gpu_shadow_core(world, round == 1), true);
        point_warm(world);
        test_assert(box_render_gpu_stats(world, &before));
        ecs_progress(world, 0);
        box_render_sync(world);
        test_assert(box_render_gpu_stats(world, &after));
        test_int(after.shadow_tiles - before.shadow_tiles, 1);
        test_int(after.shadow_draws - before.shadow_draws, BOX_RENDER_SHADOW_OCTANTS);
    }

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    point_no_errors(world);
    ecs_fini(world);
}

/* A lost device recreates the atlas and the octant pipelines, and the tile shadows again */
void PointShadow_device_loss_recreates_tile(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = point_scene(&light, &slab);
    unsigned char *px = point_capture(world, "point_loss_a.png");
    test_assert(point_red(px, 32, 32) < POINT_DARK);
    stbi_image_free(px);

    box_render_gpu_lose_device(world);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    px = point_capture(world, "point_loss_b.png");
    test_assert(point_red(px, 32, 32) < POINT_DARK);
    test_assert(point_red(px, 56, 32) > POINT_LIT);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);
    ecs_fini(world);
}
