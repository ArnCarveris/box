/**
 * @file ViewDebug.c
 * @brief Tests for the debug pass of a view (View.debug) and its infinite grid: no pass without
 * debug items, lines at every cell with their width in pixels, major lines and axes, the depth of
 * the view, the fade with the distance and toward the horizon, the height of the plane,
 * orthographic cameras, several views of a target, effects, probe captures, screenshots of the
 * camera of a view (with the grid) and of a camera without a view (without), a camera under the
 * plane and resizes; then the light and reflection probe views: spheres at the probes of the volume
 * and at the capture points of the reflection probes, their colors, invalid probes, the boxes of
 * the reflection probes, the probe size and views that sample no probes; and the wireframe: no
 * change while off, the edges and diagonals of the triangles, hidden lines of the overlay,
 * WireframeOnly without shaded meshes and occlusion, the width in pixels, parametric surfaces,
 * instances, blended and transmissive meshes, 2D shapes that are not wired, several views,
 * replaced meshes, the grid with the wireframe, and lines on the outline of a quad and a sphere
 * and on the edges of a box with the width of the view; checked on PNG captures.
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

/** Capture the scene of a view into a reflection probe over a dark floor, with or without debug
 * drawing in the view, and read a face of the probe and its SH.
 *
 * @param debug Debug settings of the view, NULL for none
 * @param bake Whether the scene also bakes a probe volume, which the view samples
 * @param texels Receives the 16 by 16 texels of the face that looks down, RGBA
 * @param sh Receives the SH coefficients of the probe
 */
static void dg_capture(
    const ViewDebug *debug,
    bool bake,
    float *texels,
    float sh[BOX_RENDER_SH_COEFFICIENTS][4])
{
    bool grid = debug != NULL;
    ecs_world_t *world = dg_world();
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = false});
    if (bake) {
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .bounces = 1, .capture_size = 16, .budget_ms = 4, .compute = true});
    }

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
    ecs_entity_t view = dg_view(world, camera, NULL);
    if (debug) {
        View *v = ecs_get_mut(world, view, View);
        v->debug = *debug;
        v->lighting.baked = bake;
        v->lighting.reflections = true;
        ecs_modified(world, view, View);
    } else if (bake) {
        View *v = ecs_get_mut(world, view, View);
        v->lighting.baked = true;
        v->lighting.reflections = true;
        ecs_modified(world, view, View);
    }

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
        if (status->converged &&
            (!bake || ecs_get(world, scene, LightBakeStatus)->converged))
        {
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
    ViewDebug debug = { .grid = dg_grid };
    debug.grid.line_width = 6;
    debug.grid.cell_size = 0.5f;
    dg_capture(NULL, false, plain, sh_plain);
    dg_capture(&debug, false, gridded, sh_grid);
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

/** Capture a camera looking down from 10 units through the capture API into a black square
 * PNG. A view with the grid renders a camera at the same place; the capture is of that camera,
 * or of a second camera that no view renders.
 *
 * @param of_view Whether the captured camera is the one of the view
 * @param file Name of the PNG in the temporary directory
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dg_capture_api(
    bool of_view,
    const char *file)
{
    ecs_world_t *world = dg_world();
    ecs_entity_t camera = dg_camera(world, 10, -(float)GLM_PI_2);
    ecs_entity_t lone = dg_camera(world, 10, -(float)GLM_PI_2);
    dg_view(world, camera, &dg_grid);
    dg_surface(world, 64, 48, NULL);
    box_render_gpu_stats_t before = dg_frames(world, 1), after;

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera = of_view ? camera : lone,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .width = DG_SIZE, .height = DG_SIZE, .path = path}), 0);
    ecs_os_free(path);
    after = dg_frames(world, 3);
    test_int(box_render_captures_pending(world), 0);
    test_int(after.errors, before.errors);
    test_int(after.shader_errors, before.shader_errors);

    unsigned char *px = dg_load(world, file, DG_SIZE, DG_SIZE);
    ecs_fini(world);

    return px;
}

/** A capture of the camera of a view with the grid shows the grid as the view draws it: white
 * minor lines at every cell and green major lines every 5 cells. */
void ViewDebug_grid_in_view_capture(void) {
    // debug/captures
    unsigned char *px = dg_capture_api(true, "debug_view_capture.png");
    dg_expect_lines(px, DG_SCALE, 1);

    int32_t k, row = (int32_t)dg_pos(2.5f, DG_SCALE);
    float majors[2] = { 5.0f, -5.0f };
    for (k = 0; k < 2; k ++) {
        float center = dg_pos(majors[k], DG_SCALE);
        int32_t from = (int32_t)(center - 4), to = (int32_t)(center + 4);
        from = from < 0 ? 0 : from;
        to = to >= DG_SIZE ? DG_SIZE - 1 : to;
        dg_scan_t s = dg_scan(px, row, from, to, false);
        test_assert(s.sum[1] > 0.9f);
        test_assert(s.sum[0] < 0.4f * s.sum[1]);
    }

    stbi_image_free(px);
}

/** A capture of a camera that no view renders draws no grid, though a view of the same scene
 * at the same place has one. */
void ViewDebug_grid_not_in_view_capture_without_view(void) {
    // debug/captures
    unsigned char *px = dg_capture_api(false, "debug_lone_capture.png");
    int32_t row = (int32_t)dg_pos(2.5f, DG_SCALE);
    int32_t column = (int32_t)dg_pos(2.5f, DG_SCALE);
    dg_scan_t across = dg_scan(px, row, 0, DG_SIZE - 1, false);
    dg_scan_t down = dg_scan(px, column, 0, DG_SIZE - 1, true);
    int32_t c;
    for (c = 0; c < 3; c ++) {
        test_assert(across.sum[c] < 0.05f);
        test_assert(down.sum[c] < 0.05f);
    }

    stbi_image_free(px);
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

/* Probe views (ViewDebug.light_probes and reflection_probes). The scenes are Scene roots with
 * their own bakes, AmbientLight and a directional light of intensity 0; the cases wait for the
 * bakes to converge before they read a capture. */

/** Frames a case waits for its bakes to converge at most */
#define DP_FRAMES (300)

/** Linear gray of a reflection probe that is not captured yet, display encoded */
#define DP_GRAY (137)

/** Create a scene root with bakes, an ambient light and a directional light of intensity 0.
 *
 * @param world The world
 * @param light The LightBake of the scene, NULL for none
 * @param reflection The ReflectionBake of the scene, NULL for none
 * @param ambient Intensity of the white hemisphere ambient of the scene
 * @return The scene
 */
static ecs_entity_t dp_scene(
    ecs_world_t *world,
    const LightBake *light,
    const ReflectionBake *reflection,
    float ambient)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    if (light) {
        ecs_set_ptr(world, scene, LightBake, light);
    }

    if (reflection) {
        ecs_set_ptr(world, scene, ReflectionBake, reflection);
    }

    ecs_set(world, scene, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = ambient});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    return scene;
}

