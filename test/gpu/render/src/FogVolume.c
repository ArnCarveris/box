/**
 * @file FogVolume.c
 * @brief Tests for the fog volumes a view raymarches in its forward pass: transmittance, scattered
 * light, clipping to the depth, the camera inside and behind, edges, rotation, sun shafts, the
 * phase, the order of overlapping volumes, glass, orthographic cameras, resizes, several views
 * and the captures, which do not see them; checked on PNG captures.
 *
 * Most cases run in a lit view without a sun, from a camera at the origin looking down -Z with a
 * 60 degree field of view, over black walls that show their emission. Volumes have hard edges
 * and white color unless a case sets them.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define FV_SIZE (64)

/** Emission of the walls, display encoded */
#define FV_WALL (200)

/** Create a headless world that renders on the main thread, with a white ambient of an
 * intensity and no directional light.
 *
 * @param ambient Intensity of the white ambient
 * @return The world
 */
static ecs_world_t* fv_world(
    float ambient)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = ambient});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    return world;
}

/** Add a perspective camera at a position, turned about +Y.
 *
 * @param world The world
 * @param z Z of the camera
 * @param yaw Rotation about +Y, 0 looks down -Z
 * @return The camera
 */
static ecs_entity_t fv_camera(
    ecs_world_t *world,
    float z,
    float yaw)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, z});
    ecs_set(world, camera, Rotation, {0, yaw, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});

    return camera;
}

/** Add an orthographic camera at z 10 looking down -Z that shows 16 units, 4 pixels a unit.
 *
 * @param world The world
 * @return The camera
 */
static ecs_entity_t fv_ortho(
    ecs_world_t *world)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 16, .near_ = 0.1f,
        .far_ = 100});

    return camera;
}

/** Add a lit view of a camera that clears to black and draws every instance.
 *
 * @param world The world
 * @param camera The camera
 * @param fog Whether the view renders the fog volumes of its scene
 * @return The view
 */
static ecs_entity_t fv_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    bool fog)
{
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .fog_volumes = fog}});

    return view;
}

/** Add a wall of black albedo that shows its gray emission, facing the camera.
 *
 * @param world The world
 * @param x X of its center
 * @param z Z of its center
 * @param size Width and height
 * @param gray Emission, display encoded
 * @return The wall
 */
static ecs_entity_t fv_wall(
    ecs_world_t *world,
    float x,
    float z,
    float size,
    uint8_t gray)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Box, {size, size, 0.2f});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {gray, gray, gray}});

    return e;
}

/** Add a white fog volume with hard edges and 64 steps.
 *
 * @param world The world
 * @param pos Center
 * @param extent Half size
 * @param density Extinction per unit
 * @param absorption Share of the extinction that is absorbed
 * @return The volume
 */
static ecs_entity_t fv_volume(
    ecs_world_t *world,
    const float pos[3],
    const float extent[3],
    float density,
    float absorption)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {pos[0], pos[1], pos[2]});
    ecs_add(world, e, FogVolume);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    glm_vec3_copy(ECS_CONST_CAST(float*, extent), f->extent);
    f->density = density;
    f->absorption = absorption;
    f->edge_fade = 0;
    f->steps = 64;
    ecs_modified(world, e, FogVolume);

    return e;
}

/** Add a surface that renders the views into a PNG.
 *
 * @param world The world
 * @param width Width in pixels
 * @param height Height in pixels
 * @param file Name of the PNG in the temporary directory
 * @return The surface
 */
static ecs_entity_t fv_surface(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = width, .height = height,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return e;
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters, all 0 before the first frame with a surface
 */
static box_render_gpu_stats_t fv_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t stats;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats) || !count, true);

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
static unsigned char* fv_load(
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

/** Render frames into a square capture and load it, without new device errors.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = fv_frames(world, 0), after;
    fv_surface(world, FV_SIZE, FV_SIZE, file);
    after = fv_frames(world, 3);
    test_int(after.errors, before.errors);

    return fv_load(world, file, FV_SIZE, FV_SIZE);
}

/** Convert a display encoded byte to linear light.
 *
 * @param c The byte
 * @return The linear value
 */
static float fv_lin(
    int32_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Convert linear light to a display encoded byte.
 *
 * @param lin The linear value
 * @return The byte
 */
static int32_t fv_srgb(
    float lin)
{
    lin = lin < 0.0f ? 0.0f : (lin > 1.0f ? 1.0f : lin);
    float v = lin <= 0.0031308f ? lin * 12.92f : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;

    return (int32_t)(v * 255.0f + 0.5f);
}

/** Get a pixel of a square capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The pixel, RGBA
 */
static const unsigned char* fv_px(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * FV_SIZE + x) * 4];
}

/** Check the channels of a gray pixel against an expected value.
 *
 * @param p The pixel
 * @param want Expected value of every channel
 * @param tolerance Largest difference per channel
 */
static void fv_expect(
    const unsigned char *p,
    int32_t want,
    int32_t tolerance)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        if (abs((int32_t)p[c] - want) > tolerance) {
            printf("channel %d: got %d, want %d (pixel %d %d %d)\n", c, p[c], want,
                p[0], p[1], p[2]);
        }

        test_assert(abs((int32_t)p[c] - want) <= tolerance);
    }
}

/** Shadow settings of the views of the sun cases: two cascades over 60 units */
static const ShadowSettings fv_shadows = {
    .enabled = true, .cascades = 2, .resolution = 1024, .max_distance = 60,
    .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 2, .filter_size = 3,
    .fade = 0.2f, .blend = 0.1f
};

/** The view of a world without volumes and the same view with fog_volumes off draw the same
 * pixels in one forward pass per frame. */
