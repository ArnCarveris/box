/**
 * @file ViewDebug.c
 * @brief Tests for the debug pass of a view (View.debug) and its infinite grid: no pass without
 * debug items, lines at every cell with their width in pixels, major lines and axes, the depth of
 * the view, the fade with the distance and toward the horizon, the height of the plane,
 * orthographic cameras, several views of a target, effects, captures, a camera under the plane
 * and resizes; and for its wireframe: no change while off, the edges and diagonals of the
 * triangles, hidden lines of the overlay, WireframeOnly without shaded meshes and occlusion, the
 * width in pixels, parametric surfaces, instances, blended and transmissive meshes, 2D shapes
 * that are not wired, several views and replaced meshes; checked on PNG captures.
 *
 * Most cases look straight down from 10 units with a 60 degree field of view into a black 128
 * pixel capture: 11.09 pixels a unit, +X right and +Z down, the origin at the center, white
 * minor lines every unit and green major lines every 5 units at full alpha.
 *
 * The world has no ambient light and no light in reach: geometry shows its emission only.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define DG_SIZE (128)

/** Pixels a unit of the plane 10 units under the camera */
#define DG_SCALE (64.0f / (10.0f * 0.57735027f))

/** Grid settings of the cases: white minor lines every unit, green major lines every 5, no fade
 * within the view */
static const GridSettings dg_grid = {
    .enabled = true, .cell_size = 1, .major_every = 5, .fade_distance = 1000,
    .line_width = 1, .color = {255, 255, 255, 255}, .major_color = {0, 255, 0, 255}
};

/** Create a headless world that renders on the main thread, without ambient light and with a
 * point light out of reach.
 *
 * @return The world
 */
static ecs_world_t* dg_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    return world;
}

/** Add a perspective camera at a height above the origin, pitched about +X.
 *
 * @param world The world
 * @param y Height of the camera
 * @param pitch Rotation about +X, -pi/2 looks straight down, 0 down -Z
 * @return The camera
 */
static ecs_entity_t dg_camera(
    ecs_world_t *world,
    float y,
    float pitch)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, y, 0});
    ecs_set(world, camera, Rotation, {pitch, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 2000});

    return camera;
}

/** Add a lit view of a camera that clears to black and draws every instance.
 *
 * @param world The world
 * @param camera The camera
 * @param grid Grid settings of the view, NULL for none
 * @return The view
 */
static ecs_entity_t dg_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    const GridSettings *grid)
{
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});
    if (grid) {
        ecs_get_mut(world, view, View)->debug.grid = *grid;
        ecs_modified(world, view, View);
    }

    return view;
}

/** Add a box of black albedo that shows its emission.
 *
 * @param world The world
 * @param pos Center
 * @param size Size along each axis
 * @param color Emission, display encoded
 * @return The box
 */