/** A LightBake of one bounce and small captures.
 *
 * @param spacing World units between probes
 * @param padding World units around the static bounds
 * @return The settings
 */
static LightBake dp_light_bake(
    float spacing,
    float padding)
{
    return (LightBake){.probe_spacing = spacing, .bounds_padding = padding, .bounces = 1,
        .capture_size = 16, .budget_ms = 8, .compute = true};
}

/** A ReflectionBake of small cubemaps without a scene probe.
 *
 * @param budget_ms Capture time per frame, 0 captures nothing
 * @return The settings
 */
static ReflectionBake dp_reflection_bake(
    float budget_ms)
{
    return (ReflectionBake){.resolution = 32, .budget_ms = budget_ms, .scene_probe = false};
}

/** Add a static box of black albedo to a scene, with an emission.
 *
 * @param world The world
 * @param scene The scene
 * @param pos Center
 * @param size Size along each axis
 * @param color Emission, display encoded
 * @param strength Strength of the emission, 0 for none
 * @return The box
 */
static ecs_entity_t dp_box(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float pos[3],
    const float size[3],
    Rgb color,
    float strength)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Position, {pos[0], pos[1], pos[2]});
    ecs_set(world, e, Box, {size[0], size[1], size[2]});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    if (strength > 0) {
        ecs_set(world, e, Emissive, {.strength = strength, .color = color});
    }

    return e;
}

/** Add a camera to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param pos Position
 * @param rot Rotation
 * @param ortho Height an orthographic camera shows, 0 for a 60 degree perspective camera
 * @return The camera
 */
static ecs_entity_t dp_camera(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float pos[3],
    const float rot[3],
    float ortho)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {pos[0], pos[1], pos[2]});
    ecs_set(world, camera, Rotation, {rot[0], rot[1], rot[2]});
    if (ortho > 0) {
        ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = ortho, .near_ = 0.1f,
            .far_ = 200});
    } else {
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    }

    return camera;
}

/** Add a view that samples the baked lighting and the reflection probes of its scene, clears
 * to black and has debug settings.
 *
 * @param world The world
 * @param camera The camera
 * @param debug The debug settings, NULL for none
 * @return The view
 */
static ecs_entity_t dp_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    const ViewDebug *debug)
{
    ecs_entity_t view = dg_view(world, camera, NULL);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.baked = true;
    v->lighting.reflections = true;
    if (debug) {
        v->debug = *debug;
    }

    ecs_modified(world, view, View);

    return view;
}

/** Run frames until the bakes of a scene converge.
 *
 * @param world The world
 * @param scene The scene
 */
static void dp_converge(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    int32_t frame;
    for (frame = 0; frame < DP_FRAMES; frame ++) {
        dg_frames(world, 1);
        const LightBakeStatus *light = ecs_get(world, scene, LightBakeStatus);
        const ReflectionBakeStatus *reflection = ecs_get(world, scene, ReflectionBakeStatus);
        if ((!light || light->converged) && (!reflection || reflection->converged)) {
            break;
        }
    }

    test_assert(frame < DP_FRAMES);
}

/** Render a scene into a square capture once its bakes converged and load it, without new
 * device errors.
 *
 * @param world The world
 * @param scene The scene, 0 to not wait for bakes
 * @param file Name of the PNG
 * @param passes Receives the debug passes of two frames after the capture, NULL for none
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dp_render(
    ecs_world_t *world,
    ecs_entity_t scene,
    const char *file,
    int32_t *passes)
{
    box_render_gpu_stats_t before = dg_frames(world, 0);
    dg_surface(world, DG_SIZE, DG_SIZE, file);
    if (scene) {
        dp_converge(world, scene);
    }

    box_render_gpu_stats_t after = dg_frames(world, 3);
    test_int(after.errors, before.errors);
    test_int(after.shader_errors, before.shader_errors);
    unsigned char *px = dg_load(world, file, DG_SIZE, DG_SIZE);
    if (passes) {
        before = dg_frames(world, 0);
        after = dg_frames(world, 2);
        *passes = after.debug_passes - before.debug_passes;
    }

    return px;
}

/** Mean linear color of a square of pixels of a capture.
 *
 * @param px The pixels
 * @param x Center column
 * @param y Center row
 * @param r Half edge of the square
 * @param out Receives the linear red, green and blue
 */
static void dp_mean(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t r,
    float out[3])
{
    int32_t i, j, c, n = 0;
    out[0] = out[1] = out[2] = 0;
    for (j = y - r; j <= y + r; j ++) {
        for (i = x - r; i <= x + r; i ++) {
            if (i < 0 || j < 0 || i >= DG_SIZE || j >= DG_SIZE) {
                continue;
            }

            for (c = 0; c < 3; c ++) {
                out[c] += dg_lin(dg_px(px, i, j)[c]);
            }

            n ++;
        }
    }

    for (c = 0; c < 3; c ++) {
        out[c] /= (float)(n ? n : 1);
    }
}

/** Check whether a pixel of a capture is the black clear color.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Whether every channel is below 12
 */
static bool dp_black(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    const unsigned char *p = dg_px(px, x, y);

    return p[0] < 12 && p[1] < 12 && p[2] < 12;
}

/** Check whether a pixel of a capture is the gray of a reflection probe without a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Whether every channel is within 8 of DP_GRAY
 */
