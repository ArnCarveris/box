/**
 * @file Grass.c
 * @brief Tests for the grass blades the GPU places and draws around the camera (box.grass,
 * gpu_grass.c): the blades stand on the plane or the terrain, take their level of detail from
 * their distance, thin out and widen further away, stay where they are as the camera moves and
 * across a shift of the WorldOrigin, follow the mask of the terrain, gather in clumps that share
 * their facing, never draw thinner than about a pixel, vary in color over the world, render lit
 * and show in the GPU stats.
 *
 * The scenes are test/assets/grass/plane.flecs and test/assets/grass/terrain.flecs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "terrain.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Seconds of a frame */
#define GR_DELTA (1.0f / 60.0f)

/** Width and height of the captured image, large enough that the blades near the camera are
 * wider than a pixel and none of them thin out for it */
#define GR_WIDTH (640)
#define GR_HEIGHT (480)

/** Width and height of a small image, where the blades near the camera are thinner than a
 * pixel */
#define GR_SMALL_WIDTH (64)
#define GR_SMALL_HEIGHT (48)

/** Frames a test waits at most for the tiles of the terrain */
#define GR_FRAMES (360)

/** Largest distance between a blade and its match in another frame, in meters */
#define GR_MATCH_EPS (1e-3f)

/** Quanta per meter of the map that matches blades */
#define GR_QUANTA (256.0)

/** Create a headless world that renders on the main thread into a capture surface and run a
 * scene of the tests.
 *
 * @param scene The scene file
 * @return The world
 */
static ecs_world_t* gr_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t capture = ecs_entity(world, { .name = "capture" });
    ecs_set(world, capture, Surface, {.width = GR_WIDTH, .height = GR_HEIGHT,
        .resolution_scale = 1});
    char *path = box_app_locate(scene);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);

    return world;
}

/** Run frames and wait for the backend after each.
 *
 * @param world The world
 * @param count Frames
 */
static void gr_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, GR_DELTA);
        box_render_sync(world);
    }
}

/** Look up an entity of the scene.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t gr_lookup(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the local position of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the position
 */
static void gr_position(
    ecs_world_t *world,
    ecs_entity_t e,
    float out[3])
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    test_assert(t != NULL);
    out[0] = t->m[3][0];
    out[1] = t->m[3][1];
    out[2] = t->m[3][2];
}

/** Read back the blades of a Grass.
 *
 * @param world The world
 * @param grass The Grass entity
 * @param out Receives box_render_grass_blade_t, initialized by the caller
 */
static void gr_blades(
    ecs_world_t *world,
    ecs_entity_t grass,
    ecs_vec_t *out)
{
    ecs_vec_clear(out);
    test_bool(box_render_gpu_grass_blades(world, grass, out), true);
}

/** Get the horizontal distance of a blade from a point.
 *
 * @param b The blade
 * @param p The point
 * @return The distance in x and z
 */
static float gr_flat_distance(
    const box_render_grass_blade_t *b,
    const float p[3])
{
    float dx = b->position[0] - p[0], dz = b->position[2] - p[2];

    return sqrtf(dx * dx + dz * dz);
}

/** Get the key of a quantum of the map that matches blades.
 *
 * @param x Absolute x
 * @param z Absolute z
 * @return The key
 */
static uint64_t gr_key(
    int64_t x,
    int64_t z)
{
    return ((uint64_t)(uint32_t)(int32_t)x << 32) | (uint64_t)(uint32_t)(int32_t)z;
}

/** Map the blades of a frame by the quantum of their absolute x and z.
 *
 * @param blades The blades
 * @param origin Absolute x and z of the local origin of the frame
 * @param out Receives the index plus one of a blade per quantum
 */
