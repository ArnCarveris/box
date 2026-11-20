/**
 * @file FogVolume.c
 * @brief Tests for the fog volumes a view lights in its froxel grid and applies in its forward
 * pass: transmittance, scattered light, clipping to the depth, the camera inside and behind,
 * edges, rotation, sun shafts, the phase, the order of overlapping volumes, glass, orthographic
 * cameras, resizes, several views, the fog distance, the size of the grid, the captures,
 * which do not see them, and the history of the grid from frame to frame; checked on PNG
 * captures.
 *
 * A 64 pixel capture has a grid of 8 by 8 cells: hard faces of a box land on the depth slices of
 * the grid, so the expected transmittance of a box follows from the share of the sample points
 * of each slice in it (fv_grid_transmittance), and shapes in the fog are a cell or more across.
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
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

/** Add a white fog volume with hard edges.
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

/** Views fv_still resets at most */
#define FV_VIEWS (8)

/** Add HistoryReset to every view, so the next frame samples each cell of the froxel grids at its
 * center alone, as without the history, where a test measures the fog of one frame.
 *
 * @param world The world
 */
static void fv_still(
    ecs_world_t *world)
{
    ecs_entity_t views[FV_VIEWS];
    int32_t i, count = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count && count < FV_VIEWS; i ++) {
            views[count ++] = it.entities[i];
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_add(world, views[i], HistoryReset);
    }
}

/** Render frames into a square capture as fv_render does, the last one without the history of
 * the views, and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_render_still(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = fv_frames(world, 0), after;
    fv_surface(world, FV_SIZE, FV_SIZE, file);
    fv_frames(world, 2);
    fv_still(world);
    after = fv_frames(world, 1);
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

/** Near plane of the cameras of the cases */
#define FV_NEAR (0.1f)

/** Transmittance the froxel grid of a view gives at a view depth through the middle of the
 * image, for an absorbing volume with hard faces between two view depths.
 *
 * Each slice absorbs over its thickness times the share of its BOX_FROXEL_SAMPLES points in the
 * volume, accumulated to their far sides and interpolated as froxel_uvw of froxel.wgsl does.
 *
 * @param density Extinction per unit of the volume, times the length of the ray per unit of
 * view depth for a ray at an angle
 * @param z0 View depth of the near face of the volume
 * @param z1 View depth of the far face of the volume
 * @param depth View depth of the pixel, 0 or less for a pixel without depth
 * @param ortho Whether the camera is orthographic
 * @param distance LightSettings.fog_distance of the view
 * @return The transmittance
 */
static float fv_grid_transmittance(
    float density,
    float z0,
    float z1,
    float depth,
    bool ortho,
    float distance)
{
    const int32_t n = BOX_FROXEL_SLICES;
    const float far_ = FV_NEAR + distance;
    float t[BOX_FROXEL_SLICES], tau = 0, result;
    int32_t k;
    for (k = 0; k < n; k ++) {
        float lo = box_render_froxel_depth((float)k / n, FV_NEAR, far_, ortho);
        float hi = box_render_froxel_depth((float)(k + 1) / n, FV_NEAR, far_, ortho);
        int32_t j, inside = 0;
        for (j = 0; j < BOX_FROXEL_SAMPLES; j ++) {
            float c = lo + (hi - lo) * ((float)j + 0.5f) / BOX_FROXEL_SAMPLES;
            inside += c > z0 && c < z1;
        }

        tau += density * (hi - lo) * (float)inside / BOX_FROXEL_SAMPLES;

        t[k] = expf(-tau);
    }

    float share = depth > 0
        ? box_render_froxel_share(depth, FV_NEAR, far_, ortho) * (float)n
        : (float)n;
    if (share <= 1) {
        result = 1 + (t[0] - 1) * share;
    } else {
        float i = share - 1;
        int32_t k0 = (int32_t)floorf(i);
        result = k0 >= n - 1 ? t[n - 1] : t[k0] + (t[k0 + 1] - t[k0]) * (i - (float)k0);
    }

    return result;
}

/** fv_grid_transmittance of a perspective view with the default fog distance.
 *
 * @param density Extinction per unit of the volume
 * @param z0 View depth of the near face of the volume
 * @param z1 View depth of the far face of the volume
 * @param depth View depth of the pixel
 * @return The transmittance
 */
static float fv_grid(
    float density,
    float z0,
    float z1,
    float depth)
{
    return fv_grid_transmittance(density, z0, z1, depth, false, 100);
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
 * light, over the slices of the grid in it; the view lights its grid in two dispatches and ends
 * its pass for the volume, which draws last in one more. */
void FogVolume_density_transmittance(void) {
    // fog_volume/march
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    unsigned char *px = fv_render(world, "fog_transmittance.png");
    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.2f, 8, 12, 19.9f));
    test_assert(abs(want - fv_srgb(fv_lin(FV_WALL) * expf(-0.2f * 4.0f))) <= 12);
    fv_expect(fv_px(px, 32, 32), want, 4);
    fv_expect(fv_px(px, 40, 24), want, 6);
    stbi_image_free(px);

    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    test_int(after.fog_volumes - before.fog_volumes, 2);
    test_int(after.fog_dispatches - before.fog_dispatches, 4);
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
    test_assert(abs(near_ - fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f, 6, 14, 9.9f))) <= 6);
    test_assert(abs(far_ - fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f, 6, 14, 19.9f))) <= 6);
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
    fv_expect(fv_px(px, 32, 32), fv_srgb(fv_lin(FV_WALL) * fv_grid(0.1f, -1, 10, 19.9f)), 5);
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
 * its right. The box is 4 units, two cells, across, so the cell centers next to the middle of
 * the image are inside it. */
void FogVolume_rotated_box(void) {
    // fog_volume/edge
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_ortho(world), true);
        fv_wall(world, 0, -10, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){6, 2, 2}, 0.25f, 1);
        if (k) {
            ecs_set(world, e, Rotation, {0, 0, (float)GLM_PI_2});
        }

        unsigned char *px = fv_render(world, k ? "fog_rotated.png" : "fog_unrotated.png");
        int32_t fogged = fv_srgb(fv_lin(FV_WALL) *
            fv_grid_transmittance(0.25f, 8, 12, 19.9f, true, 100));
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

/** Render an emissive panel of black albedo in the middle of a purely absorbing volume, with
 * nothing behind it, and get the green of the center pixel.
 *
 * @param transmissive Whether the panel is transmissive
 * @param file Name of the PNG
 * @return The green of the center pixel, on the panel
 */
static int32_t fv_panel_inside(
    bool transmissive,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t panel = fv_wall(world, 0, -10, 3, FV_WALL);
    if (transmissive) {
        ecs_set(world, panel, Transmission, {.transmission_factor = 1, .ior = 1,
            .thickness_factor = 0, .attenuation_distance = 1e10f,
            .attenuation_color = {255, 255, 255}});
    }

    fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** A transmissive panel inside a volume is fogged over the part of the volume in front of it,
 * like an opaque panel at the same place. */
void FogVolume_transmissive_inside_fogged(void) {
    // fog_volume/order
    int32_t opaque = fv_panel_inside(false, "fog_panel_opaque.png");
    int32_t glass = fv_panel_inside(true, "fog_panel_glass.png");
    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f, 6, 14, 9.9f));
    printf("opaque %d, transmissive %d, want %d\n", opaque, glass, want);
    test_assert(abs(opaque - want) <= 6);
    test_assert(abs(glass - opaque) <= 6);
}

/** Add a clear transmissive panel 0.2 thick facing the camera: black albedo with a gray
 * emission, ior 1, no thickness and no absorption, so it adds its emission to what it shows.
 *
 * @param world The world
 * @param z Z of its center
 * @param size Width and height
 * @param gray Emission, display encoded
 * @return The panel
 */
