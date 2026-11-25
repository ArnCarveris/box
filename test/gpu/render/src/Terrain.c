/**
 * @file Terrain.c
 * @brief Tests for drawing a Terrain on the GPU backend: a bounded terrain of a known graph
 * renders its hills, tiles of adjacent levels meet without cracks, moving the focus refines the
 * tiles around it and drops far ones within the budget, and a shift of the world origin changes
 * nothing visible.
 *
 * The scenes are test/assets/terrain/bounded.flecs (hills seen from the south) and
 * test/assets/terrain/cracks.flecs (rugged ridges seen steeply from above over a magenta clear).
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

/** Width in pixels of the images */
#define TERRAIN_WIDTH (128)

/** Height in pixels of the images */
#define TERRAIN_HEIGHT (96)

/** Seconds of a frame */
#define TERRAIN_DELTA (1.0f / 60.0f)

/** Frames a test waits at most for the tiles of a terrain */
#define TERRAIN_FRAMES (240)

/** Largest channel difference of a pixel between the images of a shift */
#define TERRAIN_MAX_DIFF (64)

/** Pixels that may differ by more than 4 in a channel after a shift, per thousand */
#define TERRAIN_DIFF_PERMILLE (10)

/** Create a headless world that renders on the main thread into a capture surface, so the
 * backend evaluates the height graphs of the tiles, and run a scene script in it.
 *
 * @param file The script, relative to the repository root
 * @return The world
 */
static ecs_world_t* terrain_world(
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});
    ecs_entity_t capture = ecs_entity(world, { .name = "capture" });
    ecs_set(world, capture, Surface, {.width = TERRAIN_WIDTH, .height = TERRAIN_HEIGHT,
        .resolution_scale = 1});
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);

    return world;
}

/** Run frames until a terrain has no pending tile and draws at least one, then a few more so
 * the last tiles draw.
 *
 * @param world The world
 * @param terrain The terrain entity
 */
static void terrain_settle(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const TerrainStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TERRAIN_FRAMES; i ++) {
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
        status = ecs_get(world, terrain, TerrainStatus);
        if (status && !status->pending && status->drawn) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->pending || !status->drawn) {
        printf("pending %d, drawn %d\n", status->pending, status->drawn);
    }

    test_int(status->pending, 0);
    test_assert(status->drawn > 0);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
    }
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* terrain_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    ecs_set(world, e, Surface, {.width = TERRAIN_WIDTH, .height = TERRAIN_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TERRAIN_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, TERRAIN_WIDTH);
    test_int(h, TERRAIN_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Check that the backend logged no error.
 *
 * @param world The world
 */
static void terrain_expect_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);
}

/** Check whether a pixel is the clear color of the bounded scene.
 *
 * @param px The pixel
 * @return Whether it is the dark blue clear
 */
static bool terrain_is_sky(
    const unsigned char *px)
{
    return abs((int)px[0] - 20) <= 6 && abs((int)px[1] - 30) <= 6 && abs((int)px[2] - 60) <= 6;
}

/** Check whether a pixel is the magenta clear of the crack scene.
 *
 * @param px The pixel
 * @return Whether it is magenta
 */
static bool terrain_is_magenta(
    const unsigned char *px)
{
    return px[0] > 200 && px[1] < 80 && px[2] > 200;
}

void Terrain_levels_no_cracks(void) {
    // terrain/terrain_tiles
    ecs_world_t *world = terrain_world("test/assets/terrain/cracks.flecs");
    ecs_entity_t terrain = ecs_lookup(world, "terrain_cracks.ridges");
    test_assert(terrain != 0);
    terrain_settle(world, terrain);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    int32_t levels = 0, i;
    for (i = 0; i < BOX_TERRAIN_LEVELS; i ++) {
        levels += status->tiles[i] > 0;
    }

    test_assert(levels >= 3);
    unsigned char *px = terrain_frame(world, "terrain_cracks.png");
    int32_t magenta = 0;
    for (i = 0; i < TERRAIN_WIDTH * TERRAIN_HEIGHT; i ++) {
        magenta += terrain_is_magenta(&px[i * 4]);
    }

    if (magenta) {
        printf("%d magenta pixels\n", magenta);
    }

    test_int(magenta, 0);
    terrain_expect_no_errors(world);
    stbi_image_free(px);
    ecs_fini(world);
}