static void gr_map(
    const ecs_vec_t *blades,
    const double origin[2],
    ecs_map_t *out)
{
    int32_t i;
    ecs_map_init(out, NULL);
    for (i = 0; i < ecs_vec_count(blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(blades, box_render_grass_blade_t, i);
        int64_t x = llround(((double)b->position[0] + origin[0]) * GR_QUANTA);
        int64_t z = llround(((double)b->position[2] + origin[1]) * GR_QUANTA);
        *ecs_map_ensure(out, gr_key(x, z)) = (ecs_map_val_t)(i + 1);
    }
}

/** Find the blade of another frame at the absolute place of a blade, with the same size.
 *
 * @param blades The blades of the other frame
 * @param map Their map (gr_map)
 * @param origin Absolute x and z of the local origin of the other frame
 * @param b The blade
 * @param at Absolute x and z of the local origin of the frame of the blade
 * @return Whether the other frame has it
 */
static bool gr_match(
    const ecs_vec_t *blades,
    ecs_map_t *map,
    const double origin[2],
    const box_render_grass_blade_t *b,
    const double at[2])
{
    double ax = (double)b->position[0] + at[0], az = (double)b->position[2] + at[1];
    int64_t qx = llround(ax * GR_QUANTA), qz = llround(az * GR_QUANTA), dx, dz;
    bool result = false;
    for (dx = -1; dx <= 1 && !result; dx ++) {
        for (dz = -1; dz <= 1 && !result; dz ++) {
            ecs_map_val_t *v = ecs_map_get(map, gr_key(qx + dx, qz + dz));
            if (!v) {
                continue;
            }

            const box_render_grass_blade_t *o = ecs_vec_get_t(blades,
                box_render_grass_blade_t, (int32_t)*v - 1);
            result = fabs((double)o->position[0] + origin[0] - ax) < GR_MATCH_EPS &&
                fabs((double)o->position[2] + origin[1] - az) < GR_MATCH_EPS &&
                fabsf(o->position[1] - b->position[1]) < GR_MATCH_EPS &&
                fabsf(o->height - b->height) < 1e-4f &&
                fabsf(o->yaw - b->yaw) < 1e-4f;
        }
    }

    return result;
}

/** Count the blades of level 0 of a frame in a rectangle ahead of a camera and those of them
 * another frame has at the same absolute place.
 *
 * @param a The blades of the frame
 * @param at Absolute x and z of the local origin of the frame
 * @param b The blades of the other frame
 * @param origin Absolute x and z of the local origin of the other frame
 * @param x0 Smallest absolute x of the rectangle
 * @param x1 Largest absolute x
 * @param z0 Smallest absolute z
 * @param z1 Largest absolute z
 * @param found Receives the blades the other frame has
 * @return The blades in the rectangle
 */
static int32_t gr_kept(
    const ecs_vec_t *a,
    const double at[2],
    const ecs_vec_t *b,
    const double origin[2],
    double x0,
    double x1,
    double z0,
    double z1,
    int32_t *found)
{
    ecs_map_t map;
    int32_t i, result = 0;
    *found = 0;
    gr_map(b, origin, &map);
    for (i = 0; i < ecs_vec_count(a); i ++) {
        const box_render_grass_blade_t *blade = ecs_vec_get_t(a, box_render_grass_blade_t, i);
        double x = (double)blade->position[0] + at[0], z = (double)blade->position[2] + at[1];
        if (blade->level != 0 || x < x0 || x > x1 || z < z0 || z > z1) {
            continue;
        }

        result ++;
        *found += gr_match(b, &map, origin, blade, at);
    }

    ecs_map_fini(&map);

    return result;
}

/** Check that the backend logged no error.
 *
 * @param world The world
 */
static void gr_expect_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);
}