static ecs_entity_t fv_glass_panel(
    ecs_world_t *world,
    float z,
    float size,
    uint8_t gray)
{
    ecs_entity_t e = fv_wall(world, 0, z, size, gray);
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0});
    ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1,
        .thickness_factor = 0, .attenuation_distance = 1e10f,
        .attenuation_color = {255, 255, 255}});

    return e;
}

/** Render an emissive transmissive panel at z -4, in front of a purely absorbing volume from
 * z -6 to -14 or without it, with nothing behind, and get the green of the center pixel.
 *
 * @param volume Whether the volume is there
 * @param file Name of the PNG
 * @return The green of the center pixel, on the panel
 */
static int32_t fv_panel_in_front(
    bool volume,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_glass_panel(world, -4, 2, FV_WALL);
    if (volume) {
        fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
    }

    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** A transmissive panel in front of a volume is not fogged by it: its own light reaches the
 * camera unattenuated, as without the volume. */
void FogVolume_transmissive_in_front_not_fogged(void) {
    // fog_volume/order
    int32_t clear = fv_panel_in_front(false, "fog_front_none.png");
    int32_t fogged = fv_panel_in_front(true, "fog_front.png");
    test_assert(abs(clear - FV_WALL) <= 4);
    test_assert(abs(fogged - clear) <= 3);
}

/** A clear transmissive panel inside a volume, in front of a wall behind the volume, shows the
 * wall fogged over the whole depth of the volume once: the part behind the panel through the
 * snapshot, the part in front of it over the panel. */
void FogVolume_transmissive_inside_fogged_once(void) {
    // fog_volume/order
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_glass_panel(world, -10, 3, 0);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
    unsigned char *px = fv_render(world, "fog_panel_once.png");
    int32_t x[2] = {32, 4}, i;
    for (i = 0; i < 2; i ++) {
        /* The ray of the pixel crosses the volume at an angle, longer per unit of depth */
        float t = ((float)x[i] + 0.5f) / (FV_SIZE / 2.0f) - 1.0f;
        t *= tanf(30.0f * (float)GLM_PI / 180.0f);
        float stretch = sqrtf(1.0f + t * t);
        fv_expect(fv_px(px, x[i], 32),
            fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f * stretch, 6, 14, 19.9f)), 6);
    }
    stbi_image_free(px);

    ecs_fini(world);
}

/** A view with transmissive shapes and no fog volumes keeps its passes: the snapshot splits its
 * forward pass once a frame. With a volume it also draws its glass depth only, applies the fog
 * behind the glass before the snapshot and the fog in front of it after the glass, each in a pass
 * of its own, and counts the volume once a frame. */
void FogVolume_transmissive_pass_count(void) {
    // fog_volume/draw
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_glass_panel(world, -10, 3, 0);
    unsigned char *px = fv_render(world, "fog_glass_passes.png");
    stbi_image_free(px);
    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    test_int(after.forward_passes - before.forward_passes, 4);
    test_int(after.fog_volumes - before.fog_volumes, 0);
    test_int(after.errors, before.errors);

    fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
    fv_frames(world, 2);
    before = fv_frames(world, 0);
    after = fv_frames(world, 2);
    test_int(after.forward_passes - before.forward_passes, 12);
    test_int(after.fog_volumes - before.fog_volumes, 2);
    test_int(after.fog_dispatches - before.fog_dispatches, 4);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

/** An orthographic camera has parallel rays through the volume and even slices. */
void FogVolume_orthographic(void) {
    // fog_volume/views
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_ortho(world), true);
    fv_wall(world, 0, -10, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, 0}, (float[]){4, 4, 2}, 0.25f, 1);
    unsigned char *px = fv_render(world, "fog_ortho.png");
    int32_t want = fv_srgb(fv_lin(FV_WALL) *
        fv_grid_transmittance(0.25f, 8, 12, 19.9f, true, 100));
    fv_expect(fv_px(px, 32, 32), want, 4);
    fv_expect(fv_px(px, 24, 40), want, 4);
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
    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.2f, 8, 12, 19.9f));
    ecs_entity_t s = fv_surface(world, FV_SIZE, FV_SIZE, "fog_resize_a.png");
    box_render_gpu_stats_t before = fv_frames(world, 3);
    unsigned char *px = fv_load(world, "fog_resize_a.png", FV_SIZE, FV_SIZE);
    fv_expect(fv_px(px, 32, 32), want, 4);
    stbi_image_free(px);
    test_int(before.fog_grid[0], 8);
    test_int(before.fog_grid[1], 8);

    ecs_set(world, s, Surface, {.width = 96, .height = 48,
        .write_to_file = "fog_resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = fv_frames(world, 3);
    px = fv_load(world, "fog_resize_b.png", 96, 48);
    fv_expect(&px[(24 * 96 + 48) * 4], want, 4);
    stbi_image_free(px);
    test_int(after.fog_grid[0], 12);
    test_int(after.fog_grid[1], 6);
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
    fv_expect(fv_px(px, 32, 32), fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f, 8, 12, 19.9f)), 4);
    stbi_image_free(px);

    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 1);
    test_int(after.fog_volumes - before.fog_volumes, 2);
    test_int(after.fog_dispatches - before.fog_dispatches, 4);
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
        .capture_size = 8, .budget_ms = 2, .bounces = 1, .compute = true});
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
    f->noise_strength = strength;
    f->noise_scale = scale;
    glm_vec3_copy(ECS_CONST_CAST(float*, wind), f->wind);
    ecs_modified(world, e, FogVolume);

    return e;
}

/** Add a white volume that scatters all it extinguishes, with hard edges and an isotropic
 * phase.
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
    f->light_inscatter = light_inscatter;
    ecs_modified(world, e, FogVolume);

    return e;
}

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

    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.2f, 8, 12, 19.9f));
    fv_expect(fv_px(px[0], 32, 32), want, 4);
    fv_expect(fv_px(px[0], 40, 24), want, 6);
    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Noise of strength 1 varies the fog across the wall, which a strength of 0 fogs evenly,
 * around about the same mean, in a frame of cells sampled at their centers. */
void FogVolume_noise_varies(void) {
    // fog_volume/noise
    double mean[2], deviation[2], neighbours[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, (float)k, 4, (float[]){0, 0, 0});
        unsigned char *px = fv_render_still(world,
            k ? "fog_noise_one.png" : "fog_noise_none.png");
        fv_measure(px, &mean[k], &deviation[k], &neighbours[k]);
        stbi_image_free(px);
        ecs_fini(world);
    }

    printf("noise 0: mean %.1f deviation %.2f, noise 1: mean %.1f deviation %.2f\n",
        mean[0], deviation[0], mean[1], deviation[1]);
    test_assert(deviation[0] < 1.0);
    test_assert(deviation[1] > 6.0);
    test_assert(fabs(mean[1] - mean[0]) < 25.0);
    test_assert(fabs(mean[0] - fv_srgb(fv_lin(FV_WALL) *
        fv_grid_transmittance(0.4f, 9, 11, 19.9f, true, 100))) < 4.0);
}

/** A larger noise_scale makes larger billows: neighbouring pixels differ less, in a frame of
 * cells sampled at their centers. */
void FogVolume_noise_scale(void) {
    // fog_volume/noise
    double mean[2], deviation[2], neighbours[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, 1, k ? 32.0f : 4.0f, (float[]){0, 0, 0});
        unsigned char *px = fv_render_still(world,
            k ? "fog_noise_large.png" : "fog_noise_small.png");
        fv_measure(px, &mean[k], &deviation[k], &neighbours[k]);
        stbi_image_free(px);
        ecs_fini(world);
    }

    printf("scale 4: neighbours %.2f deviation %.2f, scale 32: neighbours %.2f deviation %.2f\n",
        neighbours[0], deviation[0], neighbours[1], deviation[1]);
    test_assert(deviation[0] > 4.0);
    test_assert(deviation[1] > 4.0);
    test_assert(neighbours[0] > 2.0 * neighbours[1]);
}

