/**
 * @file DepthPrepass.c
 * @brief Tests for the depth prepass of lit views: its counters and the images it shades.
 *
 * Lit views run without lights and with a black ambient, and their shapes have a black albedo
 * and an emission, so a lit pixel is its emission as an unlit one is. The same scene seen by an
 * unlit view, which draws without a prepass, then renders the image the prepass must keep.
 * Boxes share the unit box mesh, so the boxes of a material state are one batch.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a headless world that renders on the main thread, with no light but a point light of
 * intensity 0 out of reach (which replaces the default sun) and a black ambient.
 *
 * @return The world
 */
static ecs_world_t* prepass_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    return world;
}

/** Add a view of a camera.
 *
 * @param world The world
 * @param camera The camera
 * @param root Parent of the view, the view_root of its surface, 0 for none
 * @param lit Whether the view is lit
 * @param occlusion Whether the view culls by occlusion, else it draws every instance
 * @return The view
 */
static ecs_entity_t prepass_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t root,
    bool lit,
    bool occlusion)
{
    ecs_entity_t view = root ? ecs_new_w_pair(world, EcsChildOf, root) : ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = lit, .depth_prepass = true},
        .culling = {.enabled = occlusion, .occlusion = occlusion, .min_dimension_px = 1}});

    return view;
}

/** Add a surface that renders the views under a root into a PNG.
 *
 * @param world The world
 * @param root The view root, 0 for the unclaimed views
 * @param width Width in pixels
 * @param height Height in pixels
 * @param file Name of the PNG in the temporary directory, NULL for none
 * @return The surface
 */
static ecs_entity_t prepass_surface(
    ecs_world_t *world,
    ecs_entity_t root,
    int32_t width,
    int32_t height,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = width, .height = height, .view_root = root,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return e;
}

/** Add a shape of black albedo that shows its emission in a lit view without light.
 *
 * @param world The world
 * @param x X of the shape
 * @param y Y of the shape
 * @param z Z of the shape
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 * @return The shape, without a geometry component
 */
static ecs_entity_t prepass_shape(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {r, g, b}});

    return e;
}

/** Add opaque shapes in a batch each: polygons of distinct side counts in front of the camera.
 *
 * @param world The world
 * @param count Batches
 */
static void prepass_batches(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = prepass_shape(world, (float)i * 2 - (float)count, 0, -10,
            0, 255, 0);
        ecs_set(world, e, NGon, {.sides = 3 + i, .radius = 0.8f});
    }
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters
 */
static box_render_gpu_stats_t prepass_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t stats;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);

    return stats;
}

/** Load a capture of the temporary directory.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param width Expected width
 * @param height Expected height
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* prepass_load(
    ecs_world_t *world,
    const char *file,
    int32_t width,
    int32_t height)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, width);
    test_int(h, height);
    ecs_os_free(path);

    return px;
}

/** Compare the captures of a lit and an unlit view of the same frame.
 *
 * @param world The world
 * @param lit Name of the PNG of the lit view
 * @param unlit Name of the PNG of the unlit view
 * @param width Width of both
 * @param height Height of both
 * @return Pixels that are not black in the unlit capture
 */
static int32_t prepass_compare(
    ecs_world_t *world,
    const char *lit,
    const char *unlit,
    int32_t width,
    int32_t height)
{
    unsigned char *a = prepass_load(world, lit, width, height);
    unsigned char *b = prepass_load(world, unlit, width, height);
    int32_t i, c, wrong = 0, shown = 0;
    for (i = 0; i < width * height; i ++) {
        bool differs = false;
        for (c = 0; c < 3; c ++) {
            differs |= abs((int)a[i * 4 + c] - (int)b[i * 4 + c]) > 1;
        }

        if (differs && wrong < 8) {
            printf("pixel %d, %d differs: %d %d %d lit, %d %d %d unlit\n",
                i % width, i / width, a[i * 4], a[i * 4 + 1], a[i * 4 + 2],
                b[i * 4], b[i * 4 + 1], b[i * 4 + 2]);
        }

        wrong += differs;
        shown += b[i * 4] > 8 || b[i * 4 + 1] > 8 || b[i * 4 + 2] > 8;
    }

    test_int(wrong, 0);
    stbi_image_free(a);
    stbi_image_free(b);

    return shown;
}