void Grass_plane_places_blades(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t blades;
    float eye[3];
    int32_t i, levels[BOX_GRASS_LODS] = {0};
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    gr_frames(world, 10);
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &blades);
    test_assert(ecs_vec_count(&blades) > 1000);
    for (i = 0; i < ecs_vec_count(&blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&blades,
            box_render_grass_blade_t, i);
        float d[3] = {
            b->position[0] - eye[0], b->position[1] - eye[1], b->position[2] - eye[2]
        };
        float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        test_assert(fabsf(b->position[1] - 0.5f) < 1e-4f);
        test_assert(b->level >= 0 && b->level < BOX_GRASS_LODS);
        test_assert(b->height > 0 && b->height <= 0.6f + 1e-4f);
        test_assert(b->width > 0);
        test_assert(b->position[2] < eye[2] + 1.0f);
        levels[b->level] ++;
        if (b->level == 0) {
            test_assert(distance < 12.0f + 1e-3f);
        } else if (b->level == 1) {
            test_assert(distance >= 12.0f - 1e-3f && distance < 40.0f + 1e-3f);
        } else {
            test_assert(distance >= 40.0f - 1e-3f && distance < 120.0f + 1e-3f);
        }
    }

    printf("levels %d %d %d\n", levels[0], levels[1], levels[2]);
    test_assert(levels[0] > 0);
    test_assert(levels[1] > 0);
    test_assert(levels[2] > 0);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_thins_with_distance(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t blades;
    float eye[3], near_width = 0, far_width = 0;
    int32_t i, near_count = 0, far_count = 0;
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    gr_frames(world, 10);
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &blades);
    for (i = 0; i < ecs_vec_count(&blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&blades,
            box_render_grass_blade_t, i);
        float d = gr_flat_distance(b, eye);
        if (d >= 4.0f && d < 10.0f) {
            near_count ++;
            near_width += b->width;
        } else if (d >= 60.0f && d < 100.0f) {
            far_count ++;
            far_width += b->width;
        }
    }

    // Blades per square meter of the rings, the share of the rings in view about the same
    double near_density = (double)near_count / (10.0 * 10.0 - 4.0 * 4.0);
    double far_density = (double)far_count / (100.0 * 100.0 - 60.0 * 60.0);
    printf("near %d (%.2f per m2), far %d (%.3f per m2)\n", near_count, near_density,
        far_count, far_density);
    test_assert(near_count > 100);
    test_assert(far_count > 0);
    test_assert(near_density > far_density * 8.0);

    // The blades that stay widen
    test_assert(far_width / (float)far_count > near_width / (float)near_count * 4.0f);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_stays_put_as_camera_moves(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t a, b;
    float eye[3];
    double zero[2] = {0, 0};
    int32_t found, count;
    ecs_vec_init_t(NULL, &a, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &b, box_render_grass_blade_t, 0);
    gr_frames(world, 10);
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &a);

    // Move across the edge of a patch of the grid at x = 0
    ecs_set(world, camera, Position, {eye[0] - 2.37f, eye[1], eye[2] - 0.21f});
    gr_frames(world, 3);
    gr_blades(world, meadow, &b);
    count = gr_kept(&a, zero, &b, zero, eye[0] - 2.0, eye[0] - 0.4, eye[2] - 10.0,
        eye[2] - 5.0, &found);
    printf("kept %d of %d\n", found, count);
    test_assert(count > 50);
    test_assert(found * 100 >= count * 98);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &a, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &b, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_stays_put_across_origin_shift(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t a, b;
    float eye[3];
    int32_t found, count;
    ecs_singleton_set(world, WorldOrigin, {.threshold = 4, .step = 4, .enabled = true});
    ecs_add_id(world, camera, WorldFocus);
    ecs_vec_init_t(NULL, &a, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &b, box_render_grass_blade_t, 0);
    gr_frames(world, 10);
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    double before[2] = { o->x, o->z };
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &a);

    ecs_set(world, camera, Position, {eye[0] + 5.0f, eye[1], eye[2]});
    gr_frames(world, 3);
    o = ecs_singleton_get(world, WorldOrigin);
    double after[2] = { o->x, o->z };
    test_assert(after[0] != before[0]);
    gr_blades(world, meadow, &b);
    count = gr_kept(&a, before, &b, after, before[0] + eye[0] + 1.0,
        before[0] + eye[0] + 4.0, before[1] + eye[2] - 10.0, before[1] + eye[2] - 6.0,
        &found);
    printf("kept %d of %d across the shift\n", found, count);
    test_assert(count > 50);
    test_assert(found * 100 >= count * 98);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &a, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &b, box_render_grass_blade_t);
    ecs_fini(world);
}

/** Run frames until the tiles of the terrain around the focus draw and the heights there are
 * resident for the CPU queries, then a few more.
 *
 * @param world The world
 * @param terrain The terrain
 */