/** With a fixed delta time a wind of 120 units a second along +X moves the fog 2 units, 8
 * pixels, to the right each frame; without wind two frames are the same. The frames are taken
 * without the history of the grid, which lags behind the wind by its blend. */
void FogVolume_wind_moves(void) {
    // fog_volume/noise
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_noise_volume(world, 1, 8, (float[]){k ? 120.0f : 0.0f, 0, 0});
        fv_surface(world, FV_SIZE, FV_SIZE, "fog_wind.png");
        box_render_gpu_stats_t before = fv_frames(world, 2);
        fv_still(world);
        fv_frames(world, 1);
        unsigned char *a = fv_load(world, "fog_wind.png", FV_SIZE, FV_SIZE);
        fv_still(world);
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

/** After three days a frame at a time T and one at T plus the period of the noise along the
 * wind, 10 noise_scale over its speed, render the same pixels, while one moved by half a
 * noise_scale does not: half the period would move the coarse sample by whole cells and change
 * only the detail. The frames are taken without the history of the grid, which the jumps in
 * time would blend with billows from elsewhere. */
void FogVolume_wind_period(void) {
    // fog_volume/noise
    const float scale = 4, speed = 2;
    const float period = 10 * scale / speed;
    ecs_world_t *world = fv_world(0);
    fv_noise_volume(world, 1, scale, (float[]){speed, 0, 0});
    fv_surface(world, FV_SIZE, FV_SIZE, "fog_wind_period.png");
    box_render_gpu_stats_t before = fv_frames(world, 3), after;
    // Three days of uptime, where a float32 time steps by 16 ms
    fv_still(world);
    ecs_progress(world, 3 * 86400.0f + 0.37f);
    box_render_sync(world);
    unsigned char *a = fv_load(world, "fog_wind_period.png", FV_SIZE, FV_SIZE);
    fv_still(world);
    ecs_progress(world, period);
    box_render_sync(world);
    unsigned char *b = fv_load(world, "fog_wind_period.png", FV_SIZE, FV_SIZE);
    fv_still(world);
    ecs_progress(world, period / 20);
    box_render_sync(world);
    unsigned char *c = fv_load(world, "fog_wind_period.png", FV_SIZE, FV_SIZE);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.errors, before.errors);

    double mean, deviation, neighbours;
    fv_measure(a, &mean, &deviation, &neighbours);
    double same = fv_shifted_diff(a, b, 0), moved = fv_shifted_diff(a, c, 0);
    int32_t i, worst = 0;
    for (i = 0; i < FV_SIZE * FV_SIZE * 4; i ++) {
        int32_t d = abs((int32_t)a[i] - (int32_t)b[i]);
        worst = d > worst ? d : worst;
    }

    printf("wind period: deviation %.2f, diff %.2f (worst %d), moved diff %.2f\n", deviation,
        same, worst, moved);
    test_assert(deviation > 4.0);
    test_assert(same < 0.5);
    test_assert(worst <= 3);
    test_assert(moved > 4.0 * same + 2.0);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    ecs_fini(world);
}

/** Two headless runs of 30 frames with --delta-time render the same pixels at the last frame,
 * wind, jitter and the history of the grid and all. */
void FogVolume_noise_deterministic(void) {
    // fog_volume/noise
    const char *files[2] = { "fog_noise_run_a.png", "fog_noise_run_b.png" };
    char *argv[] = {"boxrun", "--headless", "--frames", "30", "--delta-time", "0.016666667"};
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        test_int(box_app_parse(world, 6, argv), 0);
        App *app = ecs_singleton_get_mut(world, App);
        app->unguarded = true;
        app->unpaced = true;
        ecs_singleton_modified(world, App);
        fv_noise_volume(world, 0.8f, 12, (float[]){1.5f, 0.5f, -0.75f});
        fv_surface(world, FV_SIZE, FV_SIZE, files[k]);
        test_int(box_app_run(world), 0);
        box_render_sync(world);
        test_int(ecs_get_world_info(world)->frame_count_total, 30);
        px[k] = fv_load(world, files[k], FV_SIZE, FV_SIZE);
        ecs_fini(world);
    }

    double mean, deviation, neighbours;
    fv_measure(px[0], &mean, &deviation, &neighbours);
    printf("deterministic run: deviation %.2f\n", deviation);
    test_assert(deviation > 4.0);
    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** After a device loss the restarted backend makes the noise texture again: the fog renders
 * the same billows as before, both frames without the history of the grid. */
void FogVolume_noise_device_loss(void) {
    // fog_volume/noise
    ecs_world_t *world = fv_world(0);
    fv_noise_volume(world, 1, 4, (float[]){0, 0, 0});
    unsigned char *before = fv_render_still(world, "fog_noise_lost.png");
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
        if (i == 3) {
            fv_still(world);
        }

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
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});
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
 * caster under the light; the light and its tile cast shadows when shadows is set, and its cone
 * is then wider, so the fog beside the shadow of the caster is a cell of the grid away from it.
 *
 * @param shadows Whether the view and the light render shadows
 * @param caster Whether a bar 1 wide 1.5 under the light crosses the cone
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
    fv_spot(world, (float[]){0, 4, -10}, 3 * GLM_PIf, 10, shadows ? 40 : 20, shadows);
    if (caster) {
        ecs_entity_t bar = ecs_new(world);
        ecs_set(world, bar, Position, {0, 2.5f, -10});
        ecs_set(world, bar, Box, {1, 0.2f, 4});
    }

    unsigned char *px = fv_render(world, file);
    ecs_fini(world);

    return px;
}

/** A spot light shining down through the fog shows its cone from the side: lit under the light,
 * dark beside the cone and above the light. At z -10 a world unit is about 5.5 pixels, the cone
 * is 1.5 wide each way at y 0; a cell of the grid is 8 pixels. */
void FogVolume_spot_light_cone(void) {
    // fog_volume/lights
    unsigned char *px = fv_spot_render(false, false, "fog_spot_cone.png");
    int32_t center = fv_px(px, 32, 32)[1];
    test_assert(center > 60);
    test_assert(fv_px(px, 32, 26)[1] > 60);
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
    int32_t open_side = fv_px(open, 44, 32)[1], shaded_side = fv_px(shaded, 44, 32)[1];
    test_assert(open_center > 60);
    test_assert(shaded_center * 2 < open_center);
    test_assert(open_side > 40);
    test_assert(abs(shaded_side - open_side) <= 8);
    test_assert(shaded_center * 2 < shaded_side);
    stbi_image_free(open);
    stbi_image_free(shaded);
}

/** Render a slab of fog around a shadowed point light, with or without a bar 1 wide 1.5 under
 * it, whose shadow covers the cells of the grid next to the middle of the image.
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
        ecs_set(world, bar, Box, {1, 0.2f, 4});
    }

    unsigned char *px = fv_render_still(world, file);
    ecs_fini(world);

    return px;
}

/** A bar under a shadowed point light leaves a dark wedge in the fog under it, while the fog
 * beside the wedge stays as lit, in a frame of cells sampled at their centers: the wedge is
 * about a cell and a half wide, which the cells averaged over many frames soften. */
void FogVolume_point_shadow_in_fog(void) {
    // fog_volume/lights
    unsigned char *open = fv_point_shadow_render(false, "fog_point_shadow_open.png");
    unsigned char *shaded = fv_point_shadow_render(true, "fog_point_shadow.png");
    int32_t open_center = fv_px(open, 32, 32)[1], shaded_center = fv_px(shaded, 32, 32)[1];
    int32_t open_side = fv_px(open, 44, 32)[1], shaded_side = fv_px(shaded, 44, 32)[1];
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

/** Count the forward passes and fog volumes of two frames of a volume with or without 16 point
 * and spot lights in it.
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

/** Count the forward passes, draws, fog dispatches and fog volumes of two frames of a view with
 * one volume or with a second one that overlaps it.
 *
 * @param second Whether the second volume is there
 * @param out Receives the four counts
 */
static void fv_overlap_counts(
    bool second,
    int32_t out[4])
{
    ecs_world_t *world = fv_world(0.3f);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){8, 8, 3}, 0.1f, 0);
    if (second) {
        fv_volume(world, (float[]){1, 0, -9}, (float[]){6, 6, 3}, 0.1f, 0);
    }

    unsigned char *px = fv_render(world, second ? "fog_overlap_two.png" : "fog_overlap_one.png");
    stbi_image_free(px);
    box_render_gpu_stats_t before = fv_frames(world, 0);
    box_render_gpu_stats_t after = fv_frames(world, 2);
    out[0] = after.forward_passes - before.forward_passes;
    out[1] = after.draws - before.draws;
    out[2] = after.fog_dispatches - before.fog_dispatches;
    out[3] = after.fog_volumes - before.fog_volumes;
    test_int(after.errors, before.errors);
    ecs_fini(world);
}