void FogVolume_no_volume_no_pass(void) {
    // fog_volume/draw
    const char *files[2] = { "fog_none_on.png", "fog_none_off.png" };
    unsigned char *px[2];
    int32_t passes[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0.3f);
        fv_view(world, fv_camera(world, 0, 0), k == 0);
        fv_wall(world, 0, -20, 40, FV_WALL);
        ecs_entity_t cube = ecs_new(world);
        ecs_set(world, cube, Position, {1, 0, -8});
        ecs_set(world, cube, Box, {2, 2, 2});
        ecs_set(world, cube, Rgb, {180, 90, 40});
        px[k] = fv_render(world, files[k]);
        box_render_gpu_stats_t before = fv_frames(world, 0);
        box_render_gpu_stats_t after = fv_frames(world, 2);
        passes[k] = after.forward_passes - before.forward_passes;
        test_int(after.fog_volumes, 0);
        test_int(after.errors, before.errors);
        ecs_fini(world);
    }

    test_int(passes[0], 2);
    test_int(passes[0], passes[1]);
    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A purely absorbing volume 4 units deep in front of a wall passes exp(-density * 4) of its
 * light; the view ends its pass for the volume, which draw last in one more. */
void FogVolume_density_transmittance(void) {
    // fog_volume/march
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    unsigned char *px = fv_render(world, "fog_transmittance.png");
    int32_t want = fv_srgb(fv_lin(FV_WALL) * expf(-0.2f * 4.0f));
    fv_expect(fv_px(px, 32, 32), want, 4);
    fv_expect(fv_px(px, 40, 24), want, 6);
    stbi_image_free(px);

    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    test_int(after.fog_volumes - before.fog_volumes, 2);
    test_int(after.forward_passes - before.forward_passes, 4);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

/** A very dense volume that scatters all it extinguishes, lit by a white ambient alone, shows
 * the ambient times its color. */
void FogVolume_scatter_albedo(void) {
    // fog_volume/march
    ecs_world_t *world = fv_world(0.5f);
    fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 20, 0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->color = (rgb){255, 128, 64};
    ecs_modified(world, e, FogVolume);
    unsigned char *px = fv_render(world, "fog_albedo.png");
    const unsigned char *p = fv_px(px, 32, 32);
    test_assert(abs((int32_t)p[0] - fv_srgb(0.5f)) <= 4);
    test_assert(abs((int32_t)p[1] - fv_srgb(0.5f * fv_lin(128))) <= 4);
    test_assert(abs((int32_t)p[2] - fv_srgb(0.5f * fv_lin(64))) <= 4);
    stbi_image_free(px);

    ecs_fini(world);
}

/** A box inside the volume is fogged over the part of the volume in front of it, less than a
 * box behind the volume, which is fogged over its whole depth. */
void FogVolume_clips_to_depth(void) {
    // fog_volume/depth
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 5, 4}, 0.3f, 1);
    ecs_entity_t inside = fv_wall(world, -2, -10, 1.5f, FV_WALL);
    ecs_entity_t behind = fv_wall(world, 4, -20, 3, FV_WALL);
    (void)inside;
    (void)behind;
    unsigned char *px = fv_render(world, "fog_depth.png");
    int32_t near_ = fv_px(px, 21, 32)[1], far_ = fv_px(px, 43, 32)[1];
    test_assert(abs(near_ - fv_srgb(fv_lin(FV_WALL) * expf(-0.3f * 3.9f))) <= 6);
    test_assert(abs(far_ - fv_srgb(fv_lin(FV_WALL) * expf(-0.3f * 8.0f))) <= 6);
    test_assert(near_ > far_ + 20);
    test_assert(near_ < FV_WALL - 20);
    stbi_image_free(px);

    ecs_fini(world);
}

/** A camera inside a volume sees it fog the wall over the distance from the camera to the
 * face of the box. */
void FogVolume_camera_inside(void) {
    // fog_volume/draw
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, 0}, (float[]){10, 10, 10}, 0.1f, 1);
    unsigned char *px = fv_render(world, "fog_inside.png");
    fv_expect(fv_px(px, 32, 32), fv_srgb(fv_lin(FV_WALL) * expf(-0.1f * 9.9f)), 5);
    stbi_image_free(px);

    ecs_fini(world);
}

/** A volume behind the camera, whose bounding sphere reaches into the frustum but whose box does
 * not, is culled and draws nothing. */
void FogVolume_not_behind_camera(void) {
    // fog_volume/draw
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, 6}, (float[]){5, 5, 5}, 1, 1);
    unsigned char *px = fv_render(world, "fog_behind.png");
    fv_expect(fv_px(px, 32, 32), FV_WALL, 1);
    fv_expect(fv_px(px, 4, 4), FV_WALL, 1);
    fv_expect(fv_px(px, 60, 60), FV_WALL, 1);
    stbi_image_free(px);
    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 1);
    test_int(after.fog_volumes - before.fog_volumes, 0);

    ecs_fini(world);
}

/** With an edge fade the fog thins smoothly toward the faces of the box: the wall brightens
 * from the center of an orthographic view toward a face without a jump, and shows unfogged
 * beyond it. */
void FogVolume_edge_fade(void) {
    // fog_volume/edge
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_ortho(world), true);
    fv_wall(world, 0, -10, 40, FV_WALL);
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){4, 4, 4}, 0.5f, 1);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->edge_fade = 2;
    ecs_modified(world, e, FogVolume);
    unsigned char *px = fv_render(world, "fog_edge.png");
    int32_t x, last = fv_px(px, 32, 32)[1];
    test_assert(last < FV_WALL - 60);
    for (x = 33; x < 48; x ++) {
        int32_t value = fv_px(px, x, 32)[1];
        test_assert(value >= last - 2);
        test_assert(value - last < 60);
        last = value;
    }

    test_assert(fv_px(px, 46, 32)[1] > fv_px(px, 32, 32)[1] + 40);
    fv_expect(fv_px(px, 52, 32), FV_WALL, 1);
    stbi_image_free(px);

    ecs_fini(world);
}