static bool dp_gray(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    const unsigned char *p = dg_px(px, x, y);

    return abs((int32_t)p[0] - DP_GRAY) <= 8 && abs((int32_t)p[1] - DP_GRAY) <= 8 &&
        abs((int32_t)p[2] - DP_GRAY) <= 8;
}

/** Find a pixel of a color within a distance of a point of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @param r Distance in pixels on each axis
 * @param color Expected color, display encoded
 * @param tolerance Largest difference in a channel
 * @return Whether a pixel of the square matches
 */
static bool dp_find(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t r,
    const int32_t color[3],
    int32_t tolerance)
{
    int32_t i, j, c;
    bool result = false;
    for (j = y - r; j <= y + r; j ++) {
        for (i = x - r; i <= x + r; i ++) {
            if (i < 0 || j < 0 || i >= DG_SIZE || j >= DG_SIZE) {
                continue;
            }

            const unsigned char *p = dg_px(px, i, j);
            for (c = 0; c < 3; c ++) {
                if (abs((int32_t)p[c] - color[c]) > tolerance) {
                    break;
                }
            }

            result = result || c == 3;
        }
    }

    return result;
}

/** The light probe scene of the grid cases: a black floor 3 units under the origin, baked with
 * a probe every unit and a unit of padding under a white ambient.
 *
 * An orthographic camera 10 units above the origin looks down and shows 8 units: 16 pixels a
 * unit, +X right and +Z down, the origin at the center.
 *
 * @param world The world
 * @param debug Debug settings of the view
 * @return The scene
 */
static ecs_entity_t dp_grid_scene(
    ecs_world_t *world,
    const ViewDebug *debug)
{
    LightBake bake = dp_light_bake(1, 1);
    ecs_entity_t scene = dp_scene(world, &bake, NULL, 1);
    dp_box(world, scene, (float[]){0, -3, 0}, (float[]){2, 0.2f, 2}, (Rgb){0, 0, 0}, 0);
    dp_view(world, dp_camera(world, scene, (float[]){0, 10, 0},
        (float[]){-(float)GLM_PI_2, 0, 0}, 8), debug);

    return scene;
}

/** Pixel of a point of the plane in the captures of dp_grid_scene.
 *
 * @param world Coordinate along X (columns) or Z (rows)
 * @return The column or row
 */
static int32_t dp_grid_px(
    float world)
{
    return (int32_t)floorf(64.0f + world * 16.0f);
}

/** Positions of the corner probes of the top layer of the volume of a scene and the bottom
 * layer, from box_render_bake_probe.
 *
 * @param world The world
 * @param scene The scene
 * @param low Receives the probe of the smallest x, y and z
 * @param high Receives the probe of the largest x, y and z
 */
static void dp_grid_corners(
    ecs_world_t *world,
    ecs_entity_t scene,
    box_render_probe_t *low,
    box_render_probe_t *high)
{
    test_assert(box_render_bake_probe(world, scene, (vec3){-100, -100, -100}, low));
    test_assert(box_render_bake_probe(world, scene, (vec3){100, 100, 100}, high));
}

/** A view with both probe views off draws the same pixels as without debug settings, with a
 * probe size set, and records no debug pass. */
void ViewDebug_probes_off_unchanged(void) {
    // debug/probes
    unsigned char *px[2];
    int32_t passes[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ViewDebug off = {.probe_size = 2};
        LightBake light = dp_light_bake(1, 0.5f);
        ReflectionBake reflection = dp_reflection_bake(1);
        ecs_entity_t scene = dp_scene(world, &light, &reflection, 1);
        dp_box(world, scene, (float[]){0, -1, 0}, (float[]){4, 0.2f, 4},
            (Rgb){200, 100, 50}, 1);
        ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, probe, Position, {0, 0, 0});
        ecs_set(world, probe, ReflectionProbe, {.extent = {2, 2, 2}, .blend = 0});
        dp_view(world, dp_camera(world, scene, (float[]){0, 1, 4}, (float[]){-0.2f, 0, 0}, 0),
            k ? &off : NULL);
        px[k] = dp_render(world, scene, k ? "probes_off_b.png" : "probes_off_a.png",
            &passes[k]);
        ecs_fini(world);
    }

    test_int(passes[0], 0);
    test_int(passes[1], 0);
    test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** The light probes of a known volume draw as spheres at their screen positions: one at every
 * probe of the top layer, as many as the volume has along X and Z one spacing apart, nothing
 * between them or a spacing past the last one.
 *
 * The default radius is a quarter of the spacing, 4 pixels: 2 from a center is in, 6 is out. */
void ViewDebug_light_probes_at_grid(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    ViewDebug debug = {.light_probes = true};
    ecs_entity_t scene = dp_grid_scene(world, &debug);
    int32_t passes;
    unsigned char *px = dp_render(world, scene, "light_probes_grid.png", &passes);
    test_int(passes, 2);

    box_render_probe_t low, high;
    dp_grid_corners(world, scene, &low, &high);
    int32_t nx = (int32_t)roundf(high.position[0] - low.position[0]) + 1;
    int32_t ny = (int32_t)roundf(high.position[1] - low.position[1]) + 1;
    int32_t nz = (int32_t)roundf(high.position[2] - low.position[2]) + 1;
    test_assert(nx >= 3);
    test_assert(nz >= 3);
    test_int(nx * ny * nz, ecs_get(world, scene, LightBakeStatus)->probes);

    int32_t i, j;
    for (j = 0; j < nz; j ++) {
        for (i = 0; i < nx; i ++) {
            float x = low.position[0] + (float)i, z = low.position[2] + (float)j;
            test_assert(!dp_black(px, dp_grid_px(x), dp_grid_px(z)));
            if (i + 1 < nx) {
                test_assert(dp_black(px, dp_grid_px(x + 0.5f), dp_grid_px(z)));
            }

            if (j + 1 < nz) {
                test_assert(dp_black(px, dp_grid_px(x), dp_grid_px(z + 0.5f)));
            }
        }
    }

    test_assert(dp_black(px, dp_grid_px(high.position[0] + 1), dp_grid_px(low.position[2])));
    test_assert(dp_black(px, dp_grid_px(low.position[0] - 1), dp_grid_px(low.position[2])));
    test_assert(dp_black(px, dp_grid_px(low.position[0]), dp_grid_px(high.position[2] + 1)));

    int32_t cx = dp_grid_px(low.position[0]), cz = dp_grid_px(low.position[2]);
    test_assert(!dp_black(px, cx + 2, cz));
    test_assert(dp_black(px, cx + 6, cz));
    stbi_image_free(px);
    ecs_fini(world);
}