/** A second volume that overlaps the first adds no pass, no draw and no dispatch: both are
 * lit in the same grid and applied in the same draw. */
void FogVolume_overlap_no_new_pass(void) {
    // fog_volume/draw
    int32_t one[4], two[4];
    fv_overlap_counts(false, one);
    fv_overlap_counts(true, two);
    test_int(one[0], 4);
    test_int(two[0], one[0]);
    test_int(two[1], one[1]);
    test_int(one[2], 4);
    test_int(two[2], one[2]);
    test_int(one[3], 2);
    test_int(two[3], 4);
}

/** Render the gray clear color of a view, the sky, through a purely absorbing volume from 5 to
 * 105 units in front of the camera, with a fog distance, and get the green of the center pixel.
 *
 * @param distance LightSettings.fog_distance of the view
 * @param file Name of the PNG
 * @return The green of the center pixel
 */
static int32_t fv_sky(
    float distance,
    const char *file)
{
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = fv_camera(world, 0, 0),
        .clear = {.enabled = true, .color = {FV_WALL, FV_WALL, FV_WALL, 255}},
        .lighting = {.enabled = true, .fog_volumes = true, .fog_distance = distance}});
    fv_volume(world, (float[]){0, 0, -55}, (float[]){100, 100, 50}, 0.05f, 1);
    unsigned char *px = fv_render(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** A pixel without depth takes the fog up to the fog distance of the view: the sky through a
 * volume 100 units deep passes the light of the part of the volume within the distance. */
void FogVolume_fog_distance_sky(void) {
    // fog_volume/froxels
    const float distances[3] = { 20, 60, 200 };
    int32_t got[3], i;
    for (i = 0; i < 3; i ++) {
        char file[64];
        snprintf(file, sizeof(file), "fog_distance_%d.png", (int32_t)distances[i]);
        got[i] = fv_sky(distances[i], file);
        int32_t want = fv_srgb(fv_lin(FV_WALL) *
            fv_grid_transmittance(0.05f, 5, 105, 0, false, distances[i]));
        printf("fog distance %.0f: got %d, want %d\n", (double)distances[i], got[i], want);
        test_assert(abs(got[i] - want) <= 4);
    }

    // About 15, 55 and all 100 units of the volume
    test_assert(abs(got[0] - fv_srgb(fv_lin(FV_WALL) * expf(-0.05f * 15.0f))) <= 8);
    test_assert(got[0] > got[1] + 40);
    test_assert(got[1] > got[2] + 20);
    test_assert(got[2] <= 16);
}

/** The grid has a cell per 8x8 pixels of a 720p and a 1080p view, and no more cells for a 4K
 * view, whose cells are 16 pixels. */
void FogVolume_grid_size_4k(void) {
    // fog_volume/froxels
    const int32_t sizes[3][4] = {
        { 1280, 720, 160, 90 },
        { 1920, 1080, 240, 135 },
        { 3840, 2160, 240, 135 }
    };
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    ecs_entity_t s = fv_surface(world, sizes[0][0], sizes[0][1], NULL);
    box_render_gpu_stats_t first = fv_frames(world, 0);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, s, Surface, {.width = sizes[i][0], .height = sizes[i][1],
            .resolution_scale = 1});
        box_render_gpu_stats_t before = fv_frames(world, 2);
        box_render_gpu_stats_t after = fv_frames(world, 1);
        test_int(after.fog_grid[0], sizes[i][2]);
        test_int(after.fog_grid[1], sizes[i][3]);
        test_int(after.fog_grid[2], BOX_FROXEL_SLICES);
        test_int(after.fog_dispatches - before.fog_dispatches, 2);
        test_int(after.errors, first.errors);
    }

    ecs_fini(world);
}

/** Consumer kind of the history pair of the froxel grid of a view,
 * BOX_GPU_HISTORY_FOG_VOLUMES of the backend */
#define FV_HISTORY_FOG (2)

/** History pairs read at most */
#define FV_HISTORY_PAIRS (8)

/** Get the history pair of the froxel grid of a view.
 *
 * @param world The world
 * @param view The view
 * @param out Receives the pair
 * @return Whether the pair exists
 */
static bool fv_history(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_gpu_history_pair_t *out)
{
    box_render_gpu_history_pair_t pairs[FV_HISTORY_PAIRS];
    int32_t i, count = box_render_gpu_history_pairs(world, pairs, FV_HISTORY_PAIRS);
    bool result = false;
    for (i = 0; i < count && i < FV_HISTORY_PAIRS && !result; i ++) {
        if (pairs[i].consumer == FV_HISTORY_FOG && pairs[i].view == view) {
            *out = pairs[i];
            result = true;
        }
    }

    return result;
}

/** Run one frame and load the square capture it wrote.
 *
 * @param world The world
 * @param file Name of the PNG of the surface
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fv_next(
    ecs_world_t *world,
    const char *file)
{
    fv_frames(world, 1);

    return fv_load(world, file, FV_SIZE, FV_SIZE);
}

/** Mean absolute difference of the green of two square captures inside a margin of 4 pixels.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The mean difference
 */
static double fv_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    double diff = 0;
    int32_t x, y, count = 0;
    for (y = 4; y < FV_SIZE - 4; y ++) {
        for (x = 4; x < FV_SIZE - 4; x ++) {
            diff += abs((int32_t)fv_px(a, x, y)[1] - (int32_t)fv_px(b, x, y)[1]);
            count ++;
        }
    }

    return diff / count;
}

/** Largest difference of the green of two square captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The largest difference
 */
static int32_t fv_max_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, result = 0;
    for (i = 0; i < FV_SIZE * FV_SIZE; i ++) {
        int32_t d = abs((int32_t)a[i * 4 + 1] - (int32_t)b[i * 4 + 1]);
        result = d > result ? d : result;
    }

    return result;
}

/** Build the scene of the temporal cases: the sun shining straight down through a volume with
 * noise and without wind, a roof over its left side that leaves a shadow edge at x 2, seen by a
 * camera at the origin turned about +Y.
 *
 * @param world The world
 * @param yaw Rotation of the camera about +Y
 * @param camera Receives the camera
 * @return The view
 */