/** A long box turned a quarter about Z fogs the column above the center instead of the row to
 * its right. */
void FogVolume_rotated_box(void) {
    // fog_volume/edge
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_ortho(world), true);
        fv_wall(world, 0, -10, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){6, 1, 1}, 1, 1);
        if (k) {
            ecs_set(world, e, Rotation, {0, 0, (float)GLM_PI_2});
        }

        unsigned char *px = fv_render(world, k ? "fog_rotated.png" : "fog_unrotated.png");
        int32_t fogged = fv_srgb(fv_lin(FV_WALL) * expf(-2.0f));
        const unsigned char *right = fv_px(px, 48, 32), *above = fv_px(px, 32, 16);
        fv_expect(k ? above : right, fogged, 4);
        fv_expect(k ? right : above, FV_WALL, 1);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** Render a scattering volume lit by an overhead sun, with or without a caster over its left
 * half, and get the pixels left and right of the center.
 *
 * @param caster Whether the caster is there
 * @param file Name of the PNG
 * @param out Receives the green of the left and the right pixel
 */
static void fv_shaft(
    bool caster,
    const char *file,
    int32_t out[2])
{
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = fv_view(world, fv_camera(world, 0, 0), true);
    View *v = ecs_get_mut(world, view, View);
    v->shadows = fv_shadows;
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, -15}, (float[]){8, 4, 6}, 0.2f, 0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->asymmetry = 0;
    ecs_modified(world, e, FogVolume);
    if (caster) {
        ecs_entity_t roof = ecs_new(world);
        ecs_set(world, roof, Position, {-5, 8, -15});
        ecs_set(world, roof, Box, {10, 0.5f, 20});
    }

    unsigned char *px = fv_render(world, file);
    out[0] = fv_px(px, 16, 32)[1];
    out[1] = fv_px(px, 48, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);
}

/** A caster between the sun and the volume leaves a dark shape in the fog under it. */
void FogVolume_shadow_shaft(void) {
    // fog_volume/light
    int32_t open[2], shaded[2];
    fv_shaft(false, "fog_shaft_open.png", open);
    fv_shaft(true, "fog_shaft.png", shaded);
    test_assert(open[0] > 40);
    test_assert(abs(open[0] - open[1]) <= 6);
    test_assert(shaded[0] * 2 < open[0]);
    test_assert(abs(shaded[1] - open[1]) <= 6);
    test_assert(shaded[0] * 2 < shaded[1]);
}

/** Render a scattering volume with g 0.7, lit by a sun that shines toward the camera or away
 * from it, and get its center pixel.
 *
 * @param toward Whether the camera looks toward the sun
 * @param file Name of the PNG
 * @return The green of the center pixel
 */
static int32_t fv_phase(
    bool toward,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, toward ? 0.0f : (float)GLM_PI, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 0.25f * GLM_PIf});
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, -10}, (float[]){4, 4, 4}, 0.2f, 0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->asymmetry = 0.7f;
    ecs_modified(world, e, FogVolume);
    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** With g 0.7 the fog is brighter looking toward the sun than away from it. */
void FogVolume_asymmetry_toward_sun(void) {
    // fog_volume/light
    int32_t toward = fv_phase(true, "fog_toward.png");
    int32_t away = fv_phase(false, "fog_away.png");
    test_assert(away > 2);
    test_assert(toward > away + 60);
}

/** Render a red and a blue volume in a line along Z from one side and get the center pixel.
 *
 * @param from_back Whether the camera looks from the far side, along +Z
 * @param file Name of the PNG
 * @param out Receives the pixel, RGBA
 */
static void fv_overlap(
    bool from_back,
    const char *file,
    unsigned char out[4])
{
    ecs_world_t *world = fv_world(1);
    fv_view(world, fv_camera(world, from_back ? -20.0f : 0.0f,
        from_back ? (float)GLM_PI : 0.0f), true);
    ecs_entity_t red = fv_volume(world, (float[]){0, 0, -6}, (float[]){3, 3, 2}, 0.5f, 0);
    ecs_entity_t blue = fv_volume(world, (float[]){0, 0, -14}, (float[]){3, 3, 2}, 0.5f, 0);
    ecs_get_mut(world, red, FogVolume)->color = (rgb){255, 0, 0};
    ecs_modified(world, red, FogVolume);
    ecs_get_mut(world, blue, FogVolume)->color = (rgb){0, 0, 255};
    ecs_modified(world, blue, FogVolume);
    unsigned char *px = fv_render(world, file);
    ecs_os_memcpy(out, fv_px(px, 32, 32), 4);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Overlapping volumes blend the nearer over the further, from either side. */
void FogVolume_overlap_order(void) {
    // fog_volume/order
    unsigned char front[4], back[4];
    fv_overlap(false, "fog_overlap_front.png", front);
    fv_overlap(true, "fog_overlap_back.png", back);
    test_assert(front[0] > 2 * front[2]);
    test_assert(front[2] > 20);
    test_assert(back[2] > 2 * back[0]);
    test_assert(back[0] > 20);
}

/** Render white glass in front of a volume lit by the ambient, or without the volume.
 *
 * @param volume Whether the volume is there
 * @param file Name of the PNG
 * @return The green of the center pixel, through the glass
 */
static int32_t fv_glass(
    bool volume,
    const char *file)
{
    ecs_world_t *world = fv_world(0.5f);
    fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t glass = ecs_new(world);
    ecs_set(world, glass, Position, {0, 0, -4});
    ecs_set(world, glass, Box, {3, 3, 0.2f});
    ecs_set(world, glass, Rgb, {255, 255, 255});
    ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 0});
    ecs_set(world, glass, Transmission, {.transmission_factor = 1, .ior = 1,
        .thickness_factor = 0, .attenuation_distance = 1e10f,
        .attenuation_color = {255, 255, 255}});
    if (volume) {
        fv_volume(world, (float[]){0, 0, -10}, (float[]){4, 4, 4}, 1, 0);
    }

    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** Glass in front of a volume shows it through the opaque snapshot. */