static ecs_entity_t dg_box(
    ecs_world_t *world,
    const float pos[3],
    const float size[3],
    Rgb color)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {pos[0], pos[1], pos[2]});
    ecs_set(world, e, Box, {size[0], size[1], size[2]});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = color});

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
static ecs_entity_t dg_surface(
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
static box_render_gpu_stats_t dg_frames(
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
static unsigned char* dg_load(
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
static unsigned char* dg_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = dg_frames(world, 0), after;
    dg_surface(world, DG_SIZE, DG_SIZE, file);
    after = dg_frames(world, 3);
    test_int(after.errors, before.errors);
    test_int(after.shader_errors, before.shader_errors);

    return dg_load(world, file, DG_SIZE, DG_SIZE);
}

/** Convert a display encoded byte to linear light.
 *
 * @param c The byte
 * @return The linear value
 */
static float dg_lin(
    int32_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Get a pixel of a square capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The pixel, RGBA
 */
static const unsigned char* dg_px(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * DG_SIZE + x) * 4];
}

/** Column or row of a square capture that a coordinate of the plane falls in, for a camera
 * looking down at a number of pixels a unit.
 *
 * @param world Coordinate along X (columns) or Z (rows)
 * @param scale Pixels a unit
 * @return The column or row, as a continuous coordinate
 */
static float dg_pos(
    float world,
    float scale)
{
    return (float)DG_SIZE * 0.5f + world * scale;
}

/** A line of a square capture across a grid line, along a row or a column. */
typedef struct dg_scan_t {
    float sum[3];           /**< Sum of the linear channels */
    int32_t peak;           /**< Column or row of the brightest pixel */
    float peak_value;       /**< Largest linear sum of the channels */
} dg_scan_t;

/** Sum the linear channels of the pixels of a row or a column between two positions.
 *
 * @param px The pixels
 * @param fixed Row of a scan along a row, column of a scan along a column
 * @param from First column or row
 * @param to Last column or row
 * @param rows Whether the scan runs along a column, over rows
 * @return The scan
 */
static dg_scan_t dg_scan(
    const unsigned char *px,
    int32_t fixed,
    int32_t from,
    int32_t to,
    bool rows)
{
    dg_scan_t result = { .peak = from, .peak_value = -1 };
    int32_t i, c;
    for (i = from; i <= to; i ++) {
        const unsigned char *p = rows ? dg_px(px, fixed, i) : dg_px(px, i, fixed);
        float total = 0;
        for (c = 0; c < 3; c ++) {
            float l = dg_lin(p[c]);
            result.sum[c] += l;
            total += l;
        }

        if (total > result.peak_value) {
            result.peak_value = total;
            result.peak = i;
        }
    }

    return result;
}

/** Count the pixels of a region of two square captures that differ by more than a tolerance in
 * a channel.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 First column
 * @param y0 First row
 * @param x1 Last column
 * @param y1 Last row
 * @param tolerance Largest difference that counts as equal
 * @return The pixels that differ
 */
static int32_t dg_differ(
    const unsigned char *a,
    const unsigned char *b,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t tolerance)
{
    int32_t x, y, c, result = 0;
    for (y = y0; y <= y1; y ++) {
        for (x = x0; x <= x1; x ++) {
            const unsigned char *p = dg_px(a, x, y), *q = dg_px(b, x, y);
            for (c = 0; c < 3; c ++) {
                if (abs((int32_t)p[c] - (int32_t)q[c]) > tolerance) {
                    result ++;
                    break;
                }
            }
        }
    }

    return result;
}

/** Render the grid of a camera looking down from 10 units, with grid settings.
 *
 * @param grid The settings, NULL for a view without debug drawing
 * @param file Name of the PNG
 * @param passes Receives the debug passes of two frames after the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dg_render_down(
    const GridSettings *grid,
    const char *file,
    int32_t *passes)
{
    ecs_world_t *world = dg_world();
    dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), grid);
    unsigned char *px = dg_render(world, file);
    box_render_gpu_stats_t before = dg_frames(world, 0);
    box_render_gpu_stats_t after = dg_frames(world, 2);
    *passes = after.debug_passes - before.debug_passes;
    test_int(after.errors, before.errors);
    ecs_fini(world);

    return px;
}

/** Check the minor lines of a capture looking down at a number of pixels a unit: one line of
 * about the line width at every cell along a row between cells, the pixels between lines black.
 *
 * @param px The pixels
 * @param scale Pixels a unit
 * @param width Line width in pixels
 */
static void dg_expect_lines(
    const unsigned char *px,
    float scale,
    float width)
{
    int32_t row = (int32_t)dg_pos(2.5f, scale), k;
    for (k = 1; k <= 4; k ++) {
        int32_t from = (int32_t)dg_pos((float)k - 0.5f, scale);
        int32_t to = (int32_t)dg_pos((float)k + 0.5f, scale);
        if (to >= DG_SIZE) {
            break;
        }

        dg_scan_t s = dg_scan(px, row, from, to, false);
        float center = dg_pos((float)k, scale);
        test_assert(s.sum[0] > width * 0.6f);
        test_assert(s.sum[0] < width * 1.5f + 0.3f);
        test_assert(fabsf(s.sum[1] - s.sum[0]) < 0.15f * s.sum[0]);
        test_assert(fabsf((float)s.peak + 0.5f - center) <= 1.0f);
        test_assert(dg_px(px, (int32_t)dg_pos((float)k + 0.5f, scale), row)[0] < 20);
    }
}

/** A view with its debug settings off records no debug pass and draws what it drew before; a
 * grid of cell size 0, as from a C literal, records its pass and draws nothing. */
void ViewDebug_off_records_nothing(void) {
    // debug/pass
    GridSettings zero = { .enabled = true };
    int32_t passes[3];
    unsigned char *px[3];
    px[0] = dg_render_down(NULL, "debug_off.png", &passes[0]);
    px[1] = dg_render_down(&zero, "debug_zero.png", &passes[1]);

    ecs_world_t *world = dg_world();
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = dg_camera(world, 10, -(float)GLM_PI_2);
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    ecs_modified(world, view, View);
    px[2] = dg_render(world, "debug_default.png");
    box_render_gpu_stats_t before = dg_frames(world, 0);
    box_render_gpu_stats_t after = dg_frames(world, 2);
    passes[2] = after.debug_passes - before.debug_passes;
    test_int(after.debug_passes, 0);
    ecs_fini(world);

    test_int(passes[0], 0);
    test_int(passes[1], 2);
    test_int(passes[2], 0);
    test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
    test_assert(!memcmp(px[0], px[2], DG_SIZE * DG_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
    stbi_image_free(px[2]);
}

/** Looking down on the plane, a white line of about one pixel crosses every cell along both
 * axes, green major lines every 5 cells, the red X axis along z = 0 and the blue Z axis along
 * x = 0, in one debug pass per frame. */
void ViewDebug_grid_lines(void) {
    // debug/grid
    int32_t passes;
    unsigned char *px = dg_render_down(&dg_grid, "debug_grid_lines.png", &passes);
    test_int(passes, 2);
    dg_expect_lines(px, DG_SCALE, 1);

    int32_t k, column = (int32_t)dg_pos(2.5f, DG_SCALE);
    for (k = 1; k <= 4; k ++) {
        dg_scan_t s = dg_scan(px, column, (int32_t)dg_pos((float)k - 0.5f, DG_SCALE),
            (int32_t)dg_pos((float)k + 0.5f, DG_SCALE), true);
        test_assert(s.sum[0] > 0.6f);
        test_assert(s.sum[0] < 1.8f);
    }

    int32_t row = (int32_t)dg_pos(2.5f, DG_SCALE);
    float majors[2] = { 5.0f, -5.0f };
    for (k = 0; k < 2; k ++) {
        float center = dg_pos(majors[k], DG_SCALE);
        int32_t from = (int32_t)(center - 4), to = (int32_t)(center + 4);
        from = from < 0 ? 0 : from;
        to = to >= DG_SIZE ? DG_SIZE - 1 : to;
        dg_scan_t s = dg_scan(px, row, from, to, false);
        test_assert(s.sum[1] > 0.9f);
        test_assert(s.sum[1] < 2.6f);
        test_assert(s.sum[0] < 0.4f * s.sum[1]);
        test_assert(fabsf((float)s.peak + 0.5f - center) <= 1.0f);
    }

    dg_scan_t z_axis = dg_scan(px, row, 60, 67, false);
    test_assert(z_axis.sum[2] > 0.9f);
    test_assert(z_axis.sum[0] < 0.4f * z_axis.sum[2]);
    dg_scan_t x_axis = dg_scan(px, column, 60, 67, true);
    test_assert(x_axis.sum[0] > 0.9f);
    test_assert(x_axis.sum[2] < 0.4f * x_axis.sum[0]);
    test_assert(x_axis.sum[1] < 0.4f * x_axis.sum[0]);
    stbi_image_free(px);
}

/** A box standing on the plane hides the grid under it: the pixels of its top are those of the
 * view without a grid, the grid around it shows. */
void ViewDebug_grid_hidden_by_geometry(void) {
    // debug/depth
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), k ? &dg_grid : NULL);
        dg_box(world, (float[]){0, 1, 0}, (float[]){4, 2, 4}, (Rgb){200, 60, 60});
        px[k] = dg_render(world, k ? "debug_hidden_on.png" : "debug_hidden_off.png");
        ecs_fini(world);
    }

    test_int(dg_differ(px[0], px[1], 46, 46, 81, 81, 1), 0);
    test_assert(dg_differ(px[0], px[1], 0, 0, DG_SIZE - 1, 20, 1) > 100);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** The grid draws over a box under the plane: its lines cross the top of the box. */
void ViewDebug_grid_over_geometry_below(void) {
    // debug/depth
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), k ? &dg_grid : NULL);
        dg_box(world, (float[]){0, -2, 0}, (float[]){6, 2, 6}, (Rgb){60, 60, 200});
        px[k] = dg_render(world, k ? "debug_below_on.png" : "debug_below_off.png");
        ecs_fini(world);
    }

    const unsigned char *box = dg_px(px[0], 70, 70);
    test_assert(box[2] > 100);
    int32_t line = (int32_t)dg_pos(1, DG_SCALE), row = (int32_t)dg_pos(1.5f, DG_SCALE);
    dg_scan_t off = dg_scan(px[0], row, line - 2, line + 2, false);
    dg_scan_t on = dg_scan(px[1], row, line - 2, line + 2, false);
    test_assert(on.sum[0] > off.sum[0] + 0.5f);
    test_assert(dg_differ(px[0], px[1], 50, 50, 77, 77, 1) > 50);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** The grid is gone at its fade distance: with the fade distance at the height of the camera no
 * pixel changes, a bit further it shows at the center under the camera and not in the corners,
 * which are further away than the fade distance. */