static ecs_entity_t fv_history_scene(
    ecs_world_t *world,
    float yaw,
    ecs_entity_t *camera)
{
    *camera = fv_camera(world, 0, yaw);
    ecs_entity_t view = fv_view(world, *camera, true);
    View *v = ecs_get_mut(world, view, View);
    v->shadows = fv_shadows;
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = fv_volume(world, (float[]){0, 0, -15}, (float[]){8, 4, 6}, 0.2f, 0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->asymmetry = 0;
    f->noise_strength = 0.6f;
    f->noise_scale = 3;
    ecs_modified(world, e, FogVolume);
    ecs_entity_t roof = ecs_new(world);
    ecs_set(world, roof, Position, {-3, 8, -15});
    ecs_set(world, roof, Box, {10, 0.5f, 20});

    return view;
}

/** With a static camera the cells converge: a frame differs from the frame a sequence period
 * later far less after 40 frames than at the start, and consecutive frames differ little. */
void FogVolume_history_converges(void) {
    // fog_volume/temporal
    const int32_t period = 16;
    const char *file = "fog_converge.png";
    unsigned char *px[64];
    ecs_entity_t camera;
    ecs_world_t *world = fv_world(0);
    fv_history_scene(world, 0, &camera);
    box_render_gpu_stats_t before = fv_frames(world, 0);
    fv_surface(world, FV_SIZE, FV_SIZE, file);
    int32_t i;
    for (i = 0; i < 64; i ++) {
        px[i] = fv_next(world, file);
    }

    double early = fv_diff(px[0], px[period]);
    double late = fv_diff(px[40], px[40 + period]);
    double step = 0;
    for (i = 48; i < 63; i ++) {
        step += fv_diff(px[i], px[i + 1]) / 15;
    }

    printf("period difference early %.3f, late %.3f, consecutive late %.3f\n",
        early, late, step);
    test_assert(early > 0);
    test_assert(late * 4 < early);
    test_assert(step < 1.5);
    test_int(fv_frames(world, 0).errors, before.errors);
    for (i = 0; i < 64; i ++) {
        stbi_image_free(px[i]);
    }

    ecs_fini(world);
}

/** The first frame and the frame after a HistoryReset sample every cell at its center without
 * the previous cells, so they render the same pixels; the frames between them blend. */
void FogVolume_history_reset(void) {
    // fog_volume/temporal
    const char *file = "fog_history_reset.png";
    box_render_gpu_history_pair_t pair;
    ecs_entity_t camera;
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = fv_history_scene(world, 0, &camera);
    fv_surface(world, FV_SIZE, FV_SIZE, file);
    unsigned char *first = fv_next(world, file);
    test_assert(fv_history(world, view, &pair));
    test_bool(pair.valid, false);
    test_int(pair.depth, BOX_FROXEL_SLICES);

    fv_frames(world, 19);
    unsigned char *blended = fv_next(world, file);
    test_assert(fv_history(world, view, &pair));
    test_bool(pair.valid, true);
    printf("blended frame differs by %.3f\n", fv_diff(first, blended));
    test_assert(fv_diff(first, blended) > 0);

    ecs_add(world, view, HistoryReset);
    unsigned char *reset = fv_next(world, file);
    test_assert(fv_history(world, view, &pair));
    test_bool(pair.valid, false);
    printf("reset frame differs by at most %d\n", fv_max_diff(first, reset));
    test_assert(fv_max_diff(first, reset) <= 1);

    stbi_image_free(first);
    stbi_image_free(blended);
    stbi_image_free(reset);
    ecs_fini(world);
}

/** The column of a square capture where the green, averaged down its middle rows, first rises
 * above halfway from its darkest to its brightest column: the shadow edge of the shaft of
 * fv_history_scene.
 *
 * @param px The pixels
 * @return The column, with the fraction of the pixel the crossing lies at
 */
static double fv_edge(
    const unsigned char *px)
{
    double cols[FV_SIZE], lo = 1e30, hi = -1e30, result = -1;
    int32_t x, y;
    for (x = 0; x < FV_SIZE; x ++) {
        cols[x] = 0;
        for (y = 16; y < 48; y ++) {
            cols[x] += fv_px(px, x, y)[1];
        }

        cols[x] /= 32;
        lo = fmin(lo, cols[x]);
        hi = fmax(hi, cols[x]);
    }

    double half = (lo + hi) / 2;
    for (x = 1; x < FV_SIZE && result < 0; x ++) {
        if (cols[x - 1] < half && cols[x] >= half) {
            result = x - 1 + (half - cols[x - 1]) / (cols[x] - cols[x - 1]);
        }
    }

    return result;
}

/** A camera that turns and stops shows the shadow edge of the shaft where a camera that stood at
 * its final rotation all along shows it: the previous cells are found where they were in the
 * world, so the edge does not trail behind the turn: two frames after it stopped it is within a
 * pixel and a half, where the cells of the old rotation, blended in at the same rate without
 * being reprojected, would leave it several behind. */
void FogVolume_history_camera_turns(void) {
    // fog_volume/temporal
    const float yaw = 0.12f;
    const int32_t steps = 10;
    ecs_entity_t camera;
    ecs_world_t *world = fv_world(0);
    fv_history_scene(world, yaw, &camera);
    fv_surface(world, FV_SIZE, FV_SIZE, "fog_turn_ref.png");
    fv_frames(world, 41);
    unsigned char *ref = fv_load(world, "fog_turn_ref.png", FV_SIZE, FV_SIZE);
    ecs_fini(world);

    world = fv_world(0);
    fv_history_scene(world, 0, &camera);
    fv_surface(world, FV_SIZE, FV_SIZE, "fog_turn.png");
    fv_frames(world, 30);
    unsigned char *start = fv_load(world, "fog_turn.png", FV_SIZE, FV_SIZE);
    int32_t i;
    for (i = 1; i <= steps; i ++) {
        ecs_set(world, camera, Rotation, {0, yaw * (float)i / (float)steps, 0});
        fv_frames(world, 1);
    }

    fv_frames(world, 2);
    unsigned char *turned = fv_load(world, "fog_turn.png", FV_SIZE, FV_SIZE);
    double from = fv_edge(start), want = fv_edge(ref), got = fv_edge(turned);
    printf("edge from %.2f to %.2f, turned camera at %.2f, differs by %.3f on average\n",
        from, want, got, fv_diff(ref, turned));
    test_assert(want - from > 4);
    test_assert(fabs(got - want) < 1.5);
    test_assert(fv_diff(ref, turned) < 6.0);
    stbi_image_free(ref);
    stbi_image_free(start);
    stbi_image_free(turned);
    ecs_fini(world);
}

/** A volume removed beside another one leaves no fog 10 frames later, while the other one keeps
 * its fog. */
void FogVolume_history_removed_volume(void) {
    // fog_volume/temporal
    const char *file = "fog_history_removed.png";
    ecs_world_t *world = fv_world(0);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){-3, 0, -10}, (float[]){2.5f, 2.5f, 1}, 0.08f, 1);
    ecs_entity_t right = fv_volume(world, (float[]){3, 0, -10}, (float[]){2.5f, 2.5f, 1},
        0.08f, 1);
    fv_surface(world, FV_SIZE, FV_SIZE, file);
    fv_frames(world, 30);
    unsigned char *px = fv_load(world, file, FV_SIZE, FV_SIZE);
    int32_t left_before = fv_px(px, 15, 32)[1], right_before = fv_px(px, 48, 32)[1];
    stbi_image_free(px);

    ecs_delete(world, right);
    fv_frames(world, 10);
    px = fv_load(world, file, FV_SIZE, FV_SIZE);
    int32_t left_after = fv_px(px, 15, 32)[1], right_after = fv_px(px, 48, 32)[1];
    stbi_image_free(px);
    printf("left %d -> %d, right %d -> %d, wall %d\n", left_before, left_after,
        right_before, right_after, FV_WALL);
    test_assert(FV_WALL - right_before > 10);
    test_assert(abs(FV_WALL - right_after) <= 3);
    test_assert(abs(left_after - left_before) <= 2);
    ecs_fini(world);
}

/** Two views of one target keep a history pair each: a HistoryReset of one does not reset the
 * other. */
void FogVolume_history_two_views(void) {
    // fog_volume/temporal
    box_render_gpu_history_pair_t a, b;
    ecs_world_t *world = fv_world(0);
    ecs_entity_t first = fv_view(world, fv_camera(world, 0, 0), true);
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, View, {.camera = fv_camera(world, 0, (float)GLM_PI),
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .fog_volumes = true}, .order = 1});
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_wall(world, 0, 20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){8, 8, 2}, 0.1f, 1);
    fv_volume(world, (float[]){0, 0, 10}, (float[]){8, 8, 2}, 0.3f, 1);
    unsigned char *px = fv_render(world, "fog_history_two_views.png");
    fv_expect(fv_px(px, 32, 32), fv_srgb(fv_lin(FV_WALL) * fv_grid(0.3f, 8, 12, 19.9f)), 4);
    stbi_image_free(px);

    test_assert(fv_history(world, first, &a));
    test_assert(fv_history(world, second, &b));
    test_assert(a.read != b.read);
    test_assert(a.write != b.write);
    test_bool(a.valid, true);
    test_bool(b.valid, true);

    ecs_add(world, second, HistoryReset);
    fv_frames(world, 1);
    test_assert(fv_history(world, first, &a));
    test_assert(fv_history(world, second, &b));
    test_bool(a.valid, true);
    test_bool(b.valid, false);
    fv_frames(world, 1);
    test_assert(fv_history(world, second, &b));
    test_bool(b.valid, true);
    ecs_fini(world);
}

