/**
 * @file Grass.c
 * @brief Tests for the grass blades the GPU places and draws around the camera (box.grass,
 * gpu_grass.c): the blades stand on the plane or the terrain, take their level of detail from
 * their distance, thin out and widen further away, stay where they are as the camera moves and
 * across a shift of the WorldOrigin, follow the mask of the terrain, gather in clumps that share
 * their facing, never draw thinner than about a pixel, vary in color over the world, render lit,
 * take a stiffness and the phase of their clump in the wind, bend and move in it, show its gusts
 * as bands of light that roll downwind, show in the GPU stats, cast shadows into the cascades of
 * the sun within their shadow distance and into the spot and point light tiles only with
 * shadow_lights, and are culled behind what hides them from a view that culls by occlusion,
 * from its camera of the frame.
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

/** Meters the line from the eye to the tip of a blade clears the terrain by where it shows */
#define GR_SHOW_MARGIN (0.1f)

/** Meters the terrain rises above the line from the eye to the tip of a blade that is buried
 * deeper behind it than the blade is tall */
#define GR_BURIED (1.5f)

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

void Grass_wind_stiffness_and_phase(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_vec_t blades, near_;
    float eye[3], lo = INFINITY, hi = -INFINITY;
    int32_t i, j, count, pairs = 0, shared = 0, phases[16] = {0}, distinct = 0;
    ecs_vec_init_t(NULL, &blades, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &near_, box_render_grass_blade_t, 0);

    gr_shape(world, meadow, 0.7f, 0.5f);
    gr_frames(world, 10);
    gr_position(world, camera, eye);
    gr_blades(world, meadow, &blades);
    test_assert(ecs_vec_count(&blades) > 1000);

    // Every blade has its own stiffness and the phase of its clump
    for (i = 0; i < ecs_vec_count(&blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&blades, box_render_grass_blade_t, i);
        test_assert(b->stiffness >= 0 && b->stiffness <= 1);
        test_assert(b->phase >= 0 && b->phase < 1);
        lo = b->stiffness < lo ? b->stiffness : lo;
        hi = b->stiffness > hi ? b->stiffness : hi;
        phases[(int32_t)(b->phase * 16.0f + 0.5f) & 15] ++;
        float dx = b->position[0] - eye[0], dz = b->position[2] - eye[2];
        if (b->level == 0 && dx > -2.0f && dx < 2.0f && dz > -8.0f && dz < -3.0f) {
            *ecs_vec_append_t(NULL, &near_, box_render_grass_blade_t) = *b;
        }
    }

    for (i = 0; i < 16; i ++) {
        distinct += phases[i] != 0;
    }

    // Close blades mostly share their clump and so the phase of its swing
    count = ecs_vec_count(&near_);
    for (i = 0; i < count; i ++) {
        const box_render_grass_blade_t *a = ecs_vec_get_t(&near_, box_render_grass_blade_t, i);
        for (j = i + 1; j < count; j ++) {
            const box_render_grass_blade_t *b = ecs_vec_get_t(&near_,
                box_render_grass_blade_t, j);
            if (gr_flat_distance(a, b->position) < 0.2f) {
                shared += a->phase == b->phase;
                pairs ++;
            }
        }
    }

    printf("stiffness %.3f to %.3f, %d phases, %d of %d close pairs share it\n", lo, hi,
        distinct, shared, pairs);
    test_assert(lo < 0.3f);
    test_assert(hi > 0.7f);
    test_assert(distinct >= 12);
    test_assert(pairs > 100);
    test_assert(shared * 2 > pairs);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &near_, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &blades, box_render_grass_blade_t);
    ecs_fini(world);
}

/** Count the pixels of the lower half of two images that differ.
 *
 * @param a The first image
 * @param b The second image
 * @return The pixels where a channel differs by more than 24
 */
static int32_t gr_changed(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t x, y, c, result = 0;
    for (y = GR_HEIGHT / 2; y < GR_HEIGHT; y ++) {
        for (x = 0; x < GR_WIDTH; x ++) {
            const unsigned char *p = &a[(y * GR_WIDTH + x) * 4];
            const unsigned char *q = &b[(y * GR_WIDTH + x) * 4];
            for (c = 0; c < 3; c ++) {
                if (abs((int)p[c] - (int)q[c]) > 24) {
                    result ++;
                    break;
                }
            }
        }
    }

    return result;
}