void FogVolume_in_transmission_snapshot(void) {
    // fog_volume/order
    int32_t clear = fv_glass(false, "fog_glass_none.png");
    int32_t fogged = fv_glass(true, "fog_glass.png");
    test_assert(clear < 60);
    test_assert(fogged > clear + 80);
}

/** An orthographic camera marches parallel rays through the volume. */
void FogVolume_orthographic(void) {
    // fog_volume/views
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_ortho(world), true);
    fv_wall(world, 0, -10, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, 0}, (float[]){4, 4, 2}, 0.25f, 1);
    unsigned char *px = fv_render(world, "fog_ortho.png");
    int32_t want = fv_srgb(fv_lin(FV_WALL) * expf(-1.0f));
    fv_expect(fv_px(px, 32, 32), want, 4);
    fv_expect(fv_px(px, 20, 44), want, 4);
    fv_expect(fv_px(px, 4, 4), FV_WALL, 1);
    stbi_image_free(px);

    ecs_fini(world);
}

/** A resized surface draws the volume at its new size. */
void FogVolume_resize(void) {
    // fog_volume/views
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    int32_t want = fv_srgb(fv_lin(FV_WALL) * expf(-0.8f));
    ecs_entity_t s = fv_surface(world, FV_SIZE, FV_SIZE, "fog_resize_a.png");
    box_render_gpu_stats_t before = fv_frames(world, 3);
    unsigned char *px = fv_load(world, "fog_resize_a.png", FV_SIZE, FV_SIZE);
    fv_expect(fv_px(px, 32, 32), want, 4);
    stbi_image_free(px);

    ecs_set(world, s, Surface, {.width = 96, .height = 48,
        .write_to_file = "fog_resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = fv_frames(world, 3);
    px = fv_load(world, "fog_resize_b.png", 96, 48);
    fv_expect(&px[(24 * 96 + 48) * 4], want, 4);
    stbi_image_free(px);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

/** Two views of one target draw the volumes each sees; the second, rendered last, shows its
 * own. */
void FogVolume_two_views(void) {
    // fog_volume/views
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, View, {.camera = fv_camera(world, 0, (float)GLM_PI),
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .fog_volumes = true}, .order = 1});
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_wall(world, 0, 20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){8, 8, 2}, 0.1f, 1);
    fv_volume(world, (float[]){0, 0, 10}, (float[]){8, 8, 2}, 0.3f, 1);
    unsigned char *px = fv_render(world, "fog_two_views.png");
    fv_expect(fv_px(px, 32, 32), fv_srgb(fv_lin(FV_WALL) * expf(-1.2f)), 4);
    stbi_image_free(px);

    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 1);
    test_int(after.fog_volumes - before.fog_volumes, 2);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

/** A view with fog_volumes off draws the wall unfogged in one forward pass, while another view
 * of the scene draws the volume. */
void FogVolume_view_disables(void) {
    // fog_volume/views
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), false);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    unsigned char *px = fv_render(world, "fog_disabled.png");
    fv_expect(fv_px(px, 32, 32), FV_WALL, 1);
    stbi_image_free(px);

    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    test_int(after.fog_volumes - before.fog_volumes, 0);
    test_int(after.forward_passes - before.forward_passes, 2);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

/** Capture a red wall into a reflection probe of a scene with a LightBake, with or without an
 * absorbing volume between them, and read the red of the texel that faces the wall.
 *
 * @param volume Whether the volume is there
 * @param sh Receives the red of the first SH coefficient of the probe
 * @return The red of the texel
 */
static float fv_capture(
    bool volume,
    float *sh)
{
    ecs_world_t *world = fv_world(0.2f);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = false});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 1});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    fv_view(world, camera, true);
    fv_surface(world, 64, 48, NULL);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {3, 1, 1.5f});
    ecs_set(world, wall, Box, {0.2f, 2, 2});
    ecs_set(world, wall, Rgb, {255, 0, 0});
    ecs_set(world, wall, Emissive, {.strength = 4, .color = {255, 0, 0}});
    if (volume) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, e, Position, {1.5f, 1, 0.75f});
        ecs_add(world, e, FogVolume);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->extent[0] = 0.6f;
        f->extent[1] = 1.5f;
        f->extent[2] = 1.5f;
        f->density = 5;
        f->absorption = 1;
        f->edge_fade = 0;
        ecs_modified(world, e, FogVolume);
    }

    int32_t frame;
    const ReflectionBakeStatus *status = NULL;
    const LightBakeStatus *light = NULL;
    box_render_gpu_stats_t before = fv_frames(world, 0), after = before;
    for (frame = 0; frame < 100; frame ++) {
        after = fv_frames(world, 1);
        status = ecs_get(world, scene, ReflectionBakeStatus);
        light = ecs_get(world, scene, LightBakeStatus);
        if (status->converged && light->converged) {
            break;
        }
    }

    test_bool(status->converged, true);
    test_bool(light->converged, true);
    test_int(after.errors, before.errors);
    test_assert(after.fog_volumes - before.fog_volumes <= frame + 1);

    int32_t i, count, slot = -1;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    for (i = 0; i < count; i ++) {
        if (!probes[i].scene) {
            slot = (int32_t)probes[i].slot;
        }
    }

    uint32_t texture = box_render_reflect_texture(world, scene);
    test_assert(texture != 0);
    test_assert(slot >= 0);
    float texels[16 * 16 * 4];
    float coefficients[BOX_RENDER_SH_COEFFICIENTS][4];
    test_assert(box_render_gpu_read_reflection(world, texture, (uint32_t)slot, 0, texels,
        16 * 16 * 4));
    test_assert(box_render_gpu_read_reflection_sh(world, texture, (uint32_t)slot,
        coefficients));
    *sh = coefficients[0][0];
    float result = texels[(8 * 16 + 4) * 4];
    ecs_fini(world);

    return result;
}