/** The first frame at a new size renders the volume as a first frame does, without the cells of
 * the old size. */
void FogVolume_history_resize(void) {
    // fog_volume/temporal
    box_render_gpu_history_pair_t pair;
    ecs_world_t *world = fv_world(0);
    ecs_entity_t view = fv_view(world, fv_camera(world, 0, 0), true);
    fv_wall(world, 0, -20, 40, FV_WALL);
    fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.2f, 8, 12, 19.9f));
    ecs_entity_t s = fv_surface(world, FV_SIZE, FV_SIZE, "fog_history_resize_a.png");
    box_render_gpu_stats_t before = fv_frames(world, 20);
    test_assert(fv_history(world, view, &pair));
    test_bool(pair.valid, true);

    ecs_set(world, s, Surface, {.width = 96, .height = 48,
        .write_to_file = "fog_history_resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = before;
    int32_t i;
    for (i = 0; i < 3 && after.fog_grid[0] != 12; i ++) {
        after = fv_frames(world, 1);
    }

    test_int(after.fog_grid[0], 12);
    test_int(after.fog_grid[1], 6);
    test_assert(fv_history(world, view, &pair));
    test_bool(pair.valid, false);
    test_int(pair.width, 12);
    unsigned char *px = fv_load(world, "fog_history_resize_b.png", 96, 48);
    fv_expect(&px[(24 * 96 + 48) * 4], want, 4);
    stbi_image_free(px);
    test_int(after.errors, before.errors);
    ecs_fini(world);
}

/** Mean size of the bumps of the rows of a square capture: the green of each row averaged over
 * its middle half, and the mean absolute second difference of those means down the rows inside
 * a margin of an eighth. Stripes where a face crosses the depth slices make it large.
 *
 * @param px The pixels
 * @param size Width and height
 * @return The mean bump
 */
static double fv_row_bumps(
    const unsigned char *px,
    int32_t size)
{
    double *rows = ecs_os_malloc_n(double, size), result = 0;
    int32_t x, y, margin = size / 8;
    for (y = 0; y < size; y ++) {
        rows[y] = 0;
        for (x = size / 4; x < size - size / 4; x ++) {
            rows[y] += px[(y * size + x) * 4 + 1];
        }

        rows[y] /= size - 2 * (size / 4);
    }

    for (y = margin; y < size - margin; y ++) {
        result += fabs(rows[y + 1] - 2 * rows[y] + rows[y - 1]);
    }

    ecs_os_free(rows);

    return result / (size - 2 * margin);
}

/** A thin slab with the soft faces of the plain volume of fog_volume_noise.flecs lies on a floor,
 * seen from above at a grazing angle like there: its top face and the floor cross the depth
 * slices at different depths along each row, which steps the fog into stripes when a slice is
 * weighed by depth alone. After the history converged the rows change smoothly. */
void FogVolume_history_grazing_slab(void) {
    // fog_volume/temporal
    const char *file = "fog_grazing_slab.png";
    const int32_t size = 256;
    ecs_world_t *world = fv_world(1);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 11});
    ecs_set(world, camera, Rotation, {-0.75f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = FV_NEAR, .far_ = 1000});
    fv_view(world, camera, true);
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {80, 0.2f, 80});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, floor, Emissive, {.strength = 1, .color = {30, 30, 30}});
    ecs_entity_t e = fv_volume(world, (float[]){0, 0.75f, 0}, (float[]){40, 0.75f, 40}, 0.35f,
        0);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->asymmetry = 0;
    f->edge_fade = 0.4f;
    ecs_modified(world, e, FogVolume);
    fv_surface(world, size, size, file);
    fv_frames(world, 1);
    unsigned char *first = fv_load(world, file, size, size);
    fv_frames(world, 39);
    unsigned char *converged = fv_load(world, file, size, size);
    double before = fv_row_bumps(first, size), after = fv_row_bumps(converged, size);
    printf("row bumps first %.3f, converged %.3f, center %d\n", before, after,
        converged[(size / 2 * size + size / 2) * 4 + 1]);
    test_assert(converged[(size / 2 * size + size / 2) * 4 + 1] > 60);
    test_assert(after < 0.2);
    test_assert(after <= before);
    stbi_image_free(first);
    stbi_image_free(converged);
    ecs_fini(world);
}

/** Add a blended panel 0.2 thick facing the camera: black albedo with a gray emission, drawn at
 * opacity 0.5.
 *
 * @param world The world
 * @param z Z of its center
 * @param size Width and height
 * @param gray Emission, display encoded
 * @return The panel
 */
static ecs_entity_t fv_blended_panel(
    ecs_world_t *world,
    float z,
    float size,
    uint8_t gray)
{
    ecs_entity_t e = fv_wall(world, 0, z, size, gray);
    ecs_set(world, e, Opacity, {0.5f});

    return e;
}

/** Render a blended panel 3 units across with nothing behind it, with or without a volume from
 * z -6 to -14, and get the green of the center pixel, on the panel, and of a pixel at the left
 * edge beside it.
 *
 * @param ambient Intensity of the white ambient
 * @param volume Whether the volume is there
 * @param z Z of the panel
 * @param gray Emission of the panel, display encoded
 * @param density Extinction per unit of the volume
 * @param absorption Share of the extinction that is absorbed
 * @param file Name of the PNG
 * @param beside Receives the green of the pixel beside the panel, may be NULL
 * @return The green of the center pixel
 */
static int32_t fv_blended(
    float ambient,
    bool volume,
    float z,
    uint8_t gray,
    float density,
    float absorption,
    const char *file,
    int32_t *beside)
{
    ecs_world_t *world = fv_world(ambient);
    fv_view(world, fv_camera(world, 0, 0), true);
    fv_blended_panel(world, z, 3, gray);
    if (volume) {
        fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, density, absorption);
    }

    unsigned char *px = fv_render_still(world, file);
    int32_t result = fv_px(px, 32, 32)[1];
    if (beside) {
        *beside = fv_px(px, 4, 32)[1];
    }

    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** A blended panel inside a purely absorbing volume is dimmed by the part of the volume in front
 * of it, as an opaque panel at its depth, where before it drew at full strength. */
void FogVolume_blended_inside_fogged(void) {
    // fog_volume/blended
    int32_t clear = fv_blended(0, false, -10, FV_WALL, 0.3f, 1, "fog_blend_in_none.png", NULL);
    int32_t fogged = fv_blended(0, true, -10, FV_WALL, 0.3f, 1, "fog_blend_in.png", NULL);
    int32_t want = fv_srgb(0.5f * fv_lin(FV_WALL) * fv_grid(0.3f, 6, 14, 9.9f));
    printf("clear %d, fogged %d, want %d\n", clear, fogged, want);
    test_assert(abs(clear - fv_srgb(0.5f * fv_lin(FV_WALL))) <= 4);
    test_assert(abs(fogged - want) <= 8);
    test_assert(fogged < clear - 20);
}