void Grass_bends_in_wind(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    double bright = 0;

    // A dense meadow of wide blades in still air
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->density = 96;
    g->min_width = 0.05f;
    g->max_width = 0.09f;
    ecs_modified(world, meadow, Grass);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 0, .gust = 0.5f,
        .gust_scale = 10});
    gr_frames(world, 10);
    unsigned char *calm0 = gr_capture(world, "grass_calm0.png");
    gr_frames(world, 20);
    unsigned char *calm1 = gr_capture(world, "grass_calm1.png");
    int32_t calm_green = gr_green(calm1, &bright);

    // A strong wind bends the blades over and keeps them moving
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 10, .gust = 0.5f,
        .gust_scale = 10});
    gr_frames(world, 10);
    unsigned char *wind0 = gr_capture(world, "grass_wind0.png");
    gr_frames(world, 20);
    unsigned char *wind1 = gr_capture(world, "grass_wind1.png");
    int32_t wind_green = gr_green(wind1, &bright);

    int32_t still = gr_changed(calm0, calm1);
    int32_t bent = gr_changed(calm1, wind0);
    int32_t moving = gr_changed(wind0, wind1);
    printf("changed in calm %d, by the wind %d, in wind %d; green calm %d, wind %d\n", still,
        bent, moving, calm_green, wind_green);
    test_assert(bent > 4 * still);
    test_assert(moving > 2 * still);
    test_assert(moving > GR_WIDTH * GR_HEIGHT / 200);

    // The bent blades still cover the ground they grow on
    test_assert(wind_green * 10 > calm_green * 7);
    stbi_image_free(calm0);
    stbi_image_free(calm1);
    stbi_image_free(wind0);
    stbi_image_free(wind1);
    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Columns of the brightness profile of an image */
#define GR_COLUMNS (64)

/** Render a meadow seen from above the blades in a wind along +x, at two times half a second
 * apart.
 *
 * @param gust The gust of the Wind
 * @param out Receives the two images
 */
static void gr_gusts(
    float gust,
    unsigned char *out[2])
{
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->density = 48;
    g->lod[0] = 30;
    g->lod[1] = 80;
    g->lod[2] = 160;
    ecs_modified(world, meadow, Grass);
    ecs_set(world, camera, Position, {0, 5, 0});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 8, .gust = gust,
        .gust_scale = 12});
    gr_frames(world, 60);
    out[0] = gr_capture(world, gust > 0 ? "grass_gust0.png" : "grass_steady0.png");
    gr_frames(world, 29);
    out[1] = gr_capture(world, gust > 0 ? "grass_gust1.png" : "grass_steady1.png");
    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Brightness the gusts add to the columns of the far field of an image, against the same
 * frame in a steady wind.
 *
 * @param gust The image in gusts
 * @param steady The image in a steady wind
 * @param out Receives the mean difference of each of GR_COLUMNS columns
 */
static void gr_gust_profile(
    const unsigned char *gust,
    const unsigned char *steady,
    double out[GR_COLUMNS])
{
    int32_t x, y, c;
    int32_t span = GR_WIDTH / GR_COLUMNS;
    for (x = 0; x < GR_COLUMNS; x ++) {
        double sum = 0;
        for (y = GR_HEIGHT * 28 / 100; y < GR_HEIGHT * 40 / 100; y ++) {
            for (c = x * span; c < (x + 1) * span; c ++) {
                const unsigned char *p = &gust[(y * GR_WIDTH + c) * 4];
                const unsigned char *q = &steady[(y * GR_WIDTH + c) * 4];
                sum += (p[0] + p[1] + p[2]) - (q[0] + q[1] + q[2]);
            }
        }
        out[x] = sum / (3.0 * span * (GR_HEIGHT * 40 / 100 - GR_HEIGHT * 28 / 100));
    }
}

/** Correlation of a profile with another shifted by a number of columns, over the middle
 * columns.
 *
 * @param a The first profile
 * @param b The second profile
 * @param shift Columns b moves to the right relative to a
 * @return The correlation, -1..1
 */