/** A reflection probe and a bake capture their scene without its fog volumes. */
void FogVolume_not_in_captures(void) {
    // fog_volume/captures
    float sh_clear, sh_fogged;
    float clear = fv_capture(false, &sh_clear);
    float fogged = fv_capture(true, &sh_fogged);
    test_assert(clear > 0.5f);
    test_assert(fabsf(fogged - clear) <= 0.02f * clear);
    test_assert(fabsf(sh_fogged - sh_clear) <= 0.02f * fabsf(sh_clear) + 1e-4f);
}

<<<<<<< HEAD
/** Add an absorbing volume with noise in front of the wall of an orthographic view: 2 units
 * deep across the whole view, 4 pixels a unit.
 *
 * @param world The world
 * @param strength FogVolume.noise_strength
 * @param scale FogVolume.noise_scale
 * @param wind FogVolume.wind
 * @return The volume
 */
static ecs_entity_t fv_noise_volume(
    ecs_world_t *world,
    float strength,
    float scale,
    const float wind[3])
{
    fv_view(world, fv_ortho(world), true);
    fv_wall(world, 0, -10, 40, FV_WALL);
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){20, 20, 1}, 0.4f, 1);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->steps = 128;
    f->noise_strength = strength;
    f->noise_scale = scale;
    glm_vec3_copy(ECS_CONST_CAST(float*, wind), f->wind);
=======
/** Add a white volume that scatters all it extinguishes, with hard edges, an isotropic phase
 * and 128 steps.
 *
 * @param world The world
 * @param pos Center
 * @param extent Half size
 * @param density Extinction per unit
 * @param light_inscatter Scale of the point and spot lights
 * @return The volume
 */