/** A blended panel behind a purely absorbing volume is dimmed by the whole depth of the volume. */
void FogVolume_blended_behind_fogged(void) {
    // fog_volume/blended
    int32_t clear = fv_blended(0, false, -20, FV_WALL, 0.3f, 1, "fog_blend_behind_none.png",
        NULL);
    int32_t fogged = fv_blended(0, true, -20, FV_WALL, 0.3f, 1, "fog_blend_behind.png", NULL);
    int32_t want = fv_srgb(0.5f * fv_lin(FV_WALL) * fv_grid(0.3f, 6, 14, 19.9f));
    printf("clear %d, fogged %d, want %d\n", clear, fogged, want);
    test_assert(abs(fogged - want) <= 8);
    test_assert(fogged < clear - 30);
}

/** A dark blended panel inside and one behind a dense scattering volume lit by the ambient take
 * the color of the fog in front of them, as the fog beside them, where without the volume they
 * show black. */
void FogVolume_blended_fog_color(void) {
    // fog_volume/blended
    float z[2] = { -10, -20 };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        int32_t beside = 0;
        int32_t clear = fv_blended(0.5f, false, z[i], 0, 20, 0, "fog_blend_color_none.png",
            NULL);
        int32_t fogged = fv_blended(0.5f, true, z[i], 0, 20, 0, "fog_blend_color.png",
            &beside);
        printf("z %.0f: clear %d, fogged %d, beside %d\n", (double)z[i], clear, fogged, beside);
        test_assert(clear < 10);
        test_assert(beside > 80);
        test_assert(abs(fogged - beside) <= 6);
    }
}

/** A blended panel in front of a volume, between it and the camera, is not fogged by it. */
void FogVolume_blended_in_front_unchanged(void) {
    // fog_volume/blended
    int32_t clear = fv_blended(0, false, -4, FV_WALL, 0.3f, 1, "fog_blend_front_none.png",
        NULL);
    int32_t fogged = fv_blended(0, true, -4, FV_WALL, 0.3f, 1, "fog_blend_front.png", NULL);
    printf("clear %d, fogged %d\n", clear, fogged);
    test_assert(abs(fogged - clear) <= 3);
}

/** Add an additive or alpha pool of gray particles at rest in a box around z -10.
 *
 * @param world The world
 * @param blend The ParticleBlend
 * @return The emitter
 */
static ecs_entity_t fv_particles(
    ecs_world_t *world,
    ParticleBlend blend)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, -10});
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 6000;
    p->max = 200;
    p->shape = EmitterBox;
    glm_vec3_copy((vec3){2, 2, 0.5f}, p->extents);
    glm_vec3_zero(p->direction);
    p->lifetime = (ParticleRange){100, 100};
    p->size = (ParticleRange){0.6f, 0.6f};
    p->opacity = (ParticleRange){0.5f, 0.5f};
    p->color = (rgb){160, 160, 160};
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleAppearance, {.blend = blend});

    return e;
}

/** Sum the green of every pixel of a square capture.
 *
 * @param px The pixels
 * @return The sum
 */
static int64_t fv_green_sum(
    const unsigned char *px)
{
    int64_t result = 0;
    int32_t i;
    for (i = 0; i < FV_SIZE * FV_SIZE; i ++) {
        result += px[i * 4 + 1];
    }

    return result;
}

/** An additive pool inside a purely absorbing volume adds less light than without the volume:
 * the fog in front of each particle dims it. */
void FogVolume_additive_particles_dimmed(void) {
    // fog_volume/blended
    const char *files[2] = { "fog_particles_none.png", "fog_particles.png" };
    int64_t sums[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_camera(world, 0, 0), true);
        fv_particles(world, ParticleAdditive);
        if (k) {
            fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
        }

        unsigned char *px = fv_render_still(world, files[k]);
        sums[k] = fv_green_sum(px);
        stbi_image_free(px);
        ecs_fini(world);
    }

    printf("additive green without fog %lld, in fog %lld\n", (long long)sums[0],
        (long long)sums[1]);
    test_assert(sums[0] > 2000);
    test_assert(sums[1] * 10 < sums[0] * 8);
    test_assert(sums[1] > 0);
}

/** A view without fog volumes draws blended panels, alpha and additive particles the same
 * whether it renders fog volumes or not, and whether its scene has a volume it does not render. */
void FogVolume_no_fog_blended_unchanged(void) {
    // fog_volume/blended
    const char *files[3] = {
        "fog_blend_none_off.png", "fog_blend_none_on.png", "fog_blend_none_hidden.png"
    };
    unsigned char *px[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        ecs_world_t *world = fv_world(0.3f);
        fv_view(world, fv_camera(world, 0, 0), k == 1);
        fv_wall(world, 0, -20, 40, 60);
        fv_blended_panel(world, -8, 3, FV_WALL);
        fv_particles(world, ParticleAdditive);
        ecs_set(world, fv_particles(world, ParticleAlpha), Position, {1, 0, -12});
        if (k == 2) {
            fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
        }

        px[k] = fv_render(world, files[k]);
        box_render_gpu_stats_t after = fv_frames(world, 0);
        test_int(after.fog_volumes, 0);
        ecs_fini(world);
    }

    test_assert(!memcmp(px[0], px[1], FV_SIZE * FV_SIZE * 4));
    test_assert(!memcmp(px[0], px[2], FV_SIZE * FV_SIZE * 4));
    for (k = 0; k < 3; k ++) {
        stbi_image_free(px[k]);
}


/** A box volume with its shape, height_falloff and edge_noise at their defaults renders the
 * transmittance of its constant density, and an edge_noise that has nothing to erode, without
 * noise or with hard faces, renders the same pixels as without it. */
void FogVolume_falloff_defaults_unchanged(void) {
    // fog_volume/falloff
    const char *files[6] = {
        "fog_falloff_default.png", "fog_falloff_default_edge.png",
        "fog_falloff_hard_noise.png", "fog_falloff_hard_noise_edge.png",
        "fog_falloff_fade.png", "fog_falloff_fade_edge.png" };
    unsigned char *px[6];
    int32_t k;
    for (k = 0; k < 6; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_camera(world, 0, 0), true);
        fv_wall(world, 0, -20, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, -10}, (float[]){20, 20, 2}, 0.2f, 1);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        test_int(f->shape, FogVolumeBox);
        test_flt(f->height_falloff, 0);
        test_flt(f->edge_noise, 0);
        /* Pairs without and with edge_noise: no noise, hard faces with noise, a fade without
         * noise */
        if (k / 2 == 1) {
            f->noise_strength = 1;
            f->noise_scale = 2;
        } else if (k / 2 == 2) {
            f->edge_fade = 1;
        }

        if (k % 2) {
            f->edge_noise = 1;
        }

        ecs_modified(world, e, FogVolume);
        px[k] = fv_render_still(world, files[k]);
        ecs_fini(world);
    }

    int32_t want = fv_srgb(fv_lin(FV_WALL) * fv_grid(0.2f, 8, 12, 19.9f));
    fv_expect(fv_px(px[0], 32, 32), want, 4);
    fv_expect(fv_px(px[0], 40, 24), want, 6);
    for (k = 0; k < 6; k += 2) {
        test_assert(!memcmp(px[k], px[k + 1], FV_SIZE * FV_SIZE * 4));
    }

    for (k = 0; k < 6; k ++) {
        stbi_image_free(px[k]);
    }
}

/** Blended panels and particles in a view with a volume add no render pass and no dispatch of
 * their own: the volume alone ends the pass once for the apply draw (two per frame), and the draws
 * after it continue in the one more pass that followed the apply before (three per frame), with
 * the dispatches of the volume alone. */
void FogVolume_blended_no_new_pass(void) {
    // fog_volume/blended
    int32_t passes[2], dispatches[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_camera(world, 0, 0), true);
        fv_wall(world, 0, -20, 40, FV_WALL);
        fv_volume(world, (float[]){0, 0, -10}, (float[]){10, 10, 4}, 0.3f, 1);
        if (k) {
            fv_blended_panel(world, -10, 3, FV_WALL);
            fv_particles(world, ParticleAlpha);
        }

        unsigned char *px = fv_render(world, k ? "fog_blend_passes.png" : "fog_passes.png");
        stbi_image_free(px);
        box_render_gpu_stats_t before = fv_frames(world, 0);
        box_render_gpu_stats_t after = fv_frames(world, 2);
        passes[k] = after.forward_passes - before.forward_passes;
        dispatches[k] = after.fog_dispatches - before.fog_dispatches;
        test_int(after.errors, before.errors);
        ecs_fini(world);
    }

    test_int(passes[0], 4);
    test_int(passes[1], 6);
    test_int(dispatches[1], dispatches[0]);
    test_int(dispatches[0], 4);
}