static double gr_correlate(
    const double a[GR_COLUMNS],
    const double b[GR_COLUMNS],
    int32_t shift)
{
    int32_t x, n = 0;
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (x = GR_COLUMNS / 5; x < GR_COLUMNS * 4 / 5; x ++) {
        double u = a[x], v = b[x + shift];
        sa += u;
        sb += v;
        saa += u * u;
        sbb += v * v;
        sab += u * v;
        n ++;
    }

    double cov = sab - sa * sb / n;
    double var = (saa - sa * sa / n) * (sbb - sb * sb / n);
    return var > 0 ? cov / sqrt(var) : 0;
}

void Grass_gusts_roll_as_waves(void) {
    unsigned char *gust[2], *steady[2];
    double before[GR_COLUMNS], after[GR_COLUMNS];
    gr_gusts(0.6f, gust);
    gr_gusts(0, steady);

    // The gusts light and darken the far field in bands across the wind
    gr_gust_profile(gust[0], steady[0], before);
    gr_gust_profile(gust[1], steady[1], after);
    double sum = 0, sum2 = 0;
    int32_t x;
    for (x = 0; x < GR_COLUMNS; x ++) {
        sum += before[x];
        sum2 += before[x] * before[x];
    }
    double spread = sqrt(sum2 / GR_COLUMNS - (sum / GR_COLUMNS) * (sum / GR_COLUMNS));

    // Half a second later the bands moved downwind, to the right of the image
    int32_t s, best = 0;
    double best_corr = -1;
    for (s = -GR_COLUMNS / 6; s <= GR_COLUMNS / 6; s ++) {
        double corr = gr_correlate(before, after, s);
        if (corr > best_corr) {
            best_corr = corr;
            best = s;
        }
    }

    printf("gust bands spread %.2f, moved %d columns (correlation %.2f, %.2f in place)\n",
        spread, best, best_corr, gr_correlate(before, after, 0));
    test_assert(spread > 1.5);
    test_assert(best >= 2);
    test_assert(best_corr > 0.7);
    stbi_image_free(gust[0]);
    stbi_image_free(gust[1]);
    stbi_image_free(steady[0]);
    stbi_image_free(steady[1]);
}

/** Get the GPU stats of a world.
 *
 * @param world The world
 * @return The stats
 */
static box_render_gpu_stats_t gr_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t result;
    test_bool(box_render_gpu_stats(world, &result), true);

    return result;
}

void Grass_casts_into_cascades(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    gr_frames(world, 12);

    // By default the blades cast into the cascades near the camera, every frame, and not into
    // the light tiles
    box_render_gpu_stats_t stats = gr_stats(world);
    printf("cascades %d, tiles %d, passes %d\n", stats.grass_cascades, stats.grass_tiles,
        stats.grass_passes);
    test_assert(stats.grass_cascades >= 12);
    test_int(stats.grass_tiles, 0);

    // A Grass with no shadow distance casts none
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->shadow_distance = 0;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 2);
    int32_t before = gr_stats(world).grass_cascades;
    gr_frames(world, 6);
    test_int(gr_stats(world).grass_cascades, before);

    // Casting again
    g = ecs_ensure(world, meadow, Grass);
    g->shadow_distance = 20;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 6);
    test_assert(gr_stats(world).grass_cascades > before);

    gr_expect_no_errors(world);
    ecs_fini(world);
}

void Grass_shadow_counts_apart(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });

    // The blades that cast do not add to the blades the view placed and drew
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->shadow_distance = 0;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 12);
    double drawn = ecs_singleton_get(world, GpuStats)->grass.drawn;
    g = ecs_ensure(world, meadow, Grass);
    g->shadow_distance = 30;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 12);
    double casting = ecs_singleton_get(world, GpuStats)->grass.drawn;
    printf("drawn %.0f without shadows, %.0f with\n", drawn, casting);
    test_assert(drawn > 0);
    test_assert(fabs(casting - drawn) < 0.05 * drawn);

    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Add a spot light and a point light with shadows to the meadow of the plane scene, and turn
 * on the shadows of the spot and point lights of the Scene.
 *
 * @param world The world
 */