/** A volume baked next to a red emissive wall shows a probe red on the side toward the wall
 * and not on the opposite side. The camera looks along -Z from 0.6 units in front of a probe at
 * mid height half a unit from the wall, between the probes, so that probe covers the middle of
 * the capture and its neighbours are outside it. */
void ViewDebug_light_probes_colors(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    LightBake bake = dp_light_bake(1, 0.5f);
    ecs_entity_t scene = dp_scene(world, &bake, NULL, 0);
    dp_box(world, scene, (float[]){0, -1, 0}, (float[]){6, 0.2f, 6}, (Rgb){0, 0, 0}, 0);
    dp_box(world, scene, (float[]){3, 0.5f, 0}, (float[]){0.2f, 3, 6}, (Rgb){255, 0, 0}, 0.5f);
    ecs_progress(world, 0);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, scene, (vec3){2.4f, 0.5f, 0}, &probe));
    test_bool(probe.valid, true);
    test_assert(probe.position[0] < 2.9f);
    ViewDebug debug = {.light_probes = true};
    dp_view(world, dp_camera(world, scene, (float[]){probe.position[0], probe.position[1],
        probe.position[2] + 0.6f}, (float[]){0, 0, 0}, 0), &debug);
    unsigned char *px = dp_render(world, scene, "light_probes_colors.png", NULL);

    float toward[3], away[3];
    dp_mean(px, 64 + 40, 64, 1, toward);
    dp_mean(px, 64 - 40, 64, 1, away);
    test_assert(toward[0] > 0.02f);
    test_assert(toward[0] > 2 * toward[1]);
    test_assert(toward[0] > 2 * toward[2]);
    test_assert(toward[0] > 3 * away[0]);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A box between the camera and the light probes hides them, the probes beside it show. */
void ViewDebug_light_probes_hidden_by_geometry(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    ViewDebug debug = {.light_probes = true};
    ecs_entity_t scene = dp_grid_scene(world, &debug);
    ecs_entity_t cover = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, cover, DynamicTransform);
    ecs_set(world, cover, Position, {-2.5f, 0, 0});
    ecs_set(world, cover, Box, {5, 0.2f, 10});
    ecs_set(world, cover, Rgb, {0, 0, 0});
    ecs_set(world, cover, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, cover, Emissive, {.strength = 1, .color = {0, 0, 255}});
    unsigned char *px = dp_render(world, scene, "light_probes_hidden.png", NULL);

    box_render_probe_t low, high;
    dp_grid_corners(world, scene, &low, &high);
    int32_t i, nx = (int32_t)roundf(high.position[0] - low.position[0]) + 1;
    for (i = 0; i < nx; i ++) {
        float x = low.position[0] + (float)i;
        const unsigned char *p = dg_px(px, dp_grid_px(x), dp_grid_px(low.position[2]));
        if (x < -0.25f) {
            test_assert(p[2] > 200);
            test_assert(p[0] < 20);
            test_assert(p[1] < 20);
        } else if (x > 0.25f) {
            test_assert(p[2] < 200 || p[0] > 20);
            test_assert(!dp_black(px, dp_grid_px(x), dp_grid_px(low.position[2])));
        }
    }

    stbi_image_free(px);
    ecs_fini(world);
}

/** A probe of the volume in open air, invalid for every cell around it, draws in the dark red
 * hatch; a valid probe under it, near the floor, is lit by the white ambient.
 *
 * Seen from 4 units in front along +X at 27.7 pixels a unit, the invalid probe is a unit up at
 * row 36 and the valid one a unit down at row 92, with a radius of 11 pixels. */
void ViewDebug_light_probes_invalid_marked(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    LightBake bake = dp_light_bake(2, 0);
    ecs_entity_t scene = dp_scene(world, &bake, NULL, 1);
    ecs_entity_t floor = dp_box(world, scene, (float[]){0, 0, 0}, (float[]){20, 1, 20},
        (Rgb){0, 0, 0}, 0);
    ecs_set(world, floor, Rgb, {128, 128, 128});
    ecs_set(world, floor, PbrMaterial, {.metallic = 0, .roughness = 1});
    dp_box(world, scene, (float[]){9, 20, 9}, (float[]){1, 40, 1}, (Rgb){0, 0, 0}, 0);
    ecs_progress(world, 0);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, scene, (vec3){-9, 3, -9}, &probe));
    test_flt(probe.position[1], 3);
    test_bool(probe.valid, false);
    test_assert(box_render_bake_probe(world, scene, (vec3){-9, 1, -9}, &probe));
    test_flt(probe.position[1], 1);
    test_bool(probe.valid, true);

    ViewDebug debug = {.light_probes = true, .probe_size = 0.4f};
    dp_view(world, dp_camera(world, scene, (float[]){-13, 2, -9},
        (float[]){0, -(float)GLM_PI_2, 0}, 0), &debug);
    unsigned char *px = dp_render(world, scene, "light_probes_invalid.png", NULL);

    int32_t x, y, hatched = 0, low = 255, high = 0;
    for (y = 36 - 4; y <= 36 + 4; y ++) {
        for (x = 64 - 4; x <= 64 + 4; x ++) {
            const unsigned char *p = dg_px(px, x, y);
            if (p[0] > 40 && p[1] < 20 && p[2] < 20) {
                hatched ++;
            }

            low = p[0] < low ? p[0] : low;
            high = p[0] > high ? p[0] : high;
        }
    }

    test_assert(hatched >= 70);
    test_assert(high - low > 40);

    const unsigned char *valid = dg_px(px, 64, 92);
    test_assert(valid[1] > 20);
    test_assert(valid[2] > 20);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A view with light_probes that does not sample the volume of its scene, or whose scene has
 * none, draws nothing and records no debug pass. */
