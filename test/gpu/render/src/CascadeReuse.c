/**
 * @file CascadeReuse.c
 * @brief Tests that a cascade with a ShadowSettings.interval renders once every interval frames,
 * staggered by its index, that every cascade renders at once when what it is fitted to changes,
 * and that a kept cascade is sampled with the matrix it was rendered with, with the GPU backend.
 *
 * The camera looks straight down from 10 units above a white floor lit by a sun that shines
 * straight down, into a 64x64 image: with the camera above x = 0, world x = -3 lands on pixel
 * column 15. A slab 5 units above x = -3, 1.5 units wide, shadows the floor below it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge in pixels of the images */
#define REUSE_SIZE (64)

/** The entities of the scene */
typedef struct reuse_scene_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t camera;    /**< The camera */
    ecs_entity_t view;      /**< The view */
    ecs_entity_t sun;       /**< The directional light */
} reuse_scene_t;

/** What a frame rendered of the cascades of the view */
typedef struct reuse_frame_t {
    uint32_t mask;          /**< Cascades the extract scheduled for the view */
    int32_t scheduled;      /**< RenderStatus.shadow_cascades */
    int32_t kept;           /**< RenderStatus.shadow_cascades_kept */
    int32_t rendered_gpu;   /**< Cascades the backend rendered */
    int32_t kept_gpu;       /**< Cascades the backend kept */
} reuse_frame_t;

/** Create the scene on the main thread without ambient light: a white rough floor, the slab, a
 * sun that shines straight down and a view with shadow settings.
 *
 * @param shadows The shadow settings of the view, NULL for the View defaults
 * @param out Receives the entities
 */
static void reuse_scene(
    const ShadowSettings *shadows,
    reuse_scene_t *out)
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
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});

    out->world = world;
    out->sun = ecs_new(world);
    ecs_set(world, out->sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, out->sun, DirectionalLight, {.intensity = GLM_PIf});
    out->camera = ecs_new(world);
    ecs_set(world, out->camera, Position, {0, 10, 0});
    ecs_set(world, out->camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, out->camera, Camera, {.fov = 60, .near_ = 0.1f});
    out->view = ecs_new(world);
    ecs_add(world, out->view, View);
    View *v = ecs_get_mut(world, out->view, View);
    v->camera = out->camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    if (shadows) {
        v->shadows = *shadows;
    }

    ecs_modified(world, out->view, View);
}

/** Render a frame into an image file, or without an image, and read what it rendered of the
 * cascades of the view.
 *
 * @param s The scene
 * @param file Image file in the temporary folder, NULL for none
 * @param px Receives the RGBA pixels when file is set, freed with stbi_image_free
 * @return What the frame rendered
 */
static reuse_frame_t reuse_frame_to(
    const reuse_scene_t *s,
    const char *file,
    unsigned char **px)
{
    box_render_gpu_stats_t before, after;
    ecs_world_t *world = s->world;
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    if (file || !ecs_has(world, e, Surface)) {
        ecs_set(world, e, Surface, {.width = REUSE_SIZE, .height = REUSE_SIZE,
            .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    }

    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.errors, 0);

    if (file) {
        char *path = flecs_asprintf("%s/%s",
            ecs_singleton_get(world, AppStatus)->tmp_dir, file);
        int w = 0, h = 0, n = 0;
        *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(*px != NULL);
        test_int(w, REUSE_SIZE);
        test_int(h, REUSE_SIZE);
        ecs_os_free(path);
    }

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_assert(status != NULL);

    return (reuse_frame_t){
        .mask = box_render_shadow_rendered(world, s->view),
        .scheduled = status->shadow_cascades,
        .kept = status->shadow_cascades_kept,
        .rendered_gpu = after.shadow_cascades - before.shadow_cascades,
        .kept_gpu = after.shadow_cascades_kept - before.shadow_cascades_kept
    };
}

/** Render a frame without an image and read what it rendered of the cascades of the view.
 *
 * @param s The scene
 * @return What the frame rendered
 */
static reuse_frame_t reuse_frame(
    const reuse_scene_t *s)
{
    return reuse_frame_to(s, NULL, NULL);
}

/** Count the bits of a mask.
 *
 * @param mask The mask
 * @return The bits that are set
 */
static int32_t reuse_bits(
    uint32_t mask)
{
    int32_t result = 0;
    while (mask) {
        result += (int32_t)(mask & 1u);
        mask >>= 1;
    }

    return result;
}

/** Render a frame and check the cascades it rendered and kept, in the extract and the backend.
 *
 * @param s The scene
 * @param cascades Cascades of the view
 * @param mask The cascades the frame is to render
 */
static void reuse_expect(
    const reuse_scene_t *s,
    int32_t cascades,
    uint32_t mask)
{
    reuse_frame_t f = reuse_frame(s);
    test_uint(f.mask, mask);
    test_int(f.scheduled, reuse_bits(mask));
    test_int(f.kept, cascades - reuse_bits(mask));
    test_int(f.rendered_gpu, reuse_bits(mask));
    test_int(f.kept_gpu, cascades - reuse_bits(mask));
}

/** Shadow settings with two cascades and intervals.
 *
 * @param first Interval of the first cascade
 * @param second Interval of the second cascade
 * @return The settings
 */
static ShadowSettings reuse_settings(
    int32_t first,
    int32_t second)
{
    return (ShadowSettings){.enabled = true, .cascades = 2, .resolution = 512,
        .max_distance = 30, .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 1.5f,
        .filter_size = 3, .interval = {first, second}};
}

void CascadeReuse_default_renders_every_cascade(void) {
    // lighting/shadows
    reuse_scene_t s;
    int32_t i;
    reuse_scene(NULL, &s);
    for (i = 0; i < 4; i ++) {
        reuse_expect(&s, 4, 0xFu);
    }

    ecs_fini(s.world);
}

void CascadeReuse_zero_interval_renders_every_frame(void) {
    // lighting/shadows
    reuse_scene_t s;
    int32_t i;
    ShadowSettings settings = reuse_settings(0, 0);
    reuse_scene(&settings, &s);
    for (i = 0; i < 4; i ++) {
        reuse_expect(&s, 2, 0x3u);
    }

    ecs_fini(s.world);
}

void CascadeReuse_interval_renders_every_n_frames(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(1, 3);
    reuse_scene(&settings, &s);

    // the first frame renders both, then cascade 1 renders when frames + 1 is a multiple of 3
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x1u);

    ecs_fini(s.world);
}