static ecs_entity_t fv_lit_volume(
    ecs_world_t *world,
    const float pos[3],
    const float extent[3],
    float density,
    float light_inscatter)
{
    ecs_entity_t e = fv_volume(world, pos, extent, density, 0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->asymmetry = 0;
    f->steps = 128;
    f->light_inscatter = light_inscatter;
>>>>>>> 7cb8fe4b3712af4523b83ff0cb71936a3b6f4c99
    ecs_modified(world, e, FogVolume);

    return e;
}

<<<<<<< HEAD
/** Measure the green of the pixels of a square capture inside a margin of 4 pixels.
 *
 * @param px The pixels
 * @param mean Receives the mean
 * @param deviation Receives the standard deviation
 * @param neighbours Receives the mean difference of horizontally and vertically neighbouring
 * pixels
 */
static void fv_measure(
    const unsigned char *px,
    double *mean,
    double *deviation,
    double *neighbours)
{
    double sum = 0, sq = 0, diff = 0;
    int32_t x, y, count = 0, pairs = 0;
    for (y = 4; y < FV_SIZE - 4; y ++) {
        for (x = 4; x < FV_SIZE - 4; x ++) {
            double g = fv_px(px, x, y)[1];
            sum += g;
            sq += g * g;
            count ++;
            if (x + 1 < FV_SIZE - 4) {
                diff += abs((int32_t)fv_px(px, x + 1, y)[1] - (int32_t)fv_px(px, x, y)[1]);
                pairs ++;
            }

            if (y + 1 < FV_SIZE - 4) {
                diff += abs((int32_t)fv_px(px, x, y + 1)[1] - (int32_t)fv_px(px, x, y)[1]);
                pairs ++;
            }
        }
    }

    *mean = sum / count;
    *deviation = sqrt(fmax(sq / count - *mean * *mean, 0.0));
    *neighbours = diff / pairs;
}

/** Mean difference of the green of two square captures, the second shifted by a number of
 * columns: pixel x of the second against pixel x - shift of the first.
 *
 * @param a The first capture
 * @param b The second capture
 * @param shift Columns the second is shifted by
 * @return The mean absolute difference
 */
static double fv_shifted_diff(
    const unsigned char *a,
    const unsigned char *b,
    int32_t shift)
{
    double diff = 0;
    int32_t x, y, count = 0;
    for (y = 4; y < FV_SIZE - 4; y ++) {
        for (x = 20; x < FV_SIZE - 20; x ++) {
            diff += abs((int32_t)fv_px(b, x, y)[1] - (int32_t)fv_px(a, x - shift, y)[1]);
            count ++;
        }
    }

    return diff / count;
}

/** A volume with a noise_strength of 0 renders the same pixels as one with the defaults, the
 * transmittance of its constant density, whatever its noise_scale and wind. */
void FogVolume_noise_zero_unchanged(void) {
    // fog_volume/noise
    const char *files[2] = { "fog_noise_zero_a.png", "fog_noise_zero_b.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_camera(world, 0, 0), true);
        fv_wall(world, 0, -20, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
        if (k) {
            FogVolume *f = ecs_get_mut(world, e, FogVolume);
            f->noise_strength = 0;
            f->noise_scale = 1;
            f->wind[0] = 30;
            f->wind[2] = -10;
            ecs_modified(world, e, FogVolume);
        }

        px[k] = fv_render(world, files[k]);
        ecs_fini(world);
    }

    int32_t want = fv_srgb(fv_lin(FV_WALL) * expf(-0.2f * 4.0f));
    fv_expect(fv_px(px[0], 32, 32), want, 4);
    fv_expect(fv_px(px[0], 40, 24), want, 6);
    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Noise of strength 1 varies the fog across the wall, which a strength of 0 fogs evenly,
 * around about the same mean. */
void FogVolume_noise_varies(void) {
    // fog_volume/noise
    double mean[2], deviation[2], neighbours[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, (float)k, 4, (float[]){0, 0, 0});
        unsigned char *px = fv_render(world, k ? "fog_noise_one.png" : "fog_noise_none.png");
        fv_measure(px, &mean[k], &deviation[k], &neighbours[k]);
        stbi_image_free(px);
        ecs_fini(world);
    }

    printf("noise 0: mean %.1f deviation %.2f, noise 1: mean %.1f deviation %.2f\n",
        mean[0], deviation[0], mean[1], deviation[1]);
    test_assert(deviation[0] < 1.0);
    test_assert(deviation[1] > 6.0);
    test_assert(fabs(mean[1] - mean[0]) < 25.0);
    test_assert(fabs(mean[0] - fv_srgb(fv_lin(FV_WALL) * expf(-0.8f))) < 4.0);
}

/** A larger noise_scale makes larger billows: neighbouring pixels differ less. */
void FogVolume_noise_scale(void) {
    // fog_volume/noise
    double mean[2], deviation[2], neighbours[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, 1, k ? 16.0f : 2.0f, (float[]){0, 0, 0});
        unsigned char *px = fv_render(world, k ? "fog_noise_large.png" : "fog_noise_small.png");
        fv_measure(px, &mean[k], &deviation[k], &neighbours[k]);
        stbi_image_free(px);
        ecs_fini(world);
    }

    printf("scale 2: neighbours %.2f deviation %.2f, scale 16: neighbours %.2f deviation %.2f\n",
        neighbours[0], deviation[0], neighbours[1], deviation[1]);
    test_assert(deviation[0] > 4.0);
    test_assert(deviation[1] > 4.0);
    test_assert(neighbours[0] > 2.0 * neighbours[1]);
}

/** With a fixed delta time a wind of 120 units a second along +X moves the fog 2 units, 8
 * pixels, to the right each frame; without wind two frames are the same. */
void FogVolume_wind_moves(void) {
    // fog_volume/noise
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, 1, 8, (float[]){k ? 120.0f : 0.0f, 0, 0});
        fv_surface(world, FV_SIZE, FV_SIZE, "fog_wind.png");
        box_render_gpu_stats_t before = fv_frames(world, 3);
        unsigned char *a = fv_load(world, "fog_wind.png", FV_SIZE, FV_SIZE);
        box_render_gpu_stats_t after = fv_frames(world, 1);
        unsigned char *b = fv_load(world, "fog_wind.png", FV_SIZE, FV_SIZE);
        test_int(after.errors, before.errors);
        if (!k) {
            test_assert(!memcmp(a, b, FV_SIZE * FV_SIZE * 4));
        } else {
            int32_t shift, best = 0;
            double best_diff = 1e30;
            for (shift = -16; shift <= 16; shift ++) {
                double d = fv_shifted_diff(a, b, shift);
                if (d < best_diff) {
                    best_diff = d;
                    best = shift;
                }
            }

            double still = fv_shifted_diff(a, b, 0);
            printf("wind: best shift %d diff %.2f, unshifted diff %.2f\n", best, best_diff,
                still);
            test_assert(abs(best - 8) <= 1);
            test_assert(best_diff < 2.0);
            test_assert(still > 4.0 * best_diff + 2.0);
        }

        stbi_image_free(a);
        stbi_image_free(b);
        ecs_fini(world);
    }
}

/** Two headless runs of 7 frames with --delta-time render the same pixels at the last frame,
 * wind and all. */
void FogVolume_noise_deterministic(void) {
    // fog_volume/noise
    const char *files[2] = { "fog_noise_run_a.png", "fog_noise_run_b.png" };
    char *argv[] = {"boxrun", "--headless", "--frames", "7", "--delta-time", "0.016666667"};
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        test_int(box_app_parse(world, 6, argv), 0);
        App *app = ecs_singleton_get_mut(world, App);
        app->unguarded = true;
        app->unpaced = true;
        ecs_singleton_modified(world, App);
        fv_noise_volume(world, 0.8f, 5, (float[]){1.5f, 0.5f, -0.75f});
        fv_surface(world, FV_SIZE, FV_SIZE, files[k]);
        test_int(box_app_run(world), 0);
        box_render_sync(world);
        test_int(ecs_get_world_info(world)->frame_count_total, 7);
        px[k] = fv_load(world, files[k], FV_SIZE, FV_SIZE);
        ecs_fini(world);
    }

    double mean, deviation, neighbours;
    fv_measure(px[0], &mean, &deviation, &neighbours);
    test_assert(deviation > 4.0);
    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** After a device loss the restarted backend makes the noise texture again: the fog renders
 * the same billows as before. */
void FogVolume_noise_device_loss(void) {
    // fog_volume/noise
    ecs_world_t *world = fv_world(0);
    fv_noise_volume(world, 1, 4, (float[]){0, 0, 0});
    unsigned char *before = fv_render(world, "fog_noise_lost.png");
    double mean, deviation, neighbours;
    fv_measure(before, &mean, &deviation, &neighbours);
    test_assert(deviation > 6.0);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 0);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/fog_noise_lost.png", tmp);
    remove(path);
    box_render_gpu_lose_device(world);
    int32_t i, wrong = 0;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);
    unsigned char *after = fv_load(world, "fog_noise_lost.png", FV_SIZE, FV_SIZE);
    for (i = 0; i < FV_SIZE * FV_SIZE * 4; i ++) {
        wrong += abs((int32_t)after[i] - (int32_t)before[i]) > 2;
    }

    test_int(wrong, 0);
    stbi_image_free(after);
    stbi_image_free(before);
    ecs_os_free(path);
    ecs_fini(world);
}
=======
/** Add a point light.
 *
 * @param world The world
 * @param pos Position
 * @param intensity Intensity
 * @param range Range
 * @param shadows Whether it casts shadows
 * @return The light
 */