void DepthPrepass_lit_view_counts_prepass(void) {
    // render_quality/depth_prepass
    ecs_world_t *world = prepass_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    prepass_view(world, camera, 0, true, false);
    prepass_batches(world, 4);
    prepass_surface(world, 0, 64, 64, NULL);

    box_render_gpu_stats_t before = prepass_frames(world, 3);
    box_render_gpu_stats_t after = prepass_frames(world, 1);
    box_render_draws_stats_t draws;
    box_render_draws_stats(world, &draws);
    test_int(draws.batches, 4);
    test_int(after.errors, 0);
    test_int(after.prepasses - before.prepasses, 1);
    test_int(after.prepass_draws - before.prepass_draws, 4);
    test_assert(after.draws - before.draws >= 4);

    before = after;
    after = prepass_frames(world, 2);
    test_int(after.prepasses - before.prepasses, 2);
    test_int(after.prepass_draws - before.prepass_draws, 8);

    ecs_fini(world);
}

void DepthPrepass_unlit_view_has_no_prepass(void) {
    // render_quality/depth_prepass
    ecs_world_t *world = prepass_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    prepass_view(world, camera, 0, false, false);
    prepass_batches(world, 4);
    prepass_surface(world, 0, 64, 64, NULL);

    box_render_gpu_stats_t before = prepass_frames(world, 3);
    box_render_gpu_stats_t after = prepass_frames(world, 2);
    test_int(after.errors, 0);
    test_int(after.prepasses, 0);
    test_int(after.prepass_draws, 0);
    test_int(after.prepasses - before.prepasses, 0);
    test_int(after.prepass_draws - before.prepass_draws, 0);
    test_assert(after.draws - before.draws >= 8);

    ecs_fini(world);
}

void DepthPrepass_occlusion_two_prepasses(void) {
    // render_quality/depth_prepass
    ecs_world_t *world = prepass_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = prepass_view(world, camera, 0, true, true);
    ecs_entity_t wall = prepass_shape(world, 0, 0, -10, 0, 255, 0);
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_entity_t hidden = prepass_shape(world, 0, 0, -20, 255, 0, 0);
    ecs_set(world, hidden, Box, {2, 2, 2});
    prepass_surface(world, 0, 64, 64, "occlusion.png");

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    int32_t frames = 0;
    while ((!status || status->frame < start) && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
        frames ++;
    }

    test_assert(status != NULL);
    test_assert(status->frame >= start);
    test_int(status->occlusion_culled, 1);

    box_render_gpu_stats_t before = prepass_frames(world, 0);
    box_render_gpu_stats_t after = prepass_frames(world, 1);
    test_int(after.errors, 0);
    test_int(after.prepasses - before.prepasses, 2);
    test_int(after.prepass_draws - before.prepass_draws, 2);
    unsigned char *px = prepass_load(world, "occlusion.png", 64, 64);
    test_assert(px[(32 * 64 + 32) * 4] < 20);
    test_assert(px[(32 * 64 + 32) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_set(world, wall, Position, {100, 0, -10});
    before = after;
    after = prepass_frames(world, 1);
    test_int(after.prepasses - before.prepasses, 2);
    px = prepass_load(world, "occlusion.png", 64, 64);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4 + 1] < 50);
    stbi_image_free(px);

    ecs_fini(world);
}

/** Render a scene of many instances, intersecting and nearly coplanar shapes, far and small
 * shapes, non uniform scales and a moving shape into a lit and an unlit view of one camera, and
 * check that the two images match.
 *
 * @param occlusion Whether both views cull by occlusion
 */