void ViewDebug_light_probes_no_volume(void) {
    // debug/probes
    int32_t k;
    for (k = 0; k < 2; k ++) {
        unsigned char *px[2];
        int32_t passes[2], i;
        for (i = 0; i < 2; i ++) {
            ecs_world_t *world = dg_world();
            LightBake bake = dp_light_bake(1, 1);
            ecs_entity_t scene = dp_scene(world, k ? NULL : &bake, NULL, 1);
            dp_box(world, scene, (float[]){0, -3, 0}, (float[]){2, 0.2f, 2},
                (Rgb){200, 200, 200}, 1);
            ViewDebug debug = {.light_probes = true};
            ecs_entity_t view = dp_view(world, dp_camera(world, scene, (float[]){0, 10, 0},
                (float[]){-(float)GLM_PI_2, 0, 0}, 8), i ? &debug : NULL);
            if (!k) {
                ecs_get_mut(world, view, View)->lighting.baked = false;
                ecs_modified(world, view, View);
            }

            px[i] = dp_render(world, scene, i ? "light_probes_none_b.png" :
                "light_probes_none_a.png", &passes[i]);
            ecs_fini(world);
        }

        test_int(passes[0], 0);
        test_int(passes[1], 0);
        test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
        stbi_image_free(px[0]);
        stbi_image_free(px[1]);
    }
}

/** A closed room of black walls but for a red emissive wall at +X and a green one at -X, with a
 * reflection probe capturing from its center.
 *
 * A 60 degree camera 2.5 units along +Z looks down -Z: 44 pixels a unit at the probe.
 *
 * @param world The world
 * @param budget_ms Capture time of the ReflectionBake, 0 captures nothing
 * @param debug Debug settings of the view
 * @return The scene
 */
static ecs_entity_t dp_room(
    ecs_world_t *world,
    float budget_ms,
    const ViewDebug *debug)
{
    ReflectionBake bake = dp_reflection_bake(budget_ms);
    ecs_entity_t scene = dp_scene(world, NULL, &bake, 0);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        int32_t axis = i / 2;
        float pos[3] = {0, 0, 0}, size[3] = {6.2f, 6.2f, 6.2f};
        pos[axis] = i % 2 ? -3 : 3;
        size[axis] = 0.2f;
        Rgb color = i == 0 ? (Rgb){255, 0, 0} : (Rgb){0, 255, 0};
        dp_box(world, scene, pos, size, color, i < 2 ? 1 : 0);
    }

    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 0, 0});
    ecs_set(world, probe, ReflectionProbe, {.extent = {2.8f, 2.8f, 2.8f}, .blend = 0});
    dp_view(world, dp_camera(world, scene, (float[]){0, 0, 2.5f}, (float[]){0, 0, 0}, 0),
        debug);

    return scene;
}

/** A reflection probe sphere mirrors its capture: its right side reflects the red wall at +X,
 * its left side the green wall at -X. */