void ViewDebug_grid_fades(void) {
    // debug/grid
    GridSettings near_ = dg_grid, far_ = dg_grid;
    int32_t passes;
    near_.fade_distance = 10;
    far_.fade_distance = 12;
    unsigned char *off = dg_render_down(NULL, "debug_fade_off.png", &passes);
    unsigned char *gone = dg_render_down(&near_, "debug_fade_gone.png", &passes);
    unsigned char *center = dg_render_down(&far_, "debug_fade_center.png", &passes);
    test_int(passes, 2);
    test_int(dg_differ(off, gone, 0, 0, DG_SIZE - 1, DG_SIZE - 1, 1), 0);
    test_int(dg_differ(off, center, 0, 0, 12, 12, 1), 0);
    test_int(dg_differ(off, center, DG_SIZE - 13, DG_SIZE - 13, DG_SIZE - 1, DG_SIZE - 1, 1),
        0);
    test_assert(dg_differ(off, center, 44, 44, 83, 83, 1) > 20);
    stbi_image_free(off);
    stbi_image_free(gone);
    stbi_image_free(center);
}

/** Looking along the plane from 2 units above it, the grid fades out toward the horizon without
 * a hard line: the mean of the rows changes smoothly from the horizon down, and the rows at the
 * horizon are black. */
void ViewDebug_grid_no_edge(void) {
    // debug/grid
    ecs_world_t *world = dg_world();
    dg_view(world, dg_camera(world, 2, 0), &dg_grid);
    unsigned char *px = dg_render(world, "debug_no_edge.png");
    float means[DG_SIZE];
    int32_t x, y;
    for (y = 0; y < DG_SIZE; y ++) {
        float sum = 0;
        for (x = 0; x < DG_SIZE; x ++) {
            sum += dg_lin(dg_px(px, x, y)[0]);
        }

        means[y] = sum / DG_SIZE;
    }

    for (y = 0; y < DG_SIZE / 2; y ++) {
        test_assert(means[y] < 0.01f);
    }

    for (y = DG_SIZE / 2; y < DG_SIZE / 2 + 12; y ++) {
        test_assert(fabsf(means[y + 1] - means[y]) < 0.03f);
    }

    test_assert(means[DG_SIZE / 2] < 0.02f);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A grid 2 units above the origin is 8 units under the camera: its lines are further apart in
 * the image than those of the plane at 0, at 13.86 pixels a unit. */
void ViewDebug_grid_height(void) {
    // debug/grid
    GridSettings raised = dg_grid;
    int32_t passes;
    raised.height = 2;
    unsigned char *px = dg_render_down(&raised, "debug_height.png", &passes);
    float scale = 64.0f / (8.0f * 0.57735027f);
    dg_expect_lines(px, scale, 1);
    stbi_image_free(px);

    px = dg_render_down(&dg_grid, "debug_height_zero.png", &passes);
    int32_t row = (int32_t)dg_pos(2.5f, DG_SCALE), line = (int32_t)dg_pos(1, scale);
    dg_scan_t s = dg_scan(px, row, line, line + 1, false);
    test_assert(s.sum[0] < 0.2f);
    stbi_image_free(px);
}

/** An orthographic camera looking down that shows 16 units draws the lines 8 pixels apart, one
 * pixel wide. */
void ViewDebug_grid_orthographic(void) {
    // debug/grid
    ecs_world_t *world = dg_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 16, .near_ = 0.1f,
        .far_ = 100});
    dg_view(world, camera, &dg_grid);
    unsigned char *px = dg_render(world, "debug_ortho.png");
    dg_expect_lines(px, 8.0f, 1);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Of two views of one target only the one with the grid draws it: under a later view without
 * a grid, the grid of the first stays where the later one draws nothing and is gone under its
 * box; a later view with the grid draws it over the first. */
void ViewDebug_grid_per_view(void) {
    // debug/pass
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t first = dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2),
            k ? NULL : &dg_grid);
        (void)first;
        ecs_entity_t scene = ecs_new(world);
        ecs_set(world, scene, Scene, {0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 10, 0});
        ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 2000});
        ecs_entity_t second = ecs_new(world);
        ecs_set(world, second, View, {.camera = camera, .lighting = {.enabled = true},
            .order = 1});
        if (k) {
            ecs_get_mut(world, second, View)->debug.grid = dg_grid;
            ecs_modified(world, second, View);
        }

        ecs_entity_t box = dg_box(world, (float[]){0, 1, 0}, (float[]){2, 2, 2},
            (Rgb){200, 60, 60});
        ecs_add_pair(world, box, EcsChildOf, scene);
        unsigned char *px = dg_render(world, k ? "debug_views_b.png" : "debug_views_a.png");
        box_render_gpu_stats_t before = dg_frames(world, 0);
        box_render_gpu_stats_t after = dg_frames(world, 2);
        test_int(after.debug_passes - before.debug_passes, 2);
        dg_expect_lines(px, DG_SCALE, 1);
        const unsigned char *top = dg_px(px, 64 + 11, 64 + 6);
        test_assert(top[0] > 100);
        test_assert(abs((int32_t)top[1] - (int32_t)top[2]) < 4);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** The grid keeps its color in a view with a Tonemap and a HeightFog, which change the rest of
 * the image. */