static void prepass_equal_scene(
    bool occlusion)
{
    ecs_world_t *world = prepass_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t lit_root = ecs_new(world), unlit_root = ecs_new(world);
    prepass_view(world, camera, lit_root, true, occlusion);
    prepass_view(world, camera, unlit_root, false, occlusion);
    int32_t i;
    for (i = 0; i < 160; i ++) {
        ecs_entity_t e = prepass_shape(world, (float)(i % 16) - 7.5f, (float)(i / 16) * 0.9f - 4,
            -12 - (float)(i % 3) * 0.3f, (uint8_t)(40 + (i * 37) % 200),
            (uint8_t)(40 + (i * 71) % 200), (uint8_t)(40 + (i * 13) % 200));
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, e, Rotation, {(float)i * 0.3f, (float)i * 0.7f, (float)i * 0.11f});
        if (i % 5 == 0) {
            ecs_set(world, e, Scale, {1.6f, 0.4f, 2.5f});
        }
    }

    ecs_entity_t sphere = prepass_shape(world, -1.5f, 0.5f, -6, 255, 255, 0);
    ecs_set(world, sphere, Sphere, {.segments = 32, .smooth = true, .radius = 1.2f});
    ecs_entity_t crossing = prepass_shape(world, -1, 0.8f, -6, 0, 255, 255);
    ecs_set(world, crossing, Box, {2, 2, 2});
    ecs_set(world, crossing, Rotation, {0.4f, 0.6f, 0.2f});
    ecs_entity_t near_ = prepass_shape(world, 2, -1, -5, 255, 0, 255);
    ecs_set(world, near_, Quad, {3, 3});
    ecs_entity_t coplanar = prepass_shape(world, 2.8f, -0.6f, -5.01f, 255, 128, 0);
    ecs_set(world, coplanar, Quad, {3, 3});
    ecs_entity_t far_ = prepass_shape(world, 120, 80, -800, 255, 255, 255);
    ecs_set(world, far_, Box, {40, 40, 40});
    ecs_entity_t small = prepass_shape(world, -0.4f, -0.3f, -3, 255, 255, 255);
    ecs_set(world, small, Box, {0.02f, 0.02f, 0.02f});
    ecs_entity_t stretched = prepass_shape(world, 3, 2, -8, 0, 128, 255);
    ecs_set(world, stretched, Sphere, {.segments = 24, .radius = 1});
    ecs_set(world, stretched, Scale, {2.5f, 0.3f, 1.2f});
    ecs_set(world, stretched, Rotation, {0.3f, 0.2f, 0.9f});
    ecs_entity_t mover = prepass_shape(world, 0, 0, -7, 128, 255, 128);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Box, {1.5f, 1.5f, 1.5f});
    prepass_surface(world, lit_root, 128, 96, "equal_lit.png");
    prepass_surface(world, unlit_root, 128, 96, "equal_unlit.png");

    box_render_gpu_stats_t before = prepass_frames(world, 0);
    for (i = 0; i < 12; i ++) {
        ecs_set(world, mover, Position, {sinf((float)i * 0.7f) * 3, cosf((float)i) * 1.5f,
            -7 + sinf((float)i * 0.3f)});
        ecs_set(world, mover, Rotation, {(float)i * 0.2f, (float)i * 0.35f, 0});
        prepass_frames(world, 1);
    }

    box_render_gpu_stats_t after = prepass_frames(world, 0);
    test_int(after.errors, 0);
    test_assert(after.prepasses - before.prepasses >= 12);
    test_assert(prepass_compare(world, "equal_lit.png", "equal_unlit.png", 128, 96) > 2000);

    ecs_fini(world);
}

void DepthPrepass_equal_matches_unlit(void) {
    // render_quality/depth_prepass
    prepass_equal_scene(false);
    prepass_equal_scene(true);
}

