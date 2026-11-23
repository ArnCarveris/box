/**
 * @file ShadowCache.c
 * @brief Tests that the shadow atlas renders a spot or point light tile only when its light, its
 * placement or a caster within its bounds changed, with the GPU backend.
 *
 * Spot lights A at x = -8 and B at x = 8, 6 units up, shine down with a range of 10 and a 20
 * degree cone; a point light at (0, 6, 16) has a range of 8. A slab 3 units up lies under each,
 * a fourth at z = -14 outside every range; tiles are point, A, B, in the order of collection.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../../../../src/posix.h"
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge in pixels of the images */
#define CACHE_SIZE (64)

/** Tile index of the point light */
#define CACHE_POINT (0)

/** Tile index of spot light A */
#define CACHE_SPOT_A (1)

/** Tile index of spot light B */
#define CACHE_SPOT_B (2)

/** Tiles of the scene */
#define CACHE_TILES (3)

/** Seconds a frame of a transition case advances the world, a quarter of each transition */
#define CACHE_DELTA (0.25f)

/** The entities of the scene */
typedef struct cache_scene_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t camera;    /**< The camera */
    ecs_entity_t spot_a;    /**< Spot light A */
    ecs_entity_t spot_b;    /**< Spot light B */
    ecs_entity_t point;     /**< The point light */
    ecs_entity_t slab_a;    /**< The slab under spot light A */
    ecs_entity_t slab_b;    /**< The slab under spot light B */
    ecs_entity_t slab_point; /**< The slab under the point light */
    ecs_entity_t slab_far;  /**< The slab outside every range */
} cache_scene_t;

/** The counters of the shadow atlas a frame changed */
typedef struct cache_frame_t {
    int32_t rendered;       /**< Tiles rendered */
    int32_t reused;         /**< Tiles kept */
    int32_t passes;         /**< Atlas render passes */
} cache_frame_t;

/** Create a thin slab.
 *
 * @param world The world
 * @param position World position of its center
 * @return The slab
 */
static ecs_entity_t cache_slab(
    ecs_world_t *world,
    vec3 position)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {1, 0.1f, 1});
    ecs_set(world, e, Rgb, {255, 255, 255});

    return e;
}

/** Create a spot light shining straight down.
 *
 * @param world The world
 * @param x World x of the light
 * @return The light
 */
static ecs_entity_t cache_spot(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 6, 0});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, e, SpotLight, {
        .intensity = 60 * GLM_PIf, .range = 10, .inner_angle = 15, .outer_angle = 20,
        .shadows = true, .shadow_resolution = 512
    });

    return e;
}

/** Create the scene on the main thread without ambient light, with a white rough floor.
 *
 * @param out Receives the entities
 */
static void cache_scene(
    cache_scene_t *out)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 14, 6});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 90, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true,
        .shader_features.Extended.alpha_cutout = true});

    out->world = world;
    out->camera = camera;
    out->point = ecs_new(world);
    ecs_set(world, out->point, Position, {0, 6, 16});
    ecs_set(world, out->point, PointLight, {
        .intensity = 60 * GLM_PIf, .range = 8, .shadows = true, .shadow_resolution = 512
    });
    out->spot_a = cache_spot(world, -8);
    out->spot_b = cache_spot(world, 8);
    out->slab_a = cache_slab(world, (vec3){-8, 3, 0});
    out->slab_b = cache_slab(world, (vec3){8, 3, 0});
    out->slab_point = cache_slab(world, (vec3){0, 3, 16});
    out->slab_far = cache_slab(world, (vec3){0, 3, -14});
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a CACHE_SIZE square image, freed with stbi_image_free
 */
static unsigned char* cache_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = CACHE_SIZE, .height = CACHE_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, CACHE_SIZE);
    test_int(h, CACHE_SIZE);
    ecs_os_free(path);

    return px;
}