void ViewDebug_reflection_probes_mirror(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    ViewDebug debug = {.reflection_probes = true, .probe_size = 0.5f};
    ecs_entity_t scene = dp_room(world, 1, &debug);
    int32_t passes;
    unsigned char *px = dp_render(world, scene, "reflection_mirror.png", &passes);
    test_int(passes, 2);

    float right[3], left[3];
    dp_mean(px, 64 + 15, 64, 1, right);
    dp_mean(px, 64 - 15, 64, 1, left);
    test_assert(right[0] > 0.1f);
    test_assert(right[0] > 4 * right[1]);
    test_assert(right[0] > 4 * right[2]);
    test_assert(left[1] > 0.1f);
    test_assert(left[1] > 4 * left[0]);
    test_assert(left[1] > 4 * left[2]);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A reflection probe sphere is a flat gray until its probe is captured, then mirrors it. */
void ViewDebug_reflection_probes_not_ready(void) {
    // debug/probes
    ecs_world_t *world = dg_world();
    ViewDebug debug = {.reflection_probes = true, .probe_size = 0.5f};
    ecs_entity_t scene = dp_room(world, 0, &debug);
    dg_surface(world, DG_SIZE, DG_SIZE, "reflection_not_ready.png");
    dg_frames(world, 5);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 0);
    unsigned char *px = dg_load(world, "reflection_not_ready.png", DG_SIZE, DG_SIZE);
    test_assert(dp_gray(px, 64, 64));
    test_assert(dp_gray(px, 64 + 15, 64));
    test_assert(dp_gray(px, 64 - 15, 64));
    stbi_image_free(px);

    ReflectionBake bake = dp_reflection_bake(1);
    ecs_set_ptr(world, scene, ReflectionBake, &bake);
    dp_converge(world, scene);
    dg_frames(world, 2);
    px = dg_load(world, "reflection_not_ready.png", DG_SIZE, DG_SIZE);
    test_assert(!dp_gray(px, 64 + 15, 64));
    float right[3];
    dp_mean(px, 64 + 15, 64, 1, right);
    test_assert(right[0] > 4 * right[1]);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The influence box of a reflection probe turned about +Y draws in yellow lines and its
 * projection box in cyan, with their corners where the turned boxes put them.
 *
 * An orthographic camera looks down at 16 pixels a unit. A probe without a projection box
 * draws no cyan. */
void ViewDebug_reflection_boxes(void) {
    // debug/probes
    const int32_t yellow[3] = {255, 255, 0}, cyan[3] = {0, 255, 255};
    float angle = 0.5f, c = cosf(angle), s = sinf(angle);
    vec3 extent = {2.5f, 1, 1.5f}, projection = {1.5f, 0.5f, 0.75f};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ReflectionBake bake = dp_reflection_bake(1);
        ecs_entity_t scene = dp_scene(world, NULL, &bake, 0);
        dp_box(world, scene, (float[]){0, -3, 0}, (float[]){1, 0.2f, 1}, (Rgb){0, 0, 0}, 0);
        ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, probe, Position, {0, 0, 0});
        ecs_set(world, probe, Rotation, {0, angle, 0});
        ecs_set(world, probe, ReflectionProbe, {
            .extent = {extent[0], extent[1], extent[2]}, .blend = 0,
            .projection = {k ? 0 : projection[0], k ? 0 : projection[1],
                k ? 0 : projection[2]}});
        ViewDebug debug = {.reflection_probes = true};
        dp_view(world, dp_camera(world, scene, (float[]){0, 10, 0},
            (float[]){-(float)GLM_PI_2, 0, 0}, 8), &debug);
        unsigned char *px = dp_render(world, scene,
            k ? "reflection_boxes_b.png" : "reflection_boxes_a.png", NULL);

        int32_t i, cyan_found = 0;
        for (i = 0; i < 4; i ++) {
            float lx = (i & 1) ? 1.0f : -1.0f, lz = (i & 2) ? 1.0f : -1.0f;
            float ix = lx * extent[0], iz = lz * extent[2];
            float wx = c * ix + s * iz, wz = -s * ix + c * iz;
            test_assert(dp_find(px, dp_grid_px(wx), dp_grid_px(wz), 1, yellow, 60));
            float px_ = lx * projection[0], pz = lz * projection[2];
            wx = c * px_ + s * pz;
            wz = -s * px_ + c * pz;
            cyan_found += dp_find(px, dp_grid_px(wx), dp_grid_px(wz), 1, cyan, 60);
        }

        test_int(cyan_found, k ? 0 : 4);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** A scene with only the scene probe of its ReflectionBake has no sphere and no box: the view
 * records no debug pass and draws as without reflection_probes. */
void ViewDebug_reflection_scene_probe_left_out(void) {
    // debug/probes
    unsigned char *px[2];
    int32_t passes[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ReflectionBake bake = {.resolution = 32, .budget_ms = 1, .scene_probe = true,
            .scene_probe_height = 1};
        ecs_entity_t scene = dp_scene(world, NULL, &bake, 1);
        dp_box(world, scene, (float[]){0, -1, 0}, (float[]){4, 0.2f, 4},
            (Rgb){200, 100, 50}, 1);
        ViewDebug debug = {.reflection_probes = true};
        dp_view(world, dp_camera(world, scene, (float[]){0, 1, 4}, (float[]){-0.2f, 0, 0}, 0),
            k ? &debug : NULL);
        px[k] = dp_render(world, scene, k ? "scene_probe_b.png" : "scene_probe_a.png",
            &passes[k]);
        int32_t count = 0;
        const box_render_reflection_probe_t *probes = box_render_reflect_probes(
            world, scene, &count);
        test_int(count, 1);
        test_assert(probes[0].scene);
        ecs_fini(world);
    }

    test_int(passes[0], 0);
    test_int(passes[1], 0);
    test_assert(!memcmp(px[0], px[1], DG_SIZE * DG_SIZE * 4));
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A not captured reflection probe 4 units in front of a 60 degree camera, 27.7 pixels a unit,
 * gray on black.
 *
 * @param world The world
 * @param debug Debug settings of the view
 * @param view Receives the view, NULL for none
 * @return The scene
 */
static ecs_entity_t dp_gray_scene(
    ecs_world_t *world,
    const ViewDebug *debug,
    ecs_entity_t *view)
{
    ReflectionBake bake = dp_reflection_bake(0);
    ecs_entity_t scene = dp_scene(world, NULL, &bake, 0);
    dp_box(world, scene, (float[]){0, -10, 0}, (float[]){1, 0.2f, 1}, (Rgb){0, 0, 0}, 0);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 0, 0});
    ecs_set(world, probe, ReflectionProbe, {.extent = {2, 2, 2}, .blend = 0});
    ecs_entity_t v = dp_view(world, dp_camera(world, scene, (float[]){0, 0, 4},
        (float[]){0, 0, 0}, 0), debug);
    if (view) {
        *view = v;
    }

    return scene;
}

/** Count the gray pixels of the center row of a capture.
 *
 * @param px The pixels
 * @return The pixels within 8 of DP_GRAY
 */
static int32_t dp_gray_width(
    const unsigned char *px)
{
    int32_t x, result = 0;
    for (x = 0; x < DG_SIZE; x ++) {
        result += dp_gray(px, x, 64);
    }

    return result;
}

/** A probe_size sets the radius of the spheres in place of the default 0.25 of a reflection
 * probe. */