void ViewDebug_grid_not_tonemapped(void) {
    // debug/effects
    GridSettings wide = dg_grid;
    wide.line_width = 5;
    wide.color = (rgba){200, 40, 40, 255};
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255},
            .ground = {255, 255, 255}, .intensity = 1});
        ecs_entity_t view = dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), &wide);
        dg_box(world, (float[]){-2.5f, -1.5f, -2.5f}, (float[]){2, 2, 2},
            (Rgb){250, 250, 250});
        if (k) {
            ecs_entity_t tonemap = ecs_new(world);
            ecs_add(world, tonemap, Tonemap);
            ecs_entity_t fog = ecs_new(world);
            ecs_set(world, fog, HeightFog, {.density = 0.05f, .falloff = 0, .max_opacity = 1,
                .color = {40, 80, 160}});
            View *v = ecs_get_mut(world, view, View);
            *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
                .enabled = true, .effect = fog };
            *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
                .enabled = true, .effect = tonemap };
            ecs_modified(world, view, View);
        }

        px[k] = dg_render(world, k ? "debug_effects_on.png" : "debug_effects_off.png");
        box_render_gpu_stats_t before = dg_frames(world, 0);
        box_render_gpu_stats_t after = dg_frames(world, 1);
        test_int(after.debug_passes - before.debug_passes, 1);
        ecs_fini(world);
    }

    int32_t line = (int32_t)dg_pos(2, DG_SCALE), row = (int32_t)dg_pos(2.5f, DG_SCALE);
    const unsigned char *a = dg_px(px[0], line, row), *b = dg_px(px[1], line, row);
    for (k = 0; k < 3; k ++) {
        test_assert(abs((int32_t)a[k] - (int32_t)b[k]) <= 3);
    }

    test_assert(abs((int32_t)a[0] - 200) <= 6);
    const unsigned char *box_a = dg_px(px[0], 37, 37), *box_b = dg_px(px[1], 37, 37);
    test_assert(abs((int32_t)box_a[0] - (int32_t)box_b[0]) > 5 ||
        abs((int32_t)box_a[2] - (int32_t)box_b[2]) > 5);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Capture the scene of a view into a reflection probe over a dark floor, with or without a
 * wide bright grid in the view, and read a face of the probe and its SH.
 *
 * @param grid Whether the view draws the grid
 * @param texels Receives the 16 by 16 texels of the face that looks down, RGBA
 * @param sh Receives the SH coefficients of the probe
 */
static void dg_capture(
    bool grid,
    float *texels,
    float sh[BOX_RENDER_SH_COEFFICIENTS][4])
{
    ecs_world_t *world = dg_world();
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = false});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -1.1f, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 8});
    ecs_set(world, floor, Rgb, {40, 40, 40});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {3, 1, 0});
    ecs_set(world, wall, Box, {0.2f, 2, 2});
    ecs_set(world, wall, Rgb, {255, 0, 0});
    ecs_set(world, wall, Emissive, {.strength = 4, .color = {255, 0, 0}});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    GridSettings wide = dg_grid;
    wide.line_width = 6;
    wide.cell_size = 0.5f;
    dg_view(world, camera, grid ? &wide : NULL);
    dg_surface(world, 64, 48, NULL);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);

    int32_t frame;
    const ReflectionBakeStatus *status = NULL;
    box_render_gpu_stats_t before = dg_frames(world, 0), after = before;
    for (frame = 0; frame < 100; frame ++) {
        after = dg_frames(world, 1);
        status = ecs_get(world, scene, ReflectionBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_bool(status->converged, true);
    test_int(after.errors, before.errors);
    if (grid) {
        test_assert(after.debug_passes - before.debug_passes > 0);
        test_assert(after.debug_passes - before.debug_passes <= frame + 1);
    } else {
        test_int(after.debug_passes - before.debug_passes, 0);
    }

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
    test_assert(box_render_gpu_read_reflection(world, texture, (uint32_t)slot, 3, texels,
        16 * 16 * 4));
    test_assert(box_render_gpu_read_reflection_sh(world, texture, (uint32_t)slot, sh));
    ecs_fini(world);
}

/** A reflection probe captures its scene without the grid of the view. */
void ViewDebug_grid_not_in_captures(void) {
    // debug/captures
    static float plain[16 * 16 * 4], gridded[16 * 16 * 4];
    float sh_plain[BOX_RENDER_SH_COEFFICIENTS][4], sh_grid[BOX_RENDER_SH_COEFFICIENTS][4];
    int32_t i, c;
    dg_capture(false, plain, sh_plain);
    dg_capture(true, gridded, sh_grid);
    for (i = 0; i < 16 * 16 * 4; i ++) {
        test_assert(fabsf(plain[i] - gridded[i]) <= 1e-3f + 0.01f * fabsf(plain[i]));
    }

    for (i = 0; i < BOX_RENDER_SH_COEFFICIENTS; i ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(sh_plain[i][c] - sh_grid[i][c]) <=
                1e-4f + 0.01f * fabsf(sh_plain[i][c]));
        }
    }
}

/** A camera 10 units under the plane looking up sees the grid as from above. */
void ViewDebug_grid_camera_below(void) {
    // debug/grid
    ecs_world_t *world = dg_world();
    dg_view(world, dg_camera(world, -10, (float)GLM_PI_2), &dg_grid);
    unsigned char *px = dg_render(world, "debug_below_camera.png");
    dg_expect_lines(px, DG_SCALE, 1);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A resized surface draws the grid at its new size without validation errors. */
void ViewDebug_grid_resize(void) {
    // debug/pass
    ecs_world_t *world = dg_world();
    dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), &dg_grid);
    ecs_entity_t s = dg_surface(world, DG_SIZE, DG_SIZE, "debug_resize_a.png");
    box_render_gpu_stats_t before = dg_frames(world, 3);
    unsigned char *px = dg_load(world, "debug_resize_a.png", DG_SIZE, DG_SIZE);
    dg_expect_lines(px, DG_SCALE, 1);
    stbi_image_free(px);

    ecs_set(world, s, Surface, {.width = 192, .height = 96,
        .write_to_file = "debug_resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = dg_frames(world, 3);
    px = dg_load(world, "debug_resize_b.png", 192, 96);
    float scale = 48.0f / (10.0f * 0.57735027f);
    int32_t from = (int32_t)(96.0f + 0.5f * scale), to = (int32_t)(96.0f + 1.5f * scale);
    float red = 0;
    int32_t x, row = (int32_t)(48.0f + 2.5f * scale);
    for (x = from; x <= to; x ++) {
        red += dg_lin(px[(row * 192 + x) * 4]);
    }

    test_assert(red > 0.6f);
    test_assert(red < 1.8f);
    stbi_image_free(px);
    test_int(after.errors, before.errors);
    test_assert(after.debug_passes - before.debug_passes >= 3);

    ecs_fini(world);
}

/* Wireframe (ViewDebug.wireframe). Most cases look down -Z from z = 10 with a 60 degree field
 * of view into a black 128 pixel capture, +X right and +Y up, at shapes of black albedo that
 * show their emission in red or blue, so the green channel comes from the lines. */

/** White lines of one pixel at full alpha */
static const rgba dw_white = {255, 255, 255, 255};

/** Green lines of one pixel at full alpha */
static const rgba dw_green = {0, 255, 0, 255};

/** Tangent of half the field of view of the cameras */
#define DW_TAN (0.57735027f)

/** Pixels a unit at a distance in front of a camera of the cases.
 *
 * @param distance Distance along the view direction
 * @return Pixels a unit
 */
static float dw_scale(
    float distance)
{
    return 64.0f / (distance * DW_TAN);
}

/** Add a perspective camera on +Z looking down -Z at the origin.
 *
 * @param world The world
 * @param z Distance of the camera from the origin
 * @return The camera
 */
static ecs_entity_t dw_camera(
    ecs_world_t *world,
    float z)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, z});
    ecs_set(world, camera, Rotation, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 2000});

    return camera;
}