void CascadeReuse_equal_intervals_staggered(void) {
    // lighting/shadows
    reuse_scene_t s;
    int32_t i;
    reuse_scene(&(ShadowSettings){.enabled = true, .cascades = 4, .resolution = 512,
        .max_distance = 30, .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 1.5f,
        .filter_size = 3, .interval = {2, 2, 2, 2}}, &s);
    reuse_expect(&s, 4, 0xFu);
    for (i = 0; i < 3; i ++) {
        reuse_expect(&s, 4, 0xAu);
        reuse_expect(&s, 4, 0x5u);
    }

    ecs_fini(s.world);
}

void CascadeReuse_light_direction_renders_all(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(4, 4);
    reuse_scene(&settings, &s);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);
    reuse_expect(&s, 2, 0x0u);

    ecs_set(s.world, s.sun, Rotation, {(float)GLM_PI_2 - 0.1f, 0, 0});
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    ecs_fini(s.world);
}

void CascadeReuse_settings_change_renders_all(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(4, 4);
    reuse_scene(&settings, &s);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    View *v = ecs_get_mut(s.world, s.view, View);
    v->shadows.max_distance = 40;
    ecs_modified(s.world, s.view, View);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    v = ecs_get_mut(s.world, s.view, View);
    v->shadows.split_lambda = 0.5f;
    ecs_modified(s.world, s.view, View);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    v = ecs_get_mut(s.world, s.view, View);
    v->shadows.resolution = 256;
    ecs_modified(s.world, s.view, View);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    ecs_fini(s.world);
}

void CascadeReuse_projection_change_renders_all(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(4, 4);
    reuse_scene(&settings, &s);
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    ecs_set(s.world, s.camera, Camera, {.fov = 50, .near_ = 0.1f});
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);

    ecs_fini(s.world);
}

void CascadeReuse_camera_move_keeps_cascades(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(4, 4);
    reuse_scene(&settings, &s);
    reuse_expect(&s, 2, 0x3u);

    ecs_set(s.world, s.camera, Position, {1, 10, 0});
    reuse_expect(&s, 2, 0x0u);
    ecs_set(s.world, s.camera, Position, {2, 10, 0});
    reuse_expect(&s, 2, 0x0u);

    ecs_fini(s.world);
}

void CascadeReuse_kept_cascade_sampled_with_its_matrix(void) {
    // lighting/shadows
    reuse_scene_t s;
    unsigned char *px = NULL;
    reuse_scene(&(ShadowSettings){.enabled = true, .cascades = 1, .resolution = 1024,
        .max_distance = 30, .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 1.5f,
        .filter_size = 3, .interval = {4}}, &s);

    // with the camera above x = 0 the shadow of the slab is below it, on column 15
    reuse_frame_t f = reuse_frame_to(&s, "reuse_before.png", &px);
    test_uint(f.mask, 0x1u);
    test_int(f.rendered_gpu, 1);
    test_assert(px[(32 * REUSE_SIZE + 48) * 4] > 200);
    test_assert(px[(32 * REUSE_SIZE + 15) * 4] < 30);
    stbi_image_free(px);

    // the camera moves 2 units along +x: the slab at x = -3 lands on column 4 and column 15
    // shows x = -1. The kept cascade still shadows x = -3, where the slab is, not column 15
    ecs_set(s.world, s.camera, Position, {2, 10, 0});
    f = reuse_frame_to(&s, "reuse_after.png", &px);
    test_uint(f.mask, 0x0u);
    test_int(f.rendered_gpu, 0);
    test_int(f.kept_gpu, 1);
    test_assert(px[(32 * REUSE_SIZE + 4) * 4] < 30);
    test_assert(px[(32 * REUSE_SIZE + 15) * 4] > 200);
    test_assert(px[(32 * REUSE_SIZE + 48) * 4] > 200);
    stbi_image_free(px);

    ecs_fini(s.world);
}