/** Render the first frame, which renders every tile, into an image file.
 *
 * @param s The scene
 */
static void cache_first(
    const cache_scene_t *s)
{
    box_render_gpu_stats_t stats;
    stbi_image_free(cache_capture(s->world, "cache_first.png"));
    test_int(ecs_singleton_get(s->world, RenderStatus)->shadow_tiles, CACHE_TILES);
    test_int(ecs_singleton_get(s->world, RenderStatus)->shadow_dropped, 0);
    test_assert(box_render_gpu_stats(s->world, &stats));
    test_int(stats.shadow_tiles, CACHE_TILES);
    test_int(stats.shadow_reused, 0);
    test_int(stats.shadow_passes, 1);
}

/** Run a frame that advances the world by a delta and read what it changed of the counters of
 * the shadow atlas.
 *
 * @param world The world
 * @param delta Seconds the frame advances the world, 0 for the time measured
 * @return The counters of the frame
 */
static cache_frame_t cache_frame_at(
    ecs_world_t *world,
    float delta)
{
    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, delta);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, CACHE_TILES);

    return (cache_frame_t){
        .rendered = after.shadow_tiles - before.shadow_tiles,
        .reused = after.shadow_reused - before.shadow_reused,
        .passes = after.shadow_passes - before.shadow_passes
    };
}

/** Run a frame and read what it changed of the counters of the shadow atlas.
 *
 * @param world The world
 * @return The counters of the frame
 */
static cache_frame_t cache_frame(
    ecs_world_t *world)
{
    return cache_frame_at(world, 0);
}

/** Run a frame that advances the world by a delta and check that it renders no tile and
 * records no atlas pass.
 *
 * @param world The world
 * @param delta Seconds the frame advances the world, 0 for the time measured
 */
static void cache_none_at(
    ecs_world_t *world,
    float delta)
{
    cache_frame_t f = cache_frame_at(world, delta);
    test_int(f.rendered, 0);
    test_int(f.reused, CACHE_TILES);
    test_int(f.passes, 0);
}

/** Run a frame and check that it renders no tile and records no atlas pass.
 *
 * @param world The world
 */
static void cache_none(
    ecs_world_t *world)
{
    cache_none_at(world, 0);
}

/** Run a frame that advances the world by a delta and check that it renders one tile in one
 * atlas pass and keeps the others.
 *
 * @param world The world
 * @param tile Index of the tile that renders
 * @param delta Seconds the frame advances the world, 0 for the time measured
 */
static void cache_one_at(
    ecs_world_t *world,
    int32_t tile,
    float delta)
{
    box_render_cull_stats_t cull;
    int32_t i;
    cache_frame_t f = cache_frame_at(world, delta);
    test_int(f.rendered, 1);
    test_int(f.reused, CACHE_TILES - 1);
    test_int(f.passes, 1);
    for (i = 0; i < CACHE_TILES; i ++) {
        test_bool(box_render_gpu_cull_tile(world, i, &cull), i == tile);
    }
}

/** Run a frame and check that it renders one tile in one atlas pass and keeps the others.
 *
 * @param world The world
 * @param tile Index of the tile that renders
 */
static void cache_one(
    ecs_world_t *world,
    int32_t tile)
{
    cache_one_at(world, tile, 0);
}

/** Run the frames of a transition of a second that starts in the next frame: the four frames
 * at progress 0 to 3/4 and the frame it ends in render the tile of its caster and keep the
 * others, and the frame after renders none.
 *
 * @param world The world
 * @param tile Index of the tile of the caster
 */