void DepthPrepass_cutout_prepass_holes(void) {
    // render_quality/depth_prepass
    ecs_world_t *world = prepass_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    prepass_view(world, camera, 0, true, false);
    ecs_entity_t wall = prepass_shape(world, 0, 0, -1, 255, 0, 0);
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_entity_t behind = prepass_shape(world, -1, 0, -0.5f, 0, 0, 255);
    ecs_set(world, behind, Box, {1, 1, 0.1f});

    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = (i % 8) >= 4 ? 255 : 0;
    }

    char *file = flecs_asprintf("%s/prepass_halves.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t quad = prepass_shape(world, 0, 0, 0, 0, 255, 0);
    ecs_set(world, quad, Quad, {4, 4});
    ecs_set(world, quad, PbrTextures, {.albedo = tex});
    ecs_set(world, quad, AlphaCutoff, {0.5f});
    prepass_surface(world, 0, 64, 32, "prepass_cutout.png");

    int32_t frames = 0;
    while (frames < 100 && ecs_get(world, tex, TextureStatus)->state == TextureLoading) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_assert(ecs_get(world, tex, TextureStatus)->state == TextureReady);
    box_render_gpu_stats_t before = prepass_frames(world, 1);
    box_render_gpu_stats_t after = prepass_frames(world, 1);
    test_int(after.errors, 0);
    test_int(after.prepasses - before.prepasses, 1);
    test_int(after.prepass_draws - before.prepass_draws, 2);

    unsigned char *img = prepass_load(world, "prepass_cutout.png", 64, 32);
    const unsigned char *hole = &img[(16 * 64 + 28) * 4];
    const unsigned char *solid = &img[(16 * 64 + 36) * 4];
    const unsigned char *open = &img[(10 * 64 + 26) * 4];
    test_assert(hole[0] < 20 && hole[1] < 20 && hole[2] > 240);
    test_assert(solid[0] < 20 && solid[1] > 240 && solid[2] < 20);
    test_assert(open[0] > 240 && open[1] < 20 && open[2] < 20);
    test_assert(img[(16 * 64 + 20) * 4] > 240);

    stbi_image_free(img);
    ecs_fini(world);
    ecs_os_free(file);
}

void DepthPrepass_blended_and_2d_after_prepass(void) {
    // render_quality/depth_prepass
    ecs_world_t *world = prepass_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t lit_root = ecs_new(world), unlit_root = ecs_new(world);
    prepass_view(world, camera, lit_root, true, false);
    prepass_view(world, camera, unlit_root, false, false);
    ecs_entity_t wall = prepass_shape(world, 0, 0, -2, 255, 0, 0);
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_entity_t pillar = prepass_shape(world, 1.5f, 0, -1, 0, 255, 0);
    ecs_set(world, pillar, Box, {1, 4, 1});
    ecs_entity_t glass = prepass_shape(world, 0.5f, 0, 0, 0, 0, 255);
    ecs_set(world, glass, Box, {3, 2, 0.2f});
    ecs_set(world, glass, Opacity, {0.5f});
    ecs_entity_t veil = prepass_shape(world, -2, -0.5f, 0.5f, 255, 255, 255);
    ecs_set(world, veil, Sphere, {.segments = 24, .radius = 0.6f});
    ecs_set(world, veil, BlendMode, {Additive});
    ecs_set(world, veil, Opacity, {0.4f});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {-2, 1, -1.5f});
    ecs_set(world, rect, Rect, {1.5f, 1});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t covered = ecs_new(world);
    ecs_set(world, covered, Position, {1.5f, 0, -1.8f});
    ecs_set(world, covered, Circle, {0.4f});
    ecs_set(world, covered, Rgb, {255, 255, 0});
    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {1.5f, -1, 1});
    ecs_set(world, front, Circle, {0.3f});
    ecs_set(world, front, Rgb, {255, 255, 0});
    prepass_surface(world, lit_root, 64, 32, "blended_lit.png");
    prepass_surface(world, unlit_root, 64, 32, "blended_unlit.png");

    box_render_gpu_stats_t before = prepass_frames(world, 2);
    box_render_gpu_stats_t after = prepass_frames(world, 1);
    test_int(after.errors, 0);
    test_int(after.prepasses - before.prepasses, 1);
    test_int(after.prepass_draws - before.prepass_draws, 1);
    test_assert(prepass_compare(world, "blended_lit.png", "blended_unlit.png", 64, 32) > 500);

    unsigned char *img = prepass_load(world, "blended_lit.png", 64, 32);
    const unsigned char *blended = &img[(16 * 64 + 31) * 4];
    test_assert(blended[0] > 60 && blended[2] > 60 && blended[1] < 20);
    stbi_image_free(img);

    ecs_fini(world);
}