static void gr_terrain_wait(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    int32_t i;
    for (i = 0; i < GR_FRAMES; i ++) {
        const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
        if (status && status->drawn && !status->pending &&
            box_terrain_resident(world, terrain, 0, 0, 64, 64))
        {
            break;
        }

        gr_frames(world, 1);
    }

    test_bool(box_terrain_resident(world, terrain, 0, 0, 64, 64), true);
    gr_frames(world, 3);
}

void Grass_follows_terrain_height(void) {
    ecs_world_t *world = gr_world("test/assets/grass/terrain.flecs");
    ecs_entity_t terrain = gr_lookup(world, "grass_hills");
    ecs_entity_t grass = gr_lookup(world, "grass_unmasked");
    ecs_vec_t blades;
    double origin[3];
    float focus[3] = {32, 0, 32};
    int32_t i, checked = 0;
    test_bool(ecs_has_id(world, terrain, TerrainSampled), true);
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    gr_terrain_wait(world, terrain);
    box_terrain_origin(world, terrain, origin);
    gr_blades(world, grass, &blades);
    test_assert(ecs_vec_count(&blades) > 0);
    for (i = 0; i < ecs_vec_count(&blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&blades,
            box_render_grass_blade_t, i);
        float h = 0;
        if (gr_flat_distance(b, focus) > 24.0f ||
            !box_terrain_height(world, terrain, b->position[0] - origin[0],
                b->position[2] - origin[2], &h))
        {
            continue;
        }

        test_assert(fabsf(b->position[1] - ((float)origin[1] + h)) < 0.05f);
        checked ++;
    }

    printf("checked %d of %d\n", checked, ecs_vec_count(&blades));
    test_assert(checked > 100);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_mask_weighs_density(void) {
    ecs_world_t *world = gr_world("test/assets/grass/terrain.flecs");
    ecs_entity_t terrain = gr_lookup(world, "grass_hills");
    ecs_vec_t quarter, full, unmasked;
    ecs_vec_init_t(NULL, &quarter, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &full, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &unmasked, box_render_grass_blade_t, 0);
    gr_terrain_wait(world, terrain);
    gr_blades(world, gr_lookup(world, "grass_quarter"), &quarter);
    gr_blades(world, gr_lookup(world, "grass_full"), &full);
    gr_blades(world, gr_lookup(world, "grass_unmasked"), &unmasked);
    double q = ecs_vec_count(&quarter), f = ecs_vec_count(&full);
    double u = ecs_vec_count(&unmasked);
    printf("quarter %.0f, full %.0f, unmasked %.0f\n", q, f, u);
    test_assert(f > 1000);
    test_assert(q > f * 0.15 && q < f * 0.35);
    test_assert(u > f * 0.85 && u < f * 1.15);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &quarter, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &full, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &unmasked, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_stats_show_counts_and_time(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    gr_frames(world, 12);

    const GpuStats *s = ecs_singleton_get(world, GpuStats);
    test_bool(s->gpu, true);
    printf("passes %.1f, drawn %.0f of %.0f, levels %.0f %.0f %.0f, %.3f ms\n",
        s->grass.passes, s->grass.drawn, s->grass.generated, s->grass.lod0.drawn,
        s->grass.lod1.drawn, s->grass.lod2.drawn, s->time.grass_ms);
    test_assert(s->grass.passes >= 1);
    test_assert(s->grass.drawn > 0);
    test_assert(s->grass.lod0.drawn > 0);
    test_assert(s->grass.lod1.drawn > 0);
    test_assert(s->grass.lod2.drawn > 0);
    test_assert(s->grass.lod0.generated >= s->grass.lod0.drawn);
    test_assert(s->grass.lod1.generated >= s->grass.lod1.drawn);
    test_assert(s->grass.lod2.generated >= s->grass.lod2.drawn);
    test_assert(s->grass.generated >= s->grass.drawn);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.grass_passes > 0);
    test_assert(stats.grass_reads > 0);
    if (stats.grass_timed) {
        test_assert(stats.grass_ns > 0);
        test_bool(s->time.measured, true);
    }

    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* gr_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    ecs_set(world, e, Surface, {.width = GR_WIDTH, .height = GR_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    gr_frames(world, 1);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, GR_WIDTH);
    test_int(h, GR_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Count the green pixels of the lower half of an image, where the blades near the camera are.
 *
 * @param px The image
 * @param bright Receives the mean of the green channel of those pixels
 * @return The green pixels
 */
static int32_t gr_green(
    const unsigned char *px,
    double *bright)
{
    int32_t x, y, result = 0;
    double sum = 0;
    for (y = GR_HEIGHT / 2; y < GR_HEIGHT; y ++) {
        for (x = 0; x < GR_WIDTH; x ++) {
            const unsigned char *p = &px[(y * GR_WIDTH + x) * 4];
            if (p[1] > p[0] + 8 && p[1] > p[2] + 8) {
                result ++;
                sum += p[1];
            }
        }
    }

    *bright = result ? sum / result : 0;

    return result;
}

void Grass_renders_lit_blades(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    double bright = 0, bare_bright = 0;

    // A dense meadow of wide blades that covers the ground near the camera
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->density = 96;
    g->min_width = 0.05f;
    g->max_width = 0.09f;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 10);
    unsigned char *px = gr_capture(world, "grass_lit.png");
    int32_t green = gr_green(px, &bright);
    stbi_image_free(px);

    ecs_remove(world, meadow, Grass);
    gr_frames(world, 3);
    px = gr_capture(world, "grass_bare.png");
    int32_t bare = gr_green(px, &bare_bright);
    stbi_image_free(px);

    printf("green %d (mean %.1f), bare %d\n", green, bright, bare);
    test_assert(green * 2 > GR_WIDTH * GR_HEIGHT / 2);
    test_assert(bright > 45);
    test_assert(bare * 20 < GR_WIDTH * GR_HEIGHT / 2);
    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Set values of the Grass of an entity.
 *
 * @param world The world
 * @param e The Grass entity
 * @param clump_follow How strongly the blades follow their clump
 * @param variation How much the color varies over the world
 */
static void gr_shape(
    ecs_world_t *world,
    ecs_entity_t e,
    float clump_follow,
    float variation)
{
    Grass *g = ecs_ensure(world, e, Grass);
    g->clump_size = 1.5f;
    g->clump_follow = clump_follow;
    g->variation = variation;
    ecs_modified(world, e, Grass);
}

/** Get the mean cosine of the angle between the facings of close blades of level 0 in a
 * rectangle ahead of the camera.
 *
 * @param blades The blades
 * @param eye The camera
 * @param pairs Receives the pairs of close blades
 * @return The mean cosine
 */
static double gr_facing_agreement(
    const ecs_vec_t *blades,
    const float eye[3],
    int32_t *pairs)
{
    ecs_vec_t near_;
    double sum = 0;
    int32_t i, j, count;
    *pairs = 0;
    ecs_vec_init_t(NULL, &near_, box_render_grass_blade_t, 0);
    for (i = 0; i < ecs_vec_count(blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(blades, box_render_grass_blade_t, i);
        float dx = b->position[0] - eye[0], dz = b->position[2] - eye[2];
        if (b->level == 0 && dx > -2.0f && dx < 2.0f && dz > -8.0f && dz < -3.0f) {
            *ecs_vec_append_t(NULL, &near_, box_render_grass_blade_t) = *b;
        }
    }

    count = ecs_vec_count(&near_);
    for (i = 0; i < count; i ++) {
        const box_render_grass_blade_t *a = ecs_vec_get_t(&near_, box_render_grass_blade_t, i);
        for (j = i + 1; j < count; j ++) {
            const box_render_grass_blade_t *b = ecs_vec_get_t(&near_,
                box_render_grass_blade_t, j);
            if (gr_flat_distance(a, b->position) < 0.2f) {
                sum += cos((double)a->yaw - (double)b->yaw);
                (*pairs) ++;
            }
        }
    }

    ecs_vec_fini_t(NULL, &near_, box_render_grass_blade_t);

    return *pairs ? sum / *pairs : 0;
}

void Grass_clumps_share_facing(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t blades;
    float eye[3];
    int32_t pairs_free, pairs_clumped;
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    gr_position(world, camera, eye);

    gr_shape(world, meadow, 0, 0);
    gr_frames(world, 10);
    gr_blades(world, meadow, &blades);
    double free_ = gr_facing_agreement(&blades, eye, &pairs_free);

    gr_shape(world, meadow, 1, 0);
    gr_frames(world, 3);
    gr_blades(world, meadow, &blades);
    double clumped = gr_facing_agreement(&blades, eye, &pairs_clumped);

    printf("facing agreement %.3f of %d pairs free, %.3f of %d pairs clumped\n", free_,
        pairs_free, clumped, pairs_clumped);
    test_assert(pairs_free > 50);
    test_assert(pairs_clumped > 50);
    test_assert(fabs(free_) < 0.25);
    test_assert(clumped > 0.5);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

/** Count the blades of level 0 of a Grass.
 *
 * @param blades The blades
 * @return Those of level 0
 */
static int32_t gr_level0(
    const ecs_vec_t *blades)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(blades); i ++) {
        result += ecs_vec_get_t(blades, box_render_grass_blade_t, i)->level == 0;
    }

    return result;
}

void Grass_never_thinner_than_a_pixel(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t blades;
    float eye[3];
    int32_t i, large, small;
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    gr_frames(world, 10);
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &blades);
    large = gr_level0(&blades);

    // In a small image the blades near the camera are thinner than a pixel
    ecs_set(world, ecs_lookup(world, "capture"), Surface, {.width = GR_SMALL_WIDTH,
        .height = GR_SMALL_HEIGHT, .resolution_scale = 1});
    gr_frames(world, 3);
    gr_blades(world, meadow, &blades);
    small = gr_level0(&blades);
    double pixel = 2.0 * tan(30.0 * GLM_PI / 180.0) / GR_SMALL_HEIGHT;
    for (i = 0; i < ecs_vec_count(&blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&blades,
            box_render_grass_blade_t, i);
        double d[3] = {
            b->position[0] - eye[0], b->position[1] - eye[1], b->position[2] - eye[2]
        };
        double distance = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        test_assert((double)b->width >= pixel * distance * 0.995);
    }

    // As many fewer blades as they widen
    printf("level 0 blades %d at %dx%d, %d at %dx%d\n", large, GR_WIDTH, GR_HEIGHT, small,
        GR_SMALL_WIDTH, GR_SMALL_HEIGHT);
    test_assert(small > 0);
    test_assert(small * 10 < large * 6);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

/** Get the range of the red factor of the tint of the blades.
 *
 * @param blades The blades
 * @param lo Receives the smallest
 * @param hi Receives the largest
 */
static void gr_tint_range(
    const ecs_vec_t *blades,
    float *lo,
    float *hi)
{
    int32_t i;
    *lo = INFINITY;
    *hi = -INFINITY;
    for (i = 0; i < ecs_vec_count(blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(blades, box_render_grass_blade_t, i);
        *lo = b->tint[0] < *lo ? b->tint[0] : *lo;
        *hi = b->tint[0] > *hi ? b->tint[0] : *hi;
    }
}

void Grass_varies_over_world(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_vec_t blades;
    float lo, hi;
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);

    gr_shape(world, meadow, 0.7f, 0);
    gr_frames(world, 10);
    gr_blades(world, meadow, &blades);
    test_assert(ecs_vec_count(&blades) > 1000);
    gr_tint_range(&blades, &lo, &hi);
    printf("tint without variation %.3f to %.3f\n", lo, hi);
    test_assert(lo > 0.99f && hi < 1.01f);

    gr_shape(world, meadow, 0.7f, 1);
    gr_frames(world, 3);
    gr_blades(world, meadow, &blades);
    gr_tint_range(&blades, &lo, &hi);
    printf("tint with variation %.3f to %.3f\n", lo, hi);
    test_assert(hi - lo > 0.2f);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}