static void cache_transition(
    ecs_world_t *world,
    int32_t tile)
{
    int32_t i;
    for (i = 0; i < 5; i ++) {
        cache_one_at(world, tile, CACHE_DELTA);
    }

    cache_none_at(world, CACHE_DELTA);
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void cache_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

void ShadowCache_static_scene_renders_once(void) {
    // lighting/shadows
    cache_scene_t s;
    int32_t i;
    cache_scene(&s);
    cache_first(&s);
    for (i = 0; i < 3; i ++) {
        cache_none(s.world);
    }

    cache_no_errors(s.world);
    ecs_fini(s.world);
}

void ShadowCache_moved_caster_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    cache_first(&s);
    cache_none(s.world);

    ecs_set(s.world, s.slab_a, Position, {-8, 3, 1});
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    ecs_set(s.world, s.slab_b, Position, {7.5f, 3, 0});
    cache_one(s.world, CACHE_SPOT_B);
    cache_none(s.world);

    ecs_set(s.world, s.slab_point, Position, {0, 2, 16});
    cache_one(s.world, CACHE_POINT);
    cache_none(s.world);

    ecs_set(s.world, s.slab_far, Position, {1, 3, -14});
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

void ShadowCache_added_removed_caster(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    cache_first(&s);
    cache_none(s.world);

    ecs_entity_t added = cache_slab(s.world, (vec3){-8, 2, 2});
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    ecs_delete(s.world, added);
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    ecs_add(s.world, s.slab_point, NoShadow);
    cache_one(s.world, CACHE_POINT);
    cache_none(s.world);

    ecs_remove(s.world, s.slab_point, NoShadow);
    cache_one(s.world, CACHE_POINT);
    cache_none(s.world);

    added = cache_slab(s.world, (vec3){0, 3, -16});
    cache_none(s.world);
    ecs_delete(s.world, added);
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

void ShadowCache_dynamic_caster_renders_when_moved(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    ecs_entity_t mover = cache_slab(s.world, (vec3){0, 3, 13});
    ecs_add(s.world, mover, DynamicTransform);
    cache_first(&s);
    cache_none(s.world);
    cache_none(s.world);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(s.world, mover, Position, {0.5f * (float)(i + 1), 3, 13});
        cache_one(s.world, CACHE_POINT);
        cache_none(s.world);
    }

    cache_no_errors(s.world);
    ecs_fini(s.world);
}

/* The alpha of the slabs, 0.5, passes a cutoff of 0.3 and not one of 0.6 */
void ShadowCache_cutoff_change_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    ecs_set(s.world, s.slab_a, Opacity, {0.5f});
    ecs_set(s.world, s.slab_a, AlphaCutoff, {0.3f});
    ecs_entity_t prefab = ecs_new_w_id(s.world, EcsPrefab);
    ecs_set(s.world, prefab, AlphaCutoff, {0.3f});
    ecs_add_pair(s.world, s.slab_b, EcsIsA, prefab);
    ecs_set(s.world, s.slab_b, Opacity, {0.5f});
    cache_first(&s);
    cache_none(s.world);

    ecs_set(s.world, s.slab_a, AlphaCutoff, {0.6f});
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    ecs_set(s.world, prefab, AlphaCutoff, {0.6f});
    cache_one(s.world, CACHE_SPOT_B);
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

/** Write an 8x8 white image whose left half has no alpha, or none of it when it is opaque.
 *
 * @param path Path of the PNG
 * @param opaque Whether every pixel has full alpha
 * @param mtime Modification time it gets, so the file watch sees each write
 */
static void cache_halves(
    const char *path,
    bool opaque,
    int64_t mtime)
{
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = opaque || (i % 8) >= 4 ? 255 : 0;
    }

    test_assert(stbi_write_png(path, 8, 8, 4, px, 8 * 4) != 0);
    struct timespec times[2] = {{ .tv_sec = (time_t)mtime }, { .tv_sec = (time_t)mtime }};
    test_int(utimensat(AT_FDCWD, path, times, 0), 0);
}

/* The albedo image of an alpha tested quad under spot light A loads again with other holes, under
 * the same handle */