/** Set the wireframe of a view.
 *
 * @param world The world
 * @param view The view
 * @param mode The mode
 * @param color The color of the lines
 * @param width The width of the lines in pixels
 */
static void dw_set(
    ecs_world_t *world,
    ecs_entity_t view,
    WireframeMode mode,
    rgba color,
    float width)
{
    View *v = ecs_get_mut(world, view, View);
    v->debug.wireframe = mode;
    v->debug.wireframe_color = color;
    v->debug.wireframe_width = width;
    ecs_modified(world, view, View);
}

/** Add a lit view of a camera with a wireframe, clearing to black.
 *
 * @param world The world
 * @param camera The camera
 * @param mode The mode, WireframeOff for none
 * @param color The color of the lines
 * @param width The width of the lines in pixels
 * @return The view
 */
static ecs_entity_t dw_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    WireframeMode mode,
    rgba color,
    float width)
{
    ecs_entity_t view = dg_view(world, camera, NULL);
    dw_set(world, view, mode, color, width);

    return view;
}

/** Add an entity of black albedo that shows its emission, without a shape.
 *
 * @param world The world
 * @param x X of its position
 * @param y Y of its position
 * @param z Z of its position
 * @param color Emission, display encoded
 * @return The entity
 */
static ecs_entity_t dw_shape(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    Rgb color)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = color});

    return e;
}

/** Sum the linear green of the pixels of a row between two columns, clamped to the capture.
 *
 * @param px The pixels
 * @param row The row
 * @param from First column
 * @param to Last column
 * @return The sum
 */
static float dw_green_sum(
    const unsigned char *px,
    int32_t row,
    int32_t from,
    int32_t to)
{
    from = from < 0 ? 0 : from;
    to = to >= DG_SIZE ? DG_SIZE - 1 : to;

    return dg_scan(px, row, from, to, false).sum[1];
}

/** Largest green over red of the pixels of a square around a pixel.
 *
 * @param px The pixels
 * @param x Column of the center
 * @param y Row of the center
 * @param radius Pixels from the center to the edges of the square
 * @return The largest green minus red, display encoded
 */
static int32_t dw_green_peak(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t radius)
{
    int32_t i, j, result = -255;
    for (j = y - radius; j <= y + radius; j ++) {
        for (i = x - radius; i <= x + radius; i ++) {
            if (i < 0 || j < 0 || i >= DG_SIZE || j >= DG_SIZE) {
                continue;
            }

            const unsigned char *p = dg_px(px, i, j);
            int32_t d = (int32_t)p[1] - (int32_t)p[0];
            result = d > result ? d : result;
        }
    }

    return result;
}

/** Render frames, then read the draw calls and debug passes of two more frames.
 *
 * @param world The world
 * @param draws Receives the draw calls of the two frames
 * @param passes Receives the debug passes of the two frames
 */
static void dw_count(
    ecs_world_t *world,
    int32_t *draws,
    int32_t *passes)
{
    box_render_gpu_stats_t before = dg_frames(world, 0);
    box_render_gpu_stats_t after = dg_frames(world, 2);
    *draws = after.draws - before.draws;
    *passes = after.debug_passes - before.debug_passes;
    test_int(after.errors, before.errors);
}

/** A view with the wireframe off, whatever its color and width, renders the pixels and records
 * the draw calls of a view without it, and no debug pass. */