static ecs_entity_t fv_point(
    ecs_world_t *world,
    const float pos[3],
    float intensity,
    float range,
    bool shadows)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {pos[0], pos[1], pos[2]});
    ecs_set(world, e, PointLight, {.intensity = intensity, .range = range,
        .shadows = shadows, .shadow_resolution = 512});

    return e;
}

/** Add a spot light shining straight down with an inner angle 5 degrees under its outer one.
 *
 * @param world The world
 * @param pos Position
 * @param intensity Intensity
 * @param range Range
 * @param outer Outer half angle in degrees
 * @param shadows Whether it casts shadows
 * @return The light
 */
static ecs_entity_t fv_spot(
    ecs_world_t *world,
    const float pos[3],
    float intensity,
    float range,
    float outer,
    bool shadows)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {pos[0], pos[1], pos[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, e, SpotLight, {.intensity = intensity, .range = range,
        .inner_angle = outer - 5, .outer_angle = outer, .shadows = shadows,
        .shadow_resolution = 512});

    return e;
}

/** Turn on the shadows of a view.
 *
 * @param world The world
 * @param view The view
 */
static void fv_view_shadows(
    ecs_world_t *world,
    ecs_entity_t view)
{
    View *v = ecs_get_mut(world, view, View);
    v->shadows = fv_shadows;
    ecs_modified(world, view, View);
}

/** Render a volume lit by the ambient with light_inscatter 0, with or without a bright point
 * light inside.
 *
 * @param light Whether the point light is there
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_lights_zero(
    bool light,
    const char *file)
{
    ecs_world_t *world = fv_world(0.2f);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_lit_volume(world, (float[]){0, 0, -10}, (float[]){6, 6, 4}, 0.3f, 0);
    if (light) {
        fv_point(world, (float[]){0, 0, -10}, 10 * GLM_PIf, 5, false);
    }

    unsigned char *px = fv_render(world, file);
    ecs_fini(world);

    return px;
}

/** With light_inscatter 0 a point light inside the volume changes none of its pixels. */
void FogVolume_lights_zero_unchanged(void) {
    // fog_volume/lights
    unsigned char *without = fv_lights_zero(false, "fog_lights_zero_none.png");
    unsigned char *with = fv_lights_zero(true, "fog_lights_zero.png");
    test_assert(fv_px(without, 32, 32)[1] > 10);
    test_assert(!memcmp(without, with, FV_SIZE * FV_SIZE * 4));
    stbi_image_free(without);
    stbi_image_free(with);
}

/** A point light in the middle of a thin dense slab glows around it, less with the distance,
 * and not past its range. At z -10 a world unit is about 5.5 pixels. */
void FogVolume_point_light_glow(void) {
    // fog_volume/lights
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_lit_volume(world, (float[]){0, 0, -10}, (float[]){6, 6, 1}, 0.5f, 1);
    fv_point(world, (float[]){0, 0, -10}, 0.5f * GLM_PIf, 4, false);
    unsigned char *px = fv_render(world, "fog_point_glow.png");
    int32_t x, last = fv_px(px, 33, 32)[1];
    for (x = 34; x < 62; x ++) {
        int32_t value = fv_px(px, x, 32)[1];
        test_assert(value <= last + 3);
        last = value;
    }

    int32_t near_ = fv_px(px, 40, 32)[1], mid = fv_px(px, 49, 32)[1];
    test_assert(near_ > 80);
    test_assert(near_ > mid + 20);
    test_assert(mid > 10);
    test_assert(fv_px(px, 60, 32)[1] <= 2);
    test_assert(fv_px(px, 32, 60)[1] <= 2);
    test_assert(fv_px(px, 4, 4)[1] <= 2);
    stbi_image_free(px);

    ecs_fini(world);
}

/** Render a slab of fog under a spot light shining down, from the side, with or without a
 * caster under the light; the light and its tile cast shadows when shadows is set.
 *
 * @param shadows Whether the view and the light render shadows
 * @param caster Whether a thin bar 1.5 under the light crosses the cone
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_spot_render(
    bool shadows,
    bool caster,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = fv_view(world, fv_camera(world, 0, 0), true);
    if (shadows) {
        fv_view_shadows(world, view);
    }

    fv_lit_volume(world, (float[]){0, 0, -10}, (float[]){6, 5, 1}, 0.5f, 1);
    fv_spot(world, (float[]){0, 4, -10}, 3 * GLM_PIf, 10, shadows ? 30 : 20, shadows);
    if (caster) {
        ecs_entity_t bar = ecs_new(world);
        ecs_set(world, bar, Position, {0, 2.5f, -10});
        ecs_set(world, bar, Box, {0.5f, 0.2f, 4});
    }

    unsigned char *px = fv_render(world, file);
    ecs_fini(world);

    return px;
}

/** A spot light shining down through the fog shows its cone from the side: lit under the light,
 * dark beside the cone and above the light. At z -10 a world unit is about 5.5 pixels, the cone
 * is 1.5 wide each way at y 0. */
void FogVolume_spot_light_cone(void) {
    // fog_volume/lights
    unsigned char *px = fv_spot_render(false, false, "fog_spot_cone.png");
    int32_t center = fv_px(px, 32, 32)[1];
    test_assert(center > 60);
    test_assert(fv_px(px, 32, 22)[1] > 60);
    test_assert(fv_px(px, 49, 32)[1] <= 2);
    test_assert(fv_px(px, 15, 32)[1] <= 2);
    test_assert(fv_px(px, 32, 4)[1] <= 2);
    test_assert(fv_px(px, 52, 50)[1] <= 2);
    stbi_image_free(px);
}