static void gr_lights(
    ecs_world_t *world)
{
    Scene *scene = ecs_singleton_ensure(world, Scene);
    scene->punctual_shadows = true;
    ecs_singleton_modified(world, Scene);
    ecs_entity_t spot = ecs_entity(world, { .name = "grass_spot" });
    ecs_set(world, spot, Position, {0, 3, -5});
    ecs_set(world, spot, Rotation, {1.5708f, 0, 0});
    ecs_set(world, spot, SpotLight, {.intensity = 60, .range = 10, .inner_angle = 20,
        .outer_angle = 35, .shadows = true});
    ecs_entity_t point = ecs_entity(world, { .name = "grass_point" });
    ecs_set(world, point, Position, {3, 1.5, -4});
    ecs_set(world, point, PointLight, {.intensity = 30, .range = 8, .shadows = true});
}

void Grass_lights_only_when_enabled(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    gr_lights(world);
    gr_frames(world, 12);

    // The light tiles render, the blades cast nothing into them
    box_render_gpu_stats_t stats = gr_stats(world);
    printf("tiles %d, grass tiles %d\n", stats.shadow_tiles, stats.grass_tiles);
    test_assert(stats.shadow_tiles > 0);
    test_int(stats.grass_tiles, 0);

    // With shadow_lights the blades cast into both tiles every frame
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->shadow_lights = true;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 2);
    int32_t before = gr_stats(world).grass_tiles;
    gr_frames(world, 6);
    stats = gr_stats(world);
    printf("grass tiles %d over 6 frames\n", stats.grass_tiles - before);
    test_assert(stats.grass_tiles - before >= 6 * 2);

    gr_expect_no_errors(world);
    ecs_fini(world);
}

/** Get the mean brightness of the lower half of an image, the ground and blades near the
 * camera.
 *
 * @param px The image
 * @return The mean of the channels
 */
static double gr_bright(
    const unsigned char *px)
{
    double sum = 0;
    int32_t x, y;
    for (y = GR_HEIGHT / 2; y < GR_HEIGHT; y ++) {
        for (x = 0; x < GR_WIDTH; x ++) {
            const unsigned char *p = &px[(y * GR_WIDTH + x) * 4];
            sum += (double)p[0] + p[1] + p[2];
        }
    }

    return sum / (3.0 * GR_WIDTH * (GR_HEIGHT - GR_HEIGHT / 2));
}

void Grass_shadows_darken_meadow(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t sun = gr_lookup(world, "grass_sun");

    // A low sun from the side over tall blades
    ecs_set(world, sun, Rotation, {0.25f, 1.9f, 0});
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->density = 64;
    g->min_height = 0.4f;
    g->max_height = 0.8f;
    g->shadow_distance = 0;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 10);
    unsigned char *px = gr_capture(world, "grass_unshadowed.png");
    double bare = gr_bright(px);
    stbi_image_free(px);

    g = ecs_ensure(world, meadow, Grass);
    g->shadow_distance = 30;
    ecs_modified(world, meadow, Grass);
    gr_frames(world, 10);
    px = gr_capture(world, "grass_shadowed.png");
    double shadowed = gr_bright(px);
    stbi_image_free(px);

    printf("brightness %.2f without shadows, %.2f with\n", bare, shadowed);
    test_assert(shadowed < bare * 0.95);
    gr_expect_no_errors(world);
    ecs_fini(world);
}


/** Add a wall across the meadow of the plane scene in front of the camera, which hides the
 * ground beyond it from the camera.
 *
 * @param world The world
 * @return The wall
 */
static ecs_entity_t gr_wall(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_entity(world, { .name = "grass_wall" });
    ecs_set(world, result, Position, {0, 2.0f, -20.0f});
    ecs_set(world, result, Box, {400, 10, 1});
    ecs_set(world, result, Rgb, {120, 120, 120});

    return result;
}

/** Count the blades within a range of x and z.
 *
 * @param blades The blades
 * @param x0 Lowest x
 * @param x1 Highest x
 * @param z0 Lowest z
 * @param z1 Highest z
 * @return The blades
 */
static int32_t gr_within(
    const ecs_vec_t *blades,
    float x0,
    float x1,
    float z0,
    float z1)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(blades); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(blades, box_render_grass_blade_t, i);
        result += b->position[0] >= x0 && b->position[0] <= x1 &&
            b->position[2] >= z0 && b->position[2] <= z1;
    }

    return result;
}