void ViewDebug_wire_off_unchanged(void) {
    // debug/wireframe
    unsigned char *px[2];
    int32_t draws[2], passes[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t view = dg_view(world, dw_camera(world, 10), NULL);
        if (k) {
            dw_set(world, view, WireframeOff, dw_green, 3);
        }

        ecs_entity_t quad = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
        ecs_set(world, quad, Quad, {6, 6});
        ecs_entity_t box = dw_shape(world, 3, 2, 1, (Rgb){0, 0, 255});
        ecs_set(world, box, Box, {1, 1, 1});
        px[k] = dg_render(world, k ? "wire_off_b.png" : "wire_off_a.png");
        dw_count(world, &draws[k], &passes[k]);
        ecs_fini(world);
    }

    test_int(passes[0], 0);
    test_int(passes[1], 0);
    test_int(draws[0], draws[1]);
    test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Over a red quad facing the camera the overlay draws a white line along each edge and along
 * the diagonal between its two triangles, with the red interior between the lines. */
void ViewDebug_wire_overlay_quad(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_white, 1);
    ecs_entity_t quad = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    ecs_set(world, quad, Quad, {6, 6});
    unsigned char *px = dg_render(world, "wire_quad.png");
    int32_t draws, passes;
    dw_count(world, &draws, &passes);
    test_int(passes, 2);

    float s = dw_scale(10);
    int32_t row = (int32_t)(64.0f - 1.5f * s);
    float left = 64.0f - 3.0f * s, right = 64.0f + 3.0f * s;
    dg_scan_t edge = dg_scan(px, row, (int32_t)left - 4, (int32_t)left + 4, false);
    test_assert(edge.sum[1] > 0.5f);
    test_assert(fabsf((float)edge.peak + 0.5f - left) <= 2.0f);
    test_assert(dw_green_sum(px, row, (int32_t)right - 4, (int32_t)right + 4) > 0.5f);

    float top = 64.0f - 3.0f * s;
    int32_t column = (int32_t)(64.0f + 0.3f * s), y;
    float top_sum = 0;
    for (y = (int32_t)top - 4; y <= (int32_t)top + 4; y ++) {
        top_sum += dg_lin(dg_px(px, column, y)[1]);
    }

    test_assert(top_sum > 0.5f);

    float diag_a = dw_green_sum(px, row, (int32_t)(64.0f - 1.5f * s) - 4,
        (int32_t)(64.0f - 1.5f * s) + 4);
    float diag_b = dw_green_sum(px, row, (int32_t)(64.0f + 1.5f * s) - 4,
        (int32_t)(64.0f + 1.5f * s) + 4);
    test_assert((diag_a > 0.5f) != (diag_b > 0.5f));
    test_assert(diag_a < 0.1f || diag_b < 0.1f);

    const unsigned char *inside = dg_px(px, 64, row);
    test_assert(inside[0] > 100);
    test_assert(inside[1] < 30);
    inside = dg_px(px, (int32_t)(64.0f - 2.4f * s), row);
    test_assert(inside[0] > 100);
    test_assert(inside[1] < 30);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Scene of a blue box in front of a red box that it hides from the camera.
 *
 * @param world The world
 */
static void dw_hidden_scene(
    ecs_world_t *world)
{
    ecs_entity_t front = dw_shape(world, 0, 0, 2, (Rgb){0, 0, 255});
    ecs_set(world, front, Box, {4, 4, 1});
    ecs_entity_t back = dw_shape(world, 0, 0, -2, (Rgb){255, 0, 0});
    ecs_set(world, back, Box, {2, 2, 2});
}

/** The overlay draws the edges of a box in front and none of a box it hides: around the edges
 * the hidden box would have, away from the lines of the front box, the pixels are those of the
 * view without a wireframe. */
void ViewDebug_wire_overlay_hidden(void) {
    // debug/wireframe
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dw_view(world, dw_camera(world, 10), k ? WireframeOverlay : WireframeOff, dw_white, 1);
        dw_hidden_scene(world);
        px[k] = dg_render(world, k ? "wire_hidden_b.png" : "wire_hidden_a.png");
        ecs_fini(world);
    }

    int32_t edge = (int32_t)(64.0f - dw_scale(11));
    test_int(dg_differ(px[0], px[1], 60, edge - 2, 68, edge + 2, 6), 0);

    float right = 64.0f + 2.0f * dw_scale(7.5f);
    int32_t row = 64 + 10;
    test_assert(dw_green_sum(px[1], row, (int32_t)right - 4, (int32_t)right + 4) > 0.5f);
    test_assert(dw_green_sum(px[0], row, (int32_t)right - 4, (int32_t)right + 4) < 0.05f);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** WireframeOnly shows the back edges of the front box and the edges of the box it hides,
 * draws no shaded pixel and leaves the clear color between the lines. */
void ViewDebug_wire_only_see_through(void) {
    // debug/wireframe
    unsigned char *px[2];
    int32_t k, draws[2], passes[2];
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dw_view(world, dw_camera(world, 10), k ? WireframeOnly : WireframeOff, dw_white, 1);
        dw_hidden_scene(world);
        px[k] = dg_render(world, k ? "wire_only_b.png" : "wire_only_a.png");
        dw_count(world, &draws[k], &passes[k]);
        ecs_fini(world);
    }

    test_int(passes[0], 0);
    test_int(passes[1], 2);

    int32_t row = 64 + 3, x, y;
    const unsigned char *clear = dg_px(px[1], 64 + 18, row);
    test_assert(clear[0] < 12);
    test_assert(clear[1] < 12);
    test_assert(clear[2] < 12);
    test_assert(dg_px(px[0], 64 + 18, row)[2] > 100);

    float back = 64.0f + 2.0f * dw_scale(8.5f), front = 64.0f + 2.0f * dw_scale(7.5f);
    test_assert(dw_green_sum(px[1], row, (int32_t)back - 2, (int32_t)back + 1) > 0.4f);
    test_assert(dw_green_sum(px[1], row, (int32_t)front - 1, (int32_t)front + 2) > 0.4f);

    float hidden = 64.0f + dw_scale(11);
    test_assert(dw_green_sum(px[1], row, (int32_t)hidden - 4, (int32_t)hidden + 3) > 0.4f);

    int32_t shaded = 0;
    for (y = 0; y < DG_SIZE; y ++) {
        for (x = 0; x < DG_SIZE; x ++) {
            const unsigned char *p = dg_px(px[1], x, y);
            if ((int32_t)p[2] > (int32_t)p[1] + 30 || (int32_t)p[0] > (int32_t)p[1] + 30) {
                shaded ++;
            }
        }
    }

    test_int(shaded, 0);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A view with WireframeOnly culls without occlusion: a box behind a wall that occlusion would
 * cull is drawn, and its lines show through the wall. */
void ViewDebug_wire_only_no_occlusion(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    ecs_entity_t view = dw_view(world, dw_camera(world, 10), WireframeOnly, dw_white, 1);
    View *v = ecs_get_mut(world, view, View);
    v->culling = (CullSettings){.enabled = true, .occlusion = true, .min_dimension_px = 1};
    ecs_modified(world, view, View);
    ecs_entity_t wall = dw_shape(world, 0, 0, 0, (Rgb){0, 0, 255});
    ecs_set(world, wall, Box, {20, 20, 0.5f});
    ecs_entity_t hidden = dw_shape(world, 0, 0, -5, (Rgb){255, 0, 0});
    ecs_set(world, hidden, Box, {1, 1, 1});
    unsigned char *px = dg_render(world, "wire_no_occlusion.png");
    dg_frames(world, 3);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 2);
    test_int(stats.visible, 2);

    float edge = 64.0f + 0.5f * dw_scale(15.0f);
    test_assert(dw_green_sum(px, 64, (int32_t)edge - 2, (int32_t)edge + 2) > 0.4f);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Lines of 3 pixels are three times as wide as lines of 1 pixel, close up and far away. */
void ViewDebug_wire_width(void) {
    // debug/wireframe
    float distances[2] = { 10, 25 };
    int32_t d, w;
    for (d = 0; d < 2; d ++) {
        float sums[2];
        for (w = 0; w < 2; w ++) {
            ecs_world_t *world = dg_world();
            dw_view(world, dw_camera(world, distances[d]), WireframeOverlay, dw_white,
                w ? 3.0f : 1.0f);
            ecs_entity_t quad = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
            ecs_set(world, quad, Quad, {6, 6});
            unsigned char *px = dg_render(world, w ? "wire_width_b.png" : "wire_width_a.png");
            float s = dw_scale(distances[d]);
            float left = 64.0f - 3.0f * s;
            int32_t row = (int32_t)(64.0f - 0.3f * s);
            sums[w] = dw_green_sum(px, row, (int32_t)left - 4, (int32_t)left + 6);
            stbi_image_free(px);
            ecs_fini(world);
        }

        test_assert(sums[0] > 0.6f);
        test_assert(sums[0] < 1.6f);
        test_assert(sums[1] / sums[0] > 2.4f);
        test_assert(sums[1] / sums[0] < 3.6f);
    }
}

/** Create a Path2D through points.
 *
 * @param world The world
 * @param points The points
 * @param count Number of points
 * @param closed Whether the path loops back to its start
 * @return The entity with the path
 */
static ecs_entity_t dw_path2(
    ecs_world_t *world,
    const vec2 *points,
    int32_t count,
    bool closed)
{
    Path2D path = {.closed = closed};
    int32_t i;
    glm_vec2_copy(ECS_CONST_CAST(float*, points[0]), path.start);
    for (i = 1; i < count; i ++) {
        PathSegment2D *segment = ecs_vec_append_t(NULL, &path.segments, PathSegment2D);
        *segment = (PathSegment2D){.kind = PathLine};
        glm_vec2_copy(ECS_CONST_CAST(float*, points[i]), segment->end);
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path2D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);

    return e;
}

/** Add a parametric surface: a capped Lathe of radius 2, or a Sweep of a circle of radius 0.5
 * along X from -2 to 2.
 *
 * @param world The world
 * @param sweep Whether to add the sweep
 * @return The shape
 */
static ecs_entity_t dw_parametric(
    ecs_world_t *world,
    bool sweep)
{
    ecs_entity_t e = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    if (!sweep) {
        vec2 outer[2] = {{2, -1}, {2, 1}};
        Lathe lathe = {.profile = dw_path2(world, outer, 2, false), .cap_start = true,
            .cap_end = true, .segments = 24, .rings = 6};
        ecs_set_ptr(world, e, Lathe, &lathe);
        goto done;
    }

    vec2 circle[16];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        float t = (float)i * 2.0f * (float)GLM_PI / 16.0f;
        circle[i][0] = cosf(t);
        circle[i][1] = sinf(t);
    }

    Profile2D profile = {.outer = dw_path2(world, circle, 16, true)};
    ecs_entity_t profile_e = ecs_new(world);
    ecs_set_ptr(world, profile_e, Profile2D, &profile);
    Path3D path = {.start = {-2, 0, 0}};
    PathSegment3D segment = {.kind = PathCubicBezier, .control1 = {-1, 0, 0},
        .control2 = {1, 0, 0}, .end = {2, 0, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = segment;
    ecs_entity_t path_e = ecs_new(world);
    ecs_set_ptr(world, path_e, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_set(world, e, Sweep, {.path = path_e, .profile = profile_e, .scale = {0.5f, 0.5f},
        .taper = 1, .cap_start = true, .cap_end = true, .segments = 16, .rings = 8});
done:
    return e;
}

/** The lines of a Lathe and of a Sweep lie where their shaded surface is: every line pixel of
 * WireframeOnly is within 2 pixels of a shaded pixel of the view without a wireframe. */
void ViewDebug_wire_parametric_surface(void) {
    // debug/wireframe
    int32_t k, w, x, y, i, j;
    for (k = 0; k < 2; k ++) {
        unsigned char *px[2];
        for (w = 0; w < 2; w ++) {
            ecs_world_t *world = dg_world();
            ecs_entity_t camera = ecs_new(world);
            ecs_set(world, camera, Position, {0, 8, 0});
            ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
            ecs_set(world, camera, Camera, {.near_ = 0.1f, .far_ = 100, .ortho = true,
                .ortho_height = 6});
            dw_view(world, camera, w ? WireframeOnly : WireframeOff, dw_white, 1);
            dw_parametric(world, k == 1);
            px[w] = dg_render(world, w ? "wire_surface_b.png" : "wire_surface_a.png");
            ecs_fini(world);
        }

        int32_t shaded = 0, lines = 0, stray = 0;
        for (y = 0; y < DG_SIZE; y ++) {
            for (x = 0; x < DG_SIZE; x ++) {
                shaded += dg_px(px[0], x, y)[0] > 60;
                if (dg_px(px[1], x, y)[1] <= 60) {
                    continue;
                }

                bool near = false;
                lines ++;
                for (j = y - 2; j <= y + 2 && !near; j ++) {
                    for (i = x - 2; i <= x + 2 && !near; i ++) {
                        near = i >= 0 && j >= 0 && i < DG_SIZE && j < DG_SIZE &&
                            dg_px(px[0], i, j)[0] > 60;
                    }
                }

                stray += !near;
            }
        }

        test_assert(shaded > 200);
        test_assert(lines > 50);
        test_int(stray, 0);
        stbi_image_free(px[0]);
        stbi_image_free(px[1]);
    }
}

/** Every instance of a prefab in view is wired, through the diagonal of its front face, and the
 * instances its frustum culls draw nothing. */
void ViewDebug_wire_instances(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    ecs_entity_t view = dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_green, 1);
    View *v = ecs_get_mut(world, view, View);
    v->culling = (CullSettings){.enabled = true, .min_dimension_px = 1};
    ecs_modified(world, view, View);
    ecs_entity_t prefab = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    int32_t i;
    for (i = 0; i < 21; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, e, Position, {-15.0f + 1.5f * (float)i, 0, 0});
    }

    unsigned char *px = dg_render(world, "wire_instances.png");
    dg_frames(world, 2);
    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 21);
    test_int(stats.visible, 9);

    float s = dw_scale(9.5f);
    for (i = -3; i <= 3; i ++) {
        int32_t column = (int32_t)(64.0f + 1.5f * (float)i * s);
        test_assert(dw_green_peak(px, column, 64, 2) > 80);
    }

    test_assert(dw_green_peak(px, (int32_t)(64.0f + 0.75f * s), 64, 1) < 20);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A blended and a transmissive box are wired: green lines cross the centers of their front
 * faces, which the view without a wireframe draws without green. */
void ViewDebug_wire_transmissive_and_blended(void) {
    // debug/wireframe
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dw_view(world, dw_camera(world, 10), k ? WireframeOverlay : WireframeOff, dw_green, 1);
        ecs_entity_t back = dw_shape(world, 0, 0, -4, (Rgb){120, 40, 40});
        ecs_set(world, back, Box, {20, 20, 0.5f});
        ecs_entity_t blended = dw_shape(world, -2.5f, 0, 0, (Rgb){255, 255, 255});
        ecs_set(world, blended, Box, {2, 2, 2});
        ecs_set(world, blended, Opacity, {0.5f});
        ecs_entity_t glass = ecs_new(world);
        ecs_set(world, glass, Position, {2.5f, 0, 0});
        ecs_set(world, glass, Box, {2, 2, 2});
        ecs_set(world, glass, Rgb, {255, 255, 255});
        ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 0});
        ecs_set(world, glass, Transmission, {.transmission_factor = 1, .ior = 1.5f,
            .thickness_factor = 0.5f, .attenuation_distance = 1e10f,
            .attenuation_color = {255, 255, 255}});
        px[k] = dg_render(world, k ? "wire_blended_b.png" : "wire_blended_a.png");
        ecs_fini(world);
    }

    float s = dw_scale(9);
    int32_t left = (int32_t)(64.0f - 2.5f * s), right = (int32_t)(64.0f + 2.5f * s);
    test_assert(dw_green_peak(px[0], left, 64, 2) < 30);
    test_assert(dw_green_peak(px[0], right, 64, 2) < 30);
    test_assert(dw_green_peak(px[1], left, 64, 2) > 60);
    test_assert(dw_green_peak(px[1], right, 64, 2) > 60);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A 2D shape and a text keep their look under the overlay, which wires neither and records
 * no draw for them. */