/** A bar inside the cone of a shadowed spot light leaves a dark wedge in the fog under it, while
 * the fog of the cone beside the wedge stays as lit. */
void FogVolume_spot_shadow_in_fog(void) {
    // fog_volume/lights
    unsigned char *open = fv_spot_render(true, false, "fog_spot_shadow_open.png");
    unsigned char *shaded = fv_spot_render(true, true, "fog_spot_shadow.png");
    int32_t open_center = fv_px(open, 32, 32)[1], shaded_center = fv_px(shaded, 32, 32)[1];
    int32_t open_side = fv_px(open, 40, 32)[1], shaded_side = fv_px(shaded, 40, 32)[1];
    test_assert(open_center > 60);
    test_assert(shaded_center * 2 < open_center);
    test_assert(open_side > 40);
    test_assert(abs(shaded_side - open_side) <= 8);
    test_assert(shaded_center * 2 < shaded_side);
    stbi_image_free(open);
    stbi_image_free(shaded);
}

/** Render a slab of fog around a shadowed point light, with or without a bar 1.5 under it.
 *
 * @param caster Whether the bar is there
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_point_shadow_render(
    bool caster,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = fv_view(world, fv_camera(world, 0, 0), true);
    fv_view_shadows(world, view);
    fv_lit_volume(world, (float[]){0, 0, -10}, (float[]){6, 5, 1}, 0.5f, 1);
    fv_point(world, (float[]){0, 3, -10}, 2 * GLM_PIf, 6, true);
    if (caster) {
        ecs_entity_t bar = ecs_new(world);
        ecs_set(world, bar, Position, {0, 1.5f, -10});
        ecs_set(world, bar, Box, {0.5f, 0.2f, 4});
    }

    unsigned char *px = fv_render(world, file);
    ecs_fini(world);

    return px;
}

/** A bar under a shadowed point light leaves a dark wedge in the fog under it, while the fog
 * beside the wedge stays as lit. */
void FogVolume_point_shadow_in_fog(void) {
    // fog_volume/lights
    unsigned char *open = fv_point_shadow_render(false, "fog_point_shadow_open.png");
    unsigned char *shaded = fv_point_shadow_render(true, "fog_point_shadow.png");
    int32_t open_center = fv_px(open, 32, 32)[1], shaded_center = fv_px(shaded, 32, 32)[1];
    int32_t open_side = fv_px(open, 41, 32)[1], shaded_side = fv_px(shaded, 41, 32)[1];
    test_assert(open_center > 60);
    test_assert(shaded_center * 2 < open_center);
    test_assert(open_side > 40);
    test_assert(abs(shaded_side - open_side) <= 8);
    test_assert(shaded_center * 2 < shaded_side);
    stbi_image_free(open);
    stbi_image_free(shaded);
}

/** Render a volume in front of the camera, with or without a point light behind the camera
 * whose range reaches into the volume.
 *
 * @param light Whether the light is there
 * @param file Name of the PNG
 * @return The green of the center pixel
 */
static int32_t fv_behind_render(
    bool light,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_lit_volume(world, (float[]){0, 0, -3}, (float[]){6, 6, 2}, 0.5f, 1);
    if (light) {
        fv_point(world, (float[]){0, 0, 3}, 4 * GLM_PIf, 8, false);
    }

    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** A point light behind the camera, outside the frustum the clusters cover, lights the near part
 * of a volume its range reaches. */
void FogVolume_light_outside_clusters(void) {
    // fog_volume/lights
    int32_t dark = fv_behind_render(false, "fog_behind_light_none.png");
    int32_t lit = fv_behind_render(true, "fog_behind_light.png");
    test_assert(dark <= 2);
    test_assert(lit > dark + 30);
}

/** Count the forward passes and fog volume draws of two frames of a volume with or without 16
 * point and spot lights in it.
 *
 * @param lights Whether the lights are there
 * @param out Receives the forward passes and the fog volumes
 */
static void fv_splits(
    bool lights,
    int32_t out[2])
{
    ecs_world_t *world = fv_world(0.2f);
    ecs_entity_t view = fv_view(world, fv_camera(world, 0, 0), true);
    fv_view_shadows(world, view);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_lit_volume(world, (float[]){0, 0, -10}, (float[]){8, 6, 4}, 0.2f, 1);
    int32_t i;
    for (i = 0; lights && i < 16; i ++) {
        float pos[3] = {(float)(i % 4) * 4 - 6, (float)(i / 4) * 2 - 3, -10};
        if (i % 2) {
            fv_spot(world, pos, GLM_PIf, 5, 30, i % 4 == 1);
        } else {
            fv_point(world, pos, GLM_PIf, 4, i % 8 == 0);
        }
    }

    unsigned char *px = fv_render(world, lights ? "fog_splits_lights.png" : "fog_splits.png");
    stbi_image_free(px);
    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    out[0] = after.forward_passes - before.forward_passes;
    out[1] = after.fog_volumes - before.fog_volumes;
    test_int(after.errors, before.errors);
    ecs_fini(world);
}

/** Lights in a volume add no forward pass and no fog volume draw. */
void FogVolume_no_new_splits(void) {
    // fog_volume/lights
    int32_t without[2], with[2];
    fv_splits(false, without);
    fv_splits(true, with);
    test_int(without[0], 4);
    test_int(without[1], 2);
    test_int(with[0], without[0]);
    test_int(with[1], without[1]);
}
>>>>>>> 7cb8fe4b3712af4523b83ff0cb71936a3b6f4c99