void ViewDebug_probe_size(void) {
    // debug/probes
    float sizes[2] = {0, 0.75f}, expected[2] = {0.25f, 0.75f};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ViewDebug debug = {.reflection_probes = true, .probe_size = sizes[k]};
        dp_gray_scene(world, &debug, NULL);
        unsigned char *px = dp_render(world, 0, k ? "probe_size_b.png" : "probe_size_a.png",
            NULL);
        float width = 2.0f * expected[k] * 64.0f / (4.0f * 0.57735027f);
        int32_t gray = dp_gray_width(px);
        test_assert(fabsf((float)gray - width) <= 3.0f);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** Of two views of one target only the one with reflection_probes draws the sphere: under a
 * later view without them the sphere of the first stays, and a later view with them draws it
 * over the first; one debug pass a frame either way. */
void ViewDebug_probes_per_view(void) {
    // debug/probes
    ViewDebug debug = {.reflection_probes = true};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t scene = dp_gray_scene(world, k ? NULL : &debug, NULL);
        ecs_entity_t camera = dp_camera(world, scene, (float[]){0, 0, 4}, (float[]){0, 0, 0},
            0);
        ecs_entity_t second = ecs_new(world);
        ecs_set(world, second, View, {.camera = camera,
            .lighting = {.enabled = true, .baked = true, .reflections = true}, .order = 1});
        if (k) {
            ecs_get_mut(world, second, View)->debug = debug;
            ecs_modified(world, second, View);
        }

        int32_t passes;
        unsigned char *px = dp_render(world, 0, k ? "probes_views_b.png" :
            "probes_views_a.png", &passes);
        test_int(passes, 2);
        test_assert(dp_gray(px, 64, 64));
        test_assert(dp_black(px, 64 + 20, 64));
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** A sphere keeps its color in a view with a Tonemap, which changes an emissive box beside
 * it. */
void ViewDebug_probes_with_effects(void) {
    // debug/effects
    unsigned char *px[2];
    ViewDebug debug = {.reflection_probes = true};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t view;
        ecs_entity_t scene = dp_gray_scene(world, &debug, &view);
        ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_add(world, lamp, DynamicTransform);
        ecs_set(world, lamp, Position, {1.5f, 0, 0});
        ecs_set(world, lamp, Box, {0.6f, 0.6f, 0.6f});
        ecs_set(world, lamp, Rgb, {0, 0, 0});
        ecs_set(world, lamp, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, lamp, Emissive, {.strength = 2, .color = {250, 200, 150}});
        if (k) {
            ecs_entity_t tonemap = ecs_new(world);
            ecs_add(world, tonemap, Tonemap);
            View *v = ecs_get_mut(world, view, View);
            *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
                .enabled = true, .effect = tonemap };
            ecs_modified(world, view, View);
        }

        px[k] = dp_render(world, 0, k ? "probes_effects_b.png" : "probes_effects_a.png",
            NULL);
        ecs_fini(world);
    }

    const unsigned char *a = dg_px(px[0], 64, 64), *b = dg_px(px[1], 64, 64);
    for (k = 0; k < 3; k ++) {
        test_assert(abs((int32_t)a[k] - (int32_t)b[k]) <= 3);
    }

    test_assert(dp_gray(px[1], 64, 64));
    const unsigned char *lamp_a = dg_px(px[0], 64 + 42, 64);
    const unsigned char *lamp_b = dg_px(px[1], 64 + 42, 64);
    test_assert(abs((int32_t)lamp_a[0] - (int32_t)lamp_b[0]) > 5 ||
        abs((int32_t)lamp_a[2] - (int32_t)lamp_b[2]) > 5);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A reflection probe captures its scene without the light and reflection probe spheres of the
 * view, in a scene that also bakes a probe volume the view samples. */
void ViewDebug_probes_not_in_captures(void) {
    // debug/captures
    static float plain[16 * 16 * 4], probed[16 * 16 * 4];
    float sh_plain[BOX_RENDER_SH_COEFFICIENTS][4], sh_probed[BOX_RENDER_SH_COEFFICIENTS][4];
    int32_t i, c;
    ViewDebug debug = {.light_probes = true, .reflection_probes = true, .probe_size = 0.5f};
    dg_capture(NULL, true, plain, sh_plain);
    dg_capture(&debug, true, probed, sh_probed);
    for (i = 0; i < 16 * 16 * 4; i ++) {
        test_assert(fabsf(plain[i] - probed[i]) <= 1e-3f + 0.01f * fabsf(plain[i]));
    }

    for (i = 0; i < BOX_RENDER_SH_COEFFICIENTS; i ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(sh_plain[i][c] - sh_probed[i][c]) <=
                1e-4f + 0.01f * fabsf(sh_plain[i][c]));
        }
    }
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
    dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_white, 2);
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

/** Lines of 3 pixels are three times as wide as lines of 1 pixel, close up and far away: the
 * diagonal of a quad, an edge between two triangles, crosses a row over twice the width, as
 * fwidth, the sum of the derivatives, measures the distance to a diagonal edge. */
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
            float diagonal = 64.0f + 0.3f * s;
            int32_t row = (int32_t)(64.0f - 0.3f * s);
            sums[w] = dw_green_sum(px, row, (int32_t)diagonal - 8, (int32_t)diagonal + 8);
            stbi_image_free(px);
            ecs_fini(world);
        }

        test_assert(sums[0] > 1.4f);
        test_assert(sums[0] < 2.6f);
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
        dw_view(world, dw_camera(world, 10), k ? WireframeOff : WireframeOverlay, dw_green, 2);
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
        dw_set(world, second, k ? WireframeOverlay : WireframeOff, dw_green, 2);
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
        test_assert(dg_px(px, (int32_t)(64.0f + 3.0f * s - 4.0f), row)[2] > 100);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** A box whose mesh is replaced by a larger one is wired at its new edges, not at the old
 * ones, without validation errors. */
void ViewDebug_wire_mesh_replaced(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_green, 2);
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

/** Largest of red and blue over green of the pixels of a square around a pixel, which is high
 * for magenta and about 0 for white or gray.
 *
 * @param px The pixels
 * @param x Column of the center
 * @param y Row of the center
 * @param radius Pixels from the center to the edges of the square
 * @return The largest of the smaller of red and blue minus green, display encoded
 */
static int32_t dw_magenta_peak(
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
            int32_t rb = p[0] < p[2] ? p[0] : p[2];
            int32_t d = rb - (int32_t)p[1];
            result = d > result ? d : result;
        }
    }

    return result;
}

/** A view with the grid and the overlay on from its first frame draws both in its debug pass,
 * without validation errors: the grid lines away from a box, magenta lines along the edges of
 * the box, and neither in the black top of the box between its lines. */