void Terrain_far_same_image(void) {
    // terrain/terrain_tiles
    ecs_world_t *a = terrain_world("test/assets/terrain/bounded.flecs");
    ecs_world_t *b = terrain_world("test/assets/terrain/bounded.flecs");
    ecs_entity_t ta = ecs_lookup(a, "terrain_bounded.hills");
    ecs_entity_t tb = ecs_lookup(b, "terrain_bounded.hills");
    ecs_entity_t camera = ecs_lookup(b, "terrain_bounded.camera");
    const double offset[3] = { 2097152 + 2816, 0, -1048576 - 1280 };
    test_assert(camera != 0);
    const Position *p = ecs_get(b, camera, Position);
    ecs_set(b, camera, Position, {p->x + offset[0], p->y, p->z + offset[2]});
    ecs_set(b, tb, Position, {offset[0], offset[1], offset[2]});
    terrain_settle(a, ta);
    terrain_settle(b, tb);
    unsigned char *pa = terrain_frame(a, "terrain_a.png");
    unsigned char *pb = terrain_frame(b, "terrain_b.png");
    int32_t worst = 0, differ = 0, i, c;
    for (i = 0; i < TERRAIN_WIDTH * TERRAIN_HEIGHT; i ++) {
        int32_t d = 0;
        for (c = 0; c < 3; c ++) {
            int32_t delta = abs((int)pa[i * 4 + c] - (int)pb[i * 4 + c]);
            d = delta > d ? delta : d;
        }

        worst = d > worst ? d : worst;
        differ += d > 4;
    }

    if (worst > TERRAIN_MAX_DIFF || differ * 1000 > TERRAIN_DIFF_PERMILLE * TERRAIN_WIDTH *
        TERRAIN_HEIGHT)
    {
        printf("worst channel difference %d, %d pixels differ\n", worst, differ);
    }

    test_assert(worst <= TERRAIN_MAX_DIFF);
    test_assert(differ * 1000 <= TERRAIN_DIFF_PERMILLE * TERRAIN_WIDTH * TERRAIN_HEIGHT);
    const TerrainStatus *sa = ecs_get(a, ta, TerrainStatus);
    const TerrainStatus *sb = ecs_get(b, tb, TerrainStatus);
    test_int(sa->drawn, sb->drawn);
    terrain_expect_no_errors(a);
    terrain_expect_no_errors(b);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void Terrain_extract_reuses_heights(void) {
    // terrain/terrain_tiles
    ecs_world_t *world = terrain_world("test/assets/terrain/cracks.flecs");
    ecs_entity_t terrain = ecs_lookup(world, "terrain_cracks.ridges");
    ecs_entity_t camera = ecs_lookup(world, "terrain_cracks.camera");
    test_assert(terrain != 0);
    test_assert(camera != 0);
    terrain_settle(world, terrain);
    int32_t i, heights, grids, h, g, updates, selects, u, s;
    for (i = 0; i < 8; i ++) {
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
    }

    box_render_terrain_counters(world, &heights, &grids);
    box_terrain_counters(world, terrain, &updates, &selects);
    test_assert(heights > 0);
    TerrainStatus before = *ecs_get(world, terrain, TerrainStatus);
    for (i = 0; i < 8; i ++) {
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
    }

    box_render_terrain_counters(world, &h, &g);
    box_terrain_counters(world, terrain, &u, &s);
    test_int(h, heights);
    test_int(u, updates);
    test_int(s, selects);
    test_int(ecs_os_memcmp(&before, ecs_get(world, terrain, TerrainStatus),
        ECS_SIZEOF(TerrainStatus)), 0);

    for (i = 1; i <= 20; i ++) {
        ecs_set(world, camera, Position, {10.0f * (float)i, 300, 0});
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
    }

    terrain_settle(world, terrain);
    box_render_terrain_counters(world, &h, &g);
    box_terrain_counters(world, terrain, &u, &s);
    test_assert(h > heights);
    test_assert(u > updates);
    test_assert(s > selects);
    test_assert(ecs_get(world, terrain, TerrainStatus)->created > before.created);
    terrain_expect_no_errors(world);
    ecs_fini(world);
}