void Grass_culls_behind_occluder(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_vec_t open, walled;
    ecs_vec_init_t(NULL, &open, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &walled, box_render_grass_blade_t, 0);
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    gr_frames(world, 12);
    gr_blades(world, meadow, &open);
    const GpuStats *s = ecs_singleton_get(world, GpuStats);
    double drawn_open = s->grass.drawn;
    test_flt(s->grass.culled_patches, 0);
    test_flt(s->grass.culled_blades, 0);

    gr_wall(world);
    gr_frames(world, 12);
    gr_blades(world, meadow, &walled);
    s = ecs_singleton_get(world, GpuStats);
    int32_t behind_open = gr_within(&open, -1000, 1000, -1000, -25);
    int32_t behind = gr_within(&walled, -1000, 1000, -1000, -25);
    int32_t front_open = gr_within(&open, -1000, 1000, -18, 1000);
    int32_t front = gr_within(&walled, -1000, 1000, -18, 1000);
    printf("open %d (%d behind), walled %d (%d behind), drawn %.0f then %.0f, "
        "culled %.1f patches %.0f blades\n", ecs_vec_count(&open), behind_open,
        ecs_vec_count(&walled), behind, drawn_open, s->grass.drawn, s->grass.culled_patches,
        s->grass.culled_blades);

    // Nothing behind the wall, everything in front of it
    test_assert(behind_open > 1000);
    test_int(behind, 0);
    test_assert(front * 100 >= front_open * 99);
    test_assert(s->grass.culled_patches > 10);
    test_assert(s->grass.drawn < drawn_open * 0.8);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &open, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &walled, box_render_grass_blade_t);
    ecs_fini(world);
}

void Grass_occlusion_follows_camera(void) {
    ecs_world_t *world = gr_world("test/assets/grass/plane.flecs");
    ecs_entity_t meadow = gr_lookup(world, "meadow");
    ecs_entity_t camera = gr_lookup(world, "grass_camera");
    ecs_entity_t wall = gr_wall(world);
    ecs_vec_t first, settled;
    ecs_vec_init_t(NULL, &first, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &settled, box_render_grass_blade_t, 0);
    gr_frames(world, 12);

    // Turn away from the wall toward +x: the blades there are all placed in the first frame
    // that looks at them, not a frame after
    ecs_set(world, camera, Rotation, {-0.25f, -1.5707963f, 0});
    gr_frames(world, 1);
    gr_blades(world, meadow, &first);
    gr_frames(world, 10);
    gr_blades(world, meadow, &settled);
    int32_t turned = gr_within(&first, 5, 1000, -15, 15);
    int32_t turned_settled = gr_within(&settled, 5, 1000, -15, 15);
    printf("turned %d, settled %d\n", turned, turned_settled);
    test_assert(turned_settled > 1000);
    test_assert(turned * 100 >= turned_settled * 99);

    // Turn back and remove the wall: the meadow beyond it shows in the first frame
    ecs_set(world, camera, Rotation, {-0.25f, 0, 0});
    gr_frames(world, 10);
    gr_blades(world, meadow, &first);
    test_int(gr_within(&first, -1000, 1000, -1000, -25), 0);
    ecs_delete(world, wall);
    gr_frames(world, 1);
    gr_blades(world, meadow, &first);
    gr_frames(world, 10);
    gr_blades(world, meadow, &settled);
    int32_t shown = gr_within(&first, -1000, 1000, -1000, -25);
    int32_t shown_settled = gr_within(&settled, -1000, 1000, -1000, -25);
    printf("shown %d, settled %d\n", shown, shown_settled);
    test_assert(shown_settled > 1000);
    test_assert(shown * 100 >= shown_settled * 99);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &first, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &settled, box_render_grass_blade_t);
    ecs_fini(world);
}

/** Get how deep the tip of a blade is buried behind the terrain seen from an eye: the most the
 * resident terrain rises above the line from the eye to the tip, negative where the line clears
 * it everywhere.
 *
 * @param world The world
 * @param terrain The terrain
 * @param origin Origin of the frame of the terrain in the local world
 * @param eye The eye in the local world
 * @param b The blade
 * @return Meters, -1000 where the terrain under the line is not resident
 */