/** An ellipsoid of hard faces in an orthographic view fogs the ray through the center of its box
 * as the box does, and leaves the ray through a corner of the box, outside the ellipsoid,
 * unfogged, which the box fogs. */
void FogVolume_ellipsoid_corner(void) {
    // fog_volume/falloff
    const char *files[2] = { "fog_ellipsoid_box.png", "fog_ellipsoid.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_ortho(world), true);
        fv_wall(world, 0, -10, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){6, 6, 2}, 0.5f, 1);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->shape = k ? FogVolumeEllipsoid : FogVolumeBox;
        ecs_modified(world, e, FogVolume);
        px[k] = fv_render_still(world, files[k]);
        ecs_fini(world);
    }

    /* The corner at x and y 5.5, 4 pixels a unit from the center of the image, where the
     * box fogs a share of the cells the filtering of the grid blends with the wall */
    int32_t want = fv_srgb(fv_lin(FV_WALL) *
        fv_grid_transmittance(0.5f, 8, 12, 19.9f, true, 100));
    printf("center box %d ellipsoid %d, corner box %d ellipsoid %d, want %d\n",
        fv_px(px[0], 32, 32)[1], fv_px(px[1], 32, 32)[1], fv_px(px[0], 54, 10)[1],
        fv_px(px[1], 54, 10)[1], want);
    fv_expect(fv_px(px[0], 32, 32), want, 6);
    fv_expect(fv_px(px[1], 32, 32), want, 6);
    test_assert(fv_px(px[0], 54, 10)[1] < FV_WALL - 40);
    test_assert(fv_px(px[0], 10, 54)[1] < FV_WALL - 40);
    fv_expect(fv_px(px[1], 54, 10), FV_WALL, 2);
    fv_expect(fv_px(px[1], 10, 54), FV_WALL, 2);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A camera inside an ellipsoid long along the view sees the fog of the wall through its
 * center as through the box, and less fog toward the corners of the image, whose rays leave the
 * ellipsoid before they leave its box. */
void FogVolume_ellipsoid_camera_inside(void) {
    // fog_volume/falloff
    const char *files[2] = { "fog_ellipsoid_inside_box.png", "fog_ellipsoid_inside.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_camera(world, 0, 0), true);
        fv_wall(world, 0, -20, 60, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){6, 6, 30}, 0.1f, 1);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->shape = k ? FogVolumeEllipsoid : FogVolumeBox;
        ecs_modified(world, e, FogVolume);
        px[k] = fv_render_still(world, files[k]);
        ecs_fini(world);
    }

    int32_t center = fv_px(px[1], 32, 32)[1];
    printf("center box %d ellipsoid %d, corner box %d ellipsoid %d\n",
        fv_px(px[0], 32, 32)[1], center, fv_px(px[0], 4, 4)[1], fv_px(px[1], 4, 4)[1]);
    test_assert(center < FV_WALL - 60);
    test_assert(abs(center - (int32_t)fv_px(px[0], 32, 32)[1]) <= 6);
    test_assert(fv_px(px[1], 4, 4)[1] > fv_px(px[0], 4, 4)[1] + 12);
    test_assert(fv_px(px[1], 59, 59)[1] > fv_px(px[0], 59, 59)[1] + 12);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** With a height_falloff the fog thins toward the top of the box: the wall shows through a ray
 * across the top of the volume more than through one across its bottom, which fog without it
 * dims alike. */
void FogVolume_height_falloff(void) {
    // fog_volume/falloff
    const char *files[2] = { "fog_height_none.png", "fog_height.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_ortho(world), true);
        fv_wall(world, 0, -10, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){20, 6, 2}, 0.5f, 1);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->height_falloff = k ? 0.5f : 0.0f;
        ecs_modified(world, e, FogVolume);
        px[k] = fv_render_still(world, files[k]);
        ecs_fini(world);
    }

    /* Rows 16 and 48 are 4 units above and below the center: 10 and 2 above the bottom */
    int32_t top[2], bottom[2];
    for (k = 0; k < 2; k ++) {
        top[k] = fv_px(px[k], 32, 16)[1];
        bottom[k] = fv_px(px[k], 32, 48)[1];
    }

    printf("no falloff top %d bottom %d, falloff top %d bottom %d\n", top[0], bottom[0],
        top[1], bottom[1]);
    test_assert(abs(top[0] - bottom[0]) <= 2);
    test_assert(top[1] > bottom[1] + 30);
    test_assert(bottom[1] > bottom[0] + 10);
    test_assert(top[1] > FV_WALL - 20);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Deviation of the green of the columns of a square capture along them, between two pairs of
 * columns, left and right of the center, over the rows inside a margin of 4 pixels: the root
 * mean square of each pixel's difference from the mean of its column, so the fade across the
 * columns does not count.
 *
 * @param px The pixels
 * @param from First column of the left band, mirrored for the right one
 * @param to Column after the last of the left band
 * @return The deviation
 */
static double fv_band_deviation(
    const unsigned char *px,
    int32_t from,
    int32_t to)
{
    double sq = 0;
    int32_t x, y, side, count = 0;
    for (x = from; x < to; x ++) {
        for (side = 0; side < 2; side ++) {
            int32_t c = side ? FV_SIZE - 1 - x : x;
            double sum = 0;
            for (y = 4; y < FV_SIZE - 4; y ++) {
                sum += fv_px(px, c, y)[1];
            }

            double mean = sum / (FV_SIZE - 8);
            for (y = 4; y < FV_SIZE - 4; y ++) {
                double d = fv_px(px, c, y)[1] - mean;
                sq += d * d;
                count ++;
            }
        }
    }

    return sqrt(sq / count);
}

/** An orthographic camera inside a noisy volume narrow along X looks at a wall a few units
 * away inside it: the pixels of the rays through the faded border at the sides vary more along
 * the border with an edge_noise of 1 than without, and the center, whose rays stay deeper
 * inside than edge_fade, is unchanged. */
void FogVolume_edge_noise(void) {
    // fog_volume/falloff
    const char *files[2] = { "fog_edge_noise_none.png", "fog_edge_noise.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = fv_world(0);
        fv_view(world, fv_ortho(world), true);
        fv_wall(world, 0, 7, 40, FV_WALL);
        ecs_entity_t e = fv_volume(world, (float[]){0, 0, 0}, (float[]){7, 20, 20}, 0.8f, 1);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->edge_fade = 5;
        f->noise_strength = 1;
        f->noise_scale = 4;
        f->edge_noise = (float)k;
        ecs_modified(world, e, FogVolume);
        px[k] = fv_render_still(world, files[k]);
        ecs_fini(world);
    }

    /* The fade between x 2 and 7, 4 pixels a unit from the center of the image */
    double border[2];
    for (k = 0; k < 2; k ++) {
        border[k] = fv_band_deviation(px[k], 4, 20);
    }

    printf("border deviation edge_noise 0 %.2f, 1 %.2f, center %d %d\n", border[0], border[1],
        fv_px(px[0], 32, 32)[1], fv_px(px[1], 32, 32)[1]);
    test_assert(border[1] > border[0] * 1.5 + 1.0);
    test_assert(fv_px(px[0], 32, 32)[1] < FV_WALL - 20);
    fv_expect(fv_px(px[1], 32, 32), fv_px(px[0], 32, 32)[1], 1);
    fv_expect(fv_px(px[1], 31, 31), fv_px(px[0], 31, 31)[1], 1);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}