void ShadowCache_texture_reload_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    box_render_gpu_stats_t before, after;
    int32_t frames = 0;
    cache_scene(&s);
    ecs_singleton_set(s.world, FileWatch, {.interval = 0});
    char *png = flecs_asprintf("%s/cache_halves.png",
        ecs_singleton_get(s.world, AppStatus)->tmp_dir);
    cache_halves(png, false, 1000000000);
    ecs_entity_t tex = ecs_new(s.world);
    ecs_set(s.world, tex, TexturePath, {.path = png});
    ecs_entity_t quad = ecs_new(s.world);
    ecs_set(s.world, quad, Position, {-8, 4, 0});
    ecs_set(s.world, quad, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(s.world, quad, Quad, {3, 3});
    ecs_set(s.world, quad, AlphaCutoff, {0.5f});
    ecs_set(s.world, quad, PbrTextures, {.albedo = tex});
    cache_first(&s);
    while (frames < 100 && ecs_get(s.world, tex, TextureStatus)->state == TextureLoading) {
        cache_frame(s.world);
        frames ++;
    }

    test_int(ecs_get(s.world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(s.world, tex, TextureStatus)->revision, 1);
    cache_frame(s.world);
    cache_none(s.world);

    cache_halves(png, true, 1000000100);
    test_assert(box_render_gpu_stats(s.world, &before));
    for (frames = 0; frames < 100 &&
        ecs_get(s.world, tex, TextureStatus)->revision < 2; frames ++)
    {
        cache_frame(s.world);
    }

    test_int(ecs_get(s.world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(s.world, tex, TextureStatus)->revision, 2);
    cache_frame(s.world);
    test_assert(box_render_gpu_stats(s.world, &after));
    test_assert(after.shadow_tiles - before.shadow_tiles >= 1);
    cache_none(s.world);

    unsigned char *cached = cache_capture(s.world, "cache_reload.png");
    test_bool(box_render_shadow_cache(s.world, false), true);
    unsigned char *full = cache_capture(s.world, "cache_reload_rendered.png");
    test_assert(memcmp(cached, full, CACHE_SIZE * CACHE_SIZE * 4) == 0);
    stbi_image_free(cached);
    stbi_image_free(full);
    cache_no_errors(s.world);
    ecs_fini(s.world);
    ecs_os_free(png);
}

/* The color and the intensity of a light are not in its tile */
void ShadowCache_light_change_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    cache_first(&s);
    cache_none(s.world);

    ecs_set(s.world, s.spot_a, Position, {-8, 6, 0.5f});
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    ecs_set(s.world, s.spot_b, Rotation, {(float)GLM_PI_2 - 0.1f, 0, 0});
    cache_one(s.world, CACHE_SPOT_B);
    cache_none(s.world);

    ecs_set(s.world, s.point, Position, {0, 6.5f, 16});
    cache_one(s.world, CACHE_POINT);
    cache_none(s.world);

    SpotLight *spot = ecs_get_mut(s.world, s.spot_a, SpotLight);
    spot->outer_angle = 25;
    ecs_modified(s.world, s.spot_a, SpotLight);
    cache_one(s.world, CACHE_SPOT_A);
    cache_none(s.world);

    spot = ecs_get_mut(s.world, s.spot_b, SpotLight);
    spot->intensity = 30 * GLM_PIf;
    ecs_modified(s.world, s.spot_b, SpotLight);
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

/* Spot light B, then spot light A becomes the nearest; the tiles keep their size and place */
void ShadowCache_camera_move_renders_none(void) {
    // lighting/shadows
    cache_scene_t s;
    int32_t i;
    cache_scene(&s);
    cache_first(&s);
    cache_none(s.world);

    for (i = 0; i < 2; i ++) {
        ecs_set(s.world, s.camera, Position, {i ? -10.0f : 10.0f, 14, 6});
        cache_none(s.world);
    }

    cache_no_errors(s.world);
    ecs_fini(s.world);
}

void ShadowCache_atlas_resolution_renders_all(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    cache_first(&s);
    cache_none(s.world);

    ecs_singleton_set(s.world, ShadowAtlas, {.resolution = 1024, .min_tile = 64});
    cache_frame_t f = cache_frame(s.world);
    test_int(f.rendered, CACHE_TILES);
    test_int(f.reused, 0);
    test_int(f.passes, 1);
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

void ShadowCache_reused_equals_rendered(void) {
    // lighting/shadows
    cache_scene_t s;
    box_render_gpu_stats_t before, after;
    cache_scene(&s);
    ecs_entity_t mover = cache_slab(s.world, (vec3){0, 3, 13});
    ecs_add(s.world, mover, DynamicTransform);
    cache_first(&s);
    cache_none(s.world);

    ecs_set(s.world, s.slab_a, Position, {-8, 3, 1});
    ecs_set(s.world, mover, Position, {1, 3, 13});
    test_assert(box_render_gpu_stats(s.world, &before));
    unsigned char *cached = cache_capture(s.world, "cache_reused.png");
    test_assert(box_render_gpu_stats(s.world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 2);
    test_int(after.shadow_reused - before.shadow_reused, 1);

    test_bool(box_render_shadow_cache(s.world, false), true);
    test_assert(box_render_gpu_stats(s.world, &before));
    unsigned char *full = cache_capture(s.world, "cache_rendered.png");
    test_assert(box_render_gpu_stats(s.world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, CACHE_TILES);
    test_int(after.shadow_reused - before.shadow_reused, 0);
    test_assert(memcmp(cached, full, CACHE_SIZE * CACHE_SIZE * 4) == 0);
    stbi_image_free(cached);
    stbi_image_free(full);

    test_bool(box_render_shadow_cache(s.world, true), true);
    cache_none(s.world);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

/* Slab A transitions its Position under spot light A, then is written every frame, which
 * restarts its transition every frame; the tiles of spot light B and the point light stay */
void ShadowCache_transition_position_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    int32_t i;
    cache_scene(&s);
    box_transition_set(s.world, s.slab_a, ecs_id(Position), &(TransitionSpec){
        .duration = 1.0f });
    cache_first(&s);
    cache_none_at(s.world, CACHE_DELTA);

    ecs_set(s.world, s.slab_a, Position, {-8, 3, 1});
    cache_transition(s.world, CACHE_SPOT_A);

    for (i = 0; i < 3; i ++) {
        ecs_set(s.world, s.slab_a, Position, {-8, 3, 1 - 0.3f * (float)(i + 1)});
        cache_one_at(s.world, CACHE_SPOT_A, CACHE_DELTA);
    }

    for (i = 0; i < 4; i ++) {
        cache_one_at(s.world, CACHE_SPOT_A, CACHE_DELTA);
    }

    cache_none_at(s.world, CACHE_DELTA);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}

/* Slab B turns about y under spot light B, then the slab under the point light grows */
void ShadowCache_transition_rotation_scale_renders_its_tile(void) {
    // lighting/shadows
    cache_scene_t s;
    cache_scene(&s);
    ecs_set(s.world, s.slab_b, Rotation, {0, 0, 0});
    ecs_set(s.world, s.slab_point, Scale, {1, 1, 1});
    box_transition_set(s.world, s.slab_b, ecs_id(Rotation), &(TransitionSpec){
        .duration = 1.0f });
    box_transition_set(s.world, s.slab_point, ecs_id(Scale), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    cache_first(&s);
    cache_none_at(s.world, CACHE_DELTA);

    ecs_set(s.world, s.slab_b, Rotation, {0, 0.6f, 0});
    cache_transition(s.world, CACHE_SPOT_B);

    ecs_set(s.world, s.slab_point, Scale, {1.5f, 1, 1.5f});
    cache_transition(s.world, CACHE_POINT);
    cache_no_errors(s.world);
    ecs_fini(s.world);
}