void CascadeReuse_enabled_interval_renders_all_first(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(1, 1);
    reuse_scene(&settings, &s);
    reuse_expect(&s, 2, 0x3u);

    // turning an interval on renders every cascade into the new map of the view, then keeps
    View *v = ecs_get_mut(s.world, s.view, View);
    v->shadows.interval[1] = 2;
    ecs_modified(s.world, s.view, View);
    reuse_frame_t f = reuse_frame(&s);
    test_int(f.rendered_gpu, 2);
    test_int(f.kept_gpu, 0);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x3u);

    ecs_fini(s.world);
}

void CascadeReuse_kept_cascade_not_overwritten_by_other_view(void) {
    // lighting/shadows
    reuse_scene_t s;
    unsigned char *px = NULL;
    int32_t i;
    reuse_scene(&(ShadowSettings){.enabled = true, .cascades = 1, .resolution = 1024,
        .max_distance = 30, .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 1.5f,
        .filter_size = 3, .interval = {4}}, &s);
    View *v = ecs_get_mut(s.world, s.view, View);
    v->order = 1;
    ecs_modified(s.world, s.view, View);

    // a view with shadows renders before it every frame, far from every caster, so the cascades
    // it renders hold no caster
    ecs_entity_t camera = ecs_new(s.world);
    ecs_set(s.world, camera, Position, {200, 10, 0});
    ecs_set(s.world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(s.world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t other = ecs_new(s.world);
    ecs_add(s.world, other, View);
    v = ecs_get_mut(s.world, other, View);
    v->camera = camera;
    v->shadows.enabled = true;
    ecs_modified(s.world, other, View);

    // the kept cascade of the view still holds the slab
    for (i = 0; i < 4; i ++) {
        reuse_frame_t f = reuse_frame_to(&s, "reuse_other.png", &px);
        test_uint(f.mask, i ? 0x0u : 0x1u);
        test_assert(px[(32 * REUSE_SIZE + 48) * 4] > 200);
        test_assert(px[(32 * REUSE_SIZE + 15) * 4] < 30);
        stbi_image_free(px);
    }

    ecs_fini(s.world);
}

/** Add a pole that sways in a wind that blows, with the wind feature of the scene, so the
 * cascades of the scene cap their intervals at their ShadowSettings.sway_interval.
 *
 * @param s The scene
 */
static void reuse_sway(
    const reuse_scene_t *s)
{
    ecs_singleton_set(s->world, Scene, {.shader_features.Extended.wind = true});
    ecs_entity_t e = ecs_new(s->world);
    ecs_set(s->world, e, Position, {3, 1.5f, 0});
    ecs_set(s->world, e, Box, {0.3f, 3, 0.3f});
    ecs_set(s->world, e, WindSway, {.bend = 0.05f, .height = 3, .base = 0});
    ecs_singleton_set(s->world, Wind, {.direction = {1, 0}, .speed = 10, .gust = 0,
        .gust_scale = 20});
}

void CascadeReuse_sway_renders_every_frame(void) {
    // lighting/shadows
    reuse_scene_t s;
    int32_t i;
    ShadowSettings settings = reuse_settings(4, 4);
    reuse_scene(&settings, &s);
    reuse_sway(&s);
    for (i = 0; i < 4; i ++) {
        reuse_expect(&s, 2, 0x3u);
    }

    ecs_fini(s.world);
}

void CascadeReuse_sway_interval_per_cascade(void) {
    // lighting/shadows
    reuse_scene_t s;
    ShadowSettings settings = reuse_settings(4, 4);
    settings.sway_interval[0] = 2;
    settings.sway_interval[1] = 8;
    reuse_scene(&settings, &s);
    reuse_sway(&s);

    // cascade 0 renders every other frame, cascade 1 keeps its interval of 4 since its cap is
    // above it: it renders when frames + 1 is a multiple of 4
    reuse_expect(&s, 2, 0x3u);
    reuse_expect(&s, 2, 0x0u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x2u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x0u);
    reuse_expect(&s, 2, 0x1u);
    reuse_expect(&s, 2, 0x2u);

    ecs_fini(s.world);
}
