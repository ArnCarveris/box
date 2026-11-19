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

void Terrain_bounded_renders(void) {
    // terrain/terrain_tiles
    ecs_world_t *world = terrain_world("test/assets/terrain/bounded.flecs");
    ecs_entity_t terrain = ecs_lookup(world, "terrain_bounded.hills");
    test_assert(terrain != 0);
    terrain_settle(world, terrain);
    unsigned char *px = terrain_frame(world, "terrain_bounded.png");

    // The terrain covers the middle of the image, the sky shows above its far edge, and the
    // lit hills are green with light and shade.
    int32_t ground = 0, sky_top = 0, x, y, lo = 255, hi = 0;
    for (y = 0; y < TERRAIN_HEIGHT; y ++) {
        for (x = 0; x < TERRAIN_WIDTH; x ++) {
            const unsigned char *p = &px[(y * TERRAIN_WIDTH + x) * 4];
            bool sky = terrain_is_sky(p);
            sky_top += sky && y < TERRAIN_HEIGHT / 8;
            if (!sky && y > TERRAIN_HEIGHT / 3) {
                ground ++;
                lo = p[1] < lo ? p[1] : lo;
                hi = p[1] > hi ? p[1] : hi;
                test_assert(p[1] >= p[2]);
            }
        }
    }

    if (ground < TERRAIN_WIDTH * TERRAIN_HEIGHT / 3 || hi - lo < 40) {
        printf("ground %d, green %d..%d, sky at the top %d\n", ground, lo, hi, sky_top);
    }

    test_assert(ground >= TERRAIN_WIDTH * TERRAIN_HEIGHT / 3);
    test_assert(sky_top > 0);
    test_assert(hi - lo >= 40);

    const unsigned char *center = &px[((TERRAIN_HEIGHT * 2 / 3) * TERRAIN_WIDTH +
        TERRAIN_WIDTH / 2) * 4];
    test_assert(!terrain_is_sky(center));
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status->tiles[0] > 0);
    test_assert(status->gpu_bytes > 0);
    terrain_expect_no_errors(world);
    stbi_image_free(px);
    ecs_fini(world);
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

void Terrain_focus_refines(void) {
    // terrain/terrain_tiles
    ecs_world_t *world = terrain_world("test/assets/terrain/cracks.flecs");
    ecs_entity_t terrain = ecs_lookup(world, "terrain_cracks.ridges");
    ecs_entity_t camera = ecs_lookup(world, "terrain_cracks.camera");
    test_assert(terrain != 0);
    test_assert(camera != 0);
    terrain_settle(world, terrain);
    const Terrain *t = ecs_get(world, terrain, Terrain);
    int32_t budget = t->budget, i, count;
    TerrainStatus last = *ecs_get(world, terrain, TerrainStatus);

    // Fly 600 m east over 60 frames: every frame makes and drops tiles within the budget.
    for (i = 1; i <= 60; i ++) {
        ecs_set(world, camera, Position, {10.0f * (float)i, 300, 0});
        ecs_progress(world, TERRAIN_DELTA);
        box_render_sync(world);
        const TerrainStatus *s = ecs_get(world, terrain, TerrainStatus);
        test_assert(s->created - last.created <= budget);
        test_assert(s->dropped - last.dropped <= budget * 4);
        test_assert(s->drawn > 0);
        last = *s;
    }

    terrain_settle(world, terrain);
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    bool fine_near = false;
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *tile = tiles[i];
        double size = 16.0 * (double)(1 << tile->level);
        double cx = ((double)tile->ix + 0.5) * size, cz = ((double)tile->iz + 0.5) * size;
        double d = sqrt((cx - 600.0) * (cx - 600.0) + cz * cz);
        if (tile->level == 0 && tile->drawn) {
            test_assert(d < 64.0 + 32.0 * 1.5);
            fine_near |= d < 16.0;
        }

        // Nothing is left of the finest tiles around the start
        test_assert(!(tile->level == 0 && sqrt(cx * cx + cz * cz) < 64.0));
    }

    test_bool(fine_near, true);
    terrain_expect_no_errors(world);
    ecs_fini(world);
}

void Terrain_origin_shift_same_image(void) {
    // terrain/terrain_tiles
    ecs_world_t *a = terrain_world("test/assets/terrain/bounded.flecs");
    ecs_world_t *b = terrain_world("test/assets/terrain/bounded.flecs");
    ecs_singleton_add(b, WorldOrigin);
    WorldOrigin *o = ecs_singleton_get_mut(b, WorldOrigin);
    o->enabled = false;
    ecs_singleton_modified(b, WorldOrigin);
    ecs_entity_t ta = ecs_lookup(a, "terrain_bounded.hills");
    ecs_entity_t tb = ecs_lookup(b, "terrain_bounded.hills");
    terrain_settle(a, ta);
    terrain_settle(b, tb);
    box_world_origin_shift(b, 0, (double[3]){ 2816, 0, -1280 });
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