void ViewDebug_wire_with_grid(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    ecs_entity_t view = dg_view(world, dg_camera(world, 10, -(float)GLM_PI_2), &dg_grid);
    dw_set(world, view, WireframeOverlay, (rgba){255, 0, 255, 255}, 2);
    dg_box(world, (float[]){2.5f, 0.5f, -2.5f}, (float[]){3, 1, 3}, (Rgb){0, 0, 0});
    unsigned char *px = dg_render(world, "wire_grid.png");
    int32_t draws, passes;
    dw_count(world, &draws, &passes);
    test_int(passes, 2);

    dg_expect_lines(px, DG_SCALE, 1);

    float s = dw_scale(9);
    int32_t row = (int32_t)(64.0f - 2.5f * s);
    test_assert(dw_magenta_peak(px, (int32_t)(64.0f + 1.0f * s), row, 3) > 100);
    test_assert(dw_magenta_peak(px, (int32_t)(64.0f + 4.0f * s), row, 3) > 100);

    const unsigned char *top = dg_px(px, (int32_t)(64.0f + 3.25f * s), row);
    test_assert(top[0] < 30);
    test_assert(top[1] < 30);
    test_assert(top[2] < 30);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The border edge of a quad, with a triangle on one side only, has the width of the view like
 * its diagonal between two triangles, and at 3 pixels is three times as wide as at 1.
 *
 * At 1 pixel the left edge crosses a row over about a pixel and the diagonal over about two:
 * fwidth measures the distance to a diagonal edge short by the square root of two, and a row
 * crosses it over that much more. */
void ViewDebug_wire_silhouette_quad(void) {
    // debug/wireframe
    float borders[2], diagonals[2];
    int32_t w;
    for (w = 0; w < 2; w ++) {
        ecs_world_t *world = dg_world();
        dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_green, w ? 3.0f : 1.0f);
        ecs_entity_t quad = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
        ecs_set(world, quad, Quad, {6, 6});
        unsigned char *px = dg_render(world,
            w ? "wire_silhouette_quad_b.png" : "wire_silhouette_quad_a.png");
        float s = dw_scale(10);
        int32_t row = (int32_t)(64.0f - 1.5f * s);
        float left = 64.0f - 3.0f * s;
        borders[w] = dw_green_sum(px, row, (int32_t)left - 4, (int32_t)left + 6);
        diagonals[w] = dw_green_sum(px, row, (int32_t)(64.0f - 1.5f * s) - 8,
            (int32_t)(64.0f - 1.5f * s) + 8) + dw_green_sum(px, row,
            (int32_t)(64.0f + 1.5f * s) - 8, (int32_t)(64.0f + 1.5f * s) + 8);
        stbi_image_free(px);
        ecs_fini(world);
    }

    test_assert(borders[0] > 0.7f);
    test_assert(borders[0] < 1.4f);
    test_assert(borders[0] / (diagonals[0] * 0.5f) > 0.7f);
    test_assert(borders[0] / (diagonals[0] * 0.5f) < 1.4f);
    test_assert(borders[1] > 2.3f);
    test_assert(borders[1] < 3.7f);
    test_assert(borders[1] / borders[0] > 2.4f);
    test_assert(borders[1] / borders[0] < 3.6f);
}

/** The outline of a sphere at 1 pixel in the overlay has the width of its interior lines.
 *
 * Seen from the side by an orthographic camera, its silhouette is the meridian at the left
 * edge, between triangles facing the camera and away from it; the next meridian, between two
 * triangles facing it, crosses a row of the band above the equator over as many pixels. */
void ViewDebug_wire_silhouette_sphere(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Rotation, {0, 0, 0});
    ecs_set(world, camera, Camera, {.near_ = 0.1f, .far_ = 100, .ortho = true,
        .ortho_height = 6});
    dw_view(world, camera, WireframeOverlay, dw_green, 1);
    ecs_entity_t sphere = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    ecs_set(world, sphere, Sphere, {.segments = 8, .radius = 2});
    unsigned char *px = dg_render(world, "wire_silhouette_sphere.png");

    float s = 128.0f / 6.0f;
    int32_t row = 64 - 4;
    float outline = 64.0f - 2.0f * s, meridian = 64.0f - 2.0f * 0.70710678f * s;
    float outer = dw_green_sum(px, row, (int32_t)outline - 3, (int32_t)outline + 3);
    float inner = dw_green_sum(px, row, (int32_t)meridian - 3, (int32_t)meridian + 3);
    test_assert(inner > 0.6f);
    test_assert(inner < 1.6f);
    test_assert(outer / inner > 0.7f);
    test_assert(outer / inner < 1.45f);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The seam of a sphere, where its first and last meridian meet at positions computed apart,
 * is not double width: turned around Y in steps of a segment, each meridian in turn crosses the
 * same row at the same column with the width of the view. */
void ViewDebug_wire_sphere_seam(void) {
    // debug/wireframe
    float s = 128.0f / 6.0f, lo = 1e9f, hi = 0;
    float meridian = 64.0f - 2.0f * 0.70710678f * s;
    int32_t k;
    for (k = 0; k < 8; k ++) {
        ecs_world_t *world = dg_world();
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 10});
        ecs_set(world, camera, Rotation, {0, 0, 0});
        ecs_set(world, camera, Camera, {.near_ = 0.1f, .far_ = 100, .ortho = true,
            .ortho_height = 6});
        dw_view(world, camera, WireframeOverlay, dw_green, 1);
        ecs_entity_t sphere = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
        ecs_set(world, sphere, Sphere, {.segments = 8, .radius = 2});
        ecs_set(world, sphere, Rotation, {0, (float)k * (float)GLM_PI_4, 0});
        unsigned char *px = dg_render(world, "wire_sphere_seam.png");
        float sum = dw_green_sum(px, 64 - 4, (int32_t)meridian - 3, (int32_t)meridian + 3);
        lo = sum < lo ? sum : lo;
        hi = sum > hi ? sum : hi;
        stbi_image_free(px);
        ecs_fini(world);
    }

    test_assert(lo > 0.6f);
    test_assert(hi < 1.6f);
    test_assert(hi / lo < 1.45f);
}

/** The edges of a box, whose faces have vertices of their own, are not double width: turned by
 * 45 degrees around Y, the edge between the two faces the camera sees has the width of the
 * view, as do its outline edges on either side. */
void ViewDebug_wire_box_edges(void) {
    // debug/wireframe
    ecs_world_t *world = dg_world();
    dw_view(world, dw_camera(world, 10), WireframeOverlay, dw_green, 1);
    ecs_entity_t box = dw_shape(world, 0, 0, 0, (Rgb){255, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rotation, {0, (float)GLM_PI_4, 0});
    unsigned char *px = dg_render(world, "wire_box_edges.png");

    int32_t row = 64;
    float middle = dw_green_sum(px, row, 64 - 3, 64 + 3);
    float side = 1.41421356f * dw_scale(10);
    float left = dw_green_sum(px, row, (int32_t)(64.0f - side) - 3, (int32_t)(64.0f - side) + 3);
    float right = dw_green_sum(px, row, (int32_t)(64.0f + side) - 3,
        (int32_t)(64.0f + side) + 3);
    test_assert(middle > 0.6f);
    test_assert(middle < 1.45f);
    test_assert(left > 0.6f);
    test_assert(left < 1.45f);
    test_assert(right > 0.6f);
    test_assert(right < 1.45f);
    stbi_image_free(px);
    ecs_fini(world);
}