static float gr_buried(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const double origin[3],
    const float eye[3],
    const box_render_grass_blade_t *b)
{
    float d[3] = { b->position[0] - eye[0], b->position[1] + b->height - eye[1],
        b->position[2] - eye[2] };
    float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), result = -1000.0f;
    int32_t i, steps = (int32_t)(length / 0.25f);
    for (i = 4; i < steps - 4; i ++) {
        float t = (float)i / (float)steps, h = 0;
        float x = eye[0] + d[0] * t, z = eye[2] + d[2] * t;
        if (!box_terrain_height(world, terrain, x - origin[0], z - origin[2], &h)) {
            return -1000.0f;
        }

        h = (float)origin[1] + h - (eye[1] + d[1] * t);
        result = h > result ? h : result;
    }

    return result;
}

void Grass_culls_behind_hills(void) {
    ecs_world_t *world = gr_world("test/assets/grass/hills.flecs");
    ecs_entity_t terrain = gr_lookup(world, "grass_hills");
    ecs_entity_t meadow = gr_lookup(world, "grass_meadow");
    ecs_entity_t view = gr_lookup(world, "grass_view");
    ecs_entity_t walker = gr_lookup(world, "grass_walker");
    ecs_vec_t culled, open;
    ecs_map_t map;
    double origin[3], at[2] = {0, 0};
    float ground = 0, eye[3];
    int32_t i, missing = 0, shown = 0, buried = 0, buried_culled = 0;
    ecs_vec_init_t(NULL, &culled, box_render_grass_blade_t, 0);
    ecs_vec_init_t(NULL, &open, box_render_grass_blade_t, 0);
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    gr_terrain_wait(world, terrain);

    // Stand at eye height among the hills
    box_terrain_origin(world, terrain, origin);
    test_bool(box_terrain_height(world, terrain, -origin[0], -origin[2], &ground), true);
    eye[0] = 0;
    eye[1] = (float)origin[1] + ground + 1.7f;
    eye[2] = 0;
    ecs_set(world, walker, Position, {eye[0], eye[1], eye[2]});
    gr_frames(world, 12);
    gr_blades(world, meadow, &culled);
    const GpuStats *s = ecs_singleton_get(world, GpuStats);
    double drawn = s->grass.drawn, patches = s->grass.culled_patches;
    double blades = s->grass.culled_blades;

    // The same view without occlusion culling
    View *v = ecs_get_mut(world, view, View);
    v->culling.occlusion = false;
    ecs_modified(world, view, View);
    gr_frames(world, 12);
    gr_blades(world, meadow, &open);
    s = ecs_singleton_get(world, GpuStats);

    // No blade that occlusion dropped shows over the hills, and it drops most of those buried
    // deeper behind them than a blade is tall
    gr_map(&culled, at, &map);
    for (i = 0; i < ecs_vec_count(&open); i ++) {
        const box_render_grass_blade_t *b = ecs_vec_get_t(&open, box_render_grass_blade_t, i);
        bool kept = gr_match(&culled, &map, at, b, at);
        float depth = gr_buried(world, terrain, origin, eye, b);
        missing += !kept;
        shown += !kept && depth < -GR_SHOW_MARGIN;
        buried += depth > GR_BURIED;
        buried_culled += depth > GR_BURIED && !kept;
    }

    ecs_map_fini(&map);
    printf("culled %d, open %d, missing %d (%d shown), buried %d (%d culled), drawn %.0f of "
        "%.0f, culled %.1f patches %.0f blades\n", ecs_vec_count(&culled),
        ecs_vec_count(&open), missing, shown, buried, buried_culled, drawn, s->grass.drawn,
        patches, blades);
    test_int(shown, 0);
    test_assert(buried > 500);
    test_assert(buried_culled * 10 >= buried * 6);
    test_assert(blades + patches > 0);
    test_assert(missing > 500);
    test_assert(drawn < s->grass.drawn);
    test_flt(s->grass.culled_patches, 0);
    test_flt(s->grass.culled_blades, 0);
    gr_expect_no_errors(world);
    ecs_vec_fini_t(NULL, &culled, box_render_grass_blade_t);
    ecs_vec_fini_t(NULL, &open, box_render_grass_blade_t);
    ecs_fini(world);
}