void ViewDebug_wire_2d_not_wired(void) {
    // debug/wireframe
    unsigned char *px[2];
    int32_t k, draws[2], passes[2];
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 100});
        ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
            .far_ = 1000});
        dw_view(world, camera, k ? WireframeOverlay : WireframeOff, dw_green, 2);
        ecs_entity_t rect = ecs_new(world);
        ecs_set(world, rect, Position, {-20, 20, 0});
        ecs_set(world, rect, Rect, {40, 20});
        ecs_set(world, rect, Rgb, {255, 0, 0});
        ecs_entity_t font = ecs_entity(world, { .name = "wire_font" });
        ecs_set(world, font, Font, { .path = "test/assets/fonts/B612-Regular.ttf" });
        ecs_entity_t text = ecs_new(world);
        ecs_set(world, text, Position, {0, -20, 0});
        ecs_set(world, text, Text, {.value = "Box", .size = 24, .font = font});
        ecs_set(world, text, Rgb, {255, 255, 255});
        px[k] = dg_render(world, k ? "wire_2d_b.png" : "wire_2d_a.png");
        dw_count(world, &draws[k], &passes[k]);
        ecs_fini(world);
    }

    test_int(passes[0], 0);
    test_int(passes[1], 2);
    test_int(draws[0], draws[1]);
    test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** Of two views of one target only the one with the wireframe draws it: a quad of the first
 * view on the left and of the second, in another scene and without clear, on the right. */
void ViewDebug_wire_per_view(void) {
    // debug/wireframe
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        dw_view(world, dw_camera(world, 10), k ? WireframeOff : WireframeOverlay, dw_green, 1);
        ecs_entity_t left = dw_shape(world, -3, 0, 0, (Rgb){255, 0, 0});
        ecs_set(world, left, Quad, {3, 3});

        ecs_entity_t scene = ecs_new(world);
        ecs_set(world, scene, Scene, {0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 0, 10});
        ecs_set(world, camera, Rotation, {0, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 2000});
        ecs_entity_t second = ecs_new(world);
        ecs_set(world, second, View, {.camera = camera, .lighting = {.enabled = true},
            .order = 1});
        dw_set(world, second, k ? WireframeOverlay : WireframeOff, dw_green, 1);
        ecs_entity_t right = dw_shape(world, 3, 0, 0, (Rgb){0, 0, 255});
        ecs_add_pair(world, right, EcsChildOf, scene);
        ecs_set(world, right, Quad, {3, 3});
        unsigned char *px = dg_render(world, k ? "wire_views_b.png" : "wire_views_a.png");

        float s = dw_scale(10);
        int32_t row = (int32_t)(64.0f - 0.3f * s);
        float a = 64.0f - 4.5f * s, b = 64.0f + 1.5f * s;
        float first = dw_green_sum(px, row, (int32_t)a - 3, (int32_t)a + 4);
        float later = dw_green_sum(px, row, (int32_t)b - 3, (int32_t)b + 4);
        test_assert(k ? first < 0.05f : first > 0.4f);
        test_assert(k ? later > 0.4f : later < 0.05f);
        test_assert(dg_px(px, (int32_t)(64.0f - 3.0f * s), row)[0] > 100);
        test_assert(dg_px(px, (int32_t)(64.0f + 3.0f * s + 4.0f), row)[2] > 100);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** A box whose mesh is replaced by a larger one is wired at its new edges, not at the old
 * ones, without validation errors. */
void ViewDebug_wire_mesh_replaced(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_green, 1);
    ecs_entity_t box = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    unsigned char *px = dg_render(world, "wire_replaced.png");
    int32_t row = 64 + 5;
    float small = 64.0f + dw_scale(9), large = 64.0f + 2.0f * dw_scale(8);
    test_assert(dw_green_sum(px, row, (int32_t)small - 3, (int32_t)small + 3) > 0.4f);
    stbi_image_free(px);

    box_render_gpu_stats_t before = dg_frames(world, 0);
    ecs_set(world, box, Box, {4, 4, 4});
    box_render_gpu_stats_t after = dg_frames(world, 3);
    test_int(after.errors, before.errors);
    test_int(after.shader_errors, before.shader_errors);
    px = dg_load(world, "wire_replaced.png", DG_SIZE, DG_SIZE);
    test_assert(dw_green_sum(px, row, (int32_t)large - 3, (int32_t)large + 3) > 0.4f);
    test_assert(dw_green_peak(px, (int32_t)small, row, 2) < 30);
    stbi_image_free(px);

    ecs_set(world, box, Box, {2, 2, 2});
    after = dg_frames(world, 3);
    test_int(after.errors, before.errors);
    px = dg_load(world, "wire_replaced.png", DG_SIZE, DG_SIZE);
    test_assert(dw_green_sum(px, row, (int32_t)small - 3, (int32_t)small + 3) > 0.4f);
    stbi_image_free(px);
    ecs_fini(world);
}
