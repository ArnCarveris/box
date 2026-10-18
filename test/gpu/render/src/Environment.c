/**
 * @file Environment.c
 * @brief Tests for the cubemap the GPU converts an EnvironmentImage into and its processing:
 * the mips, the SH irradiance and their GPU time, and the background a view draws from it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include "environment.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Resolution of the cubemap of the tests */
#define RES (16)

/** Color of each face of axes.hdr in WebGPU cube order: +X, -X, +Y, -Y, +Z, -Z */
static const float face_colors[6][3] = {
    {1, 0, 0}, {0, 1, 1}, {0, 1, 0}, {1, 0, 1}, {0, 0, 1}, {1, 1, 0}
};

/** SH coefficient along x, y and z. axes.hdr has the color of the channel of an axis toward
 * + and the other two channels toward -, so only that channel is positive along the axis. */
static const int32_t axis_coefficient[3] = { 3, 1, 2 };

/** Frames a test renders at least while it waits for the GPU */
#define FRAMES (100)

/** Seconds a test waits for the GPU at least: on a loaded machine the image loads and the
 * readbacks of the GPU take longer than FRAMES frames of a tiny surface */
#define WAIT_SECONDS (20.0)

/** Check whether a wait goes on: for FRAMES frames and WAIT_SECONDS seconds, whichever is longer.
 *
 * @param frame Frames waited
 * @param start Time the wait started
 * @return Whether to render another frame
 */
static bool env_waiting(
    int32_t frame,
    const ecs_time_t *start)
{
    ecs_time_t now;
    ecs_os_get_time(&now);

    return frame < FRAMES || ecs_time_to_double(ecs_time_sub(now, *start)) < WAIT_SECONDS;
}

/** Create a headless world that renders on the main thread into a surface.
 *
 * @return The world
 */
static ecs_world_t* env_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .resolution_scale = 1});

    return world;
}

/** Create an EnvironmentImage source.
 *
 * @param world The world
 * @param path The image
 * @param resolution Environment.resolution
 * @param filter Environment.filter
 * @return The source
 */
static ecs_entity_t env_source(
    ecs_world_t *world,
    const char *path,
    int32_t resolution,
    EnvironmentFilter filter)
{
    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Environment, {.resolution = resolution, .filter = filter});
    ecs_set(world, source, EnvironmentImage, {.path = ECS_CONST_CAST(char*, path)});

    return source;
}

/** Get what the GPU did with the cubemap of a source.
 *
 * @param world The world
 * @param source The source
 * @param out Receives the state
 * @return Whether the cubemap exists
 */
static bool env_state(
    ecs_world_t *world,
    ecs_entity_t source,
    box_render_environment_state_t *out)
{
    uint32_t handle = box_render_environment_find(world, source);

    return handle && box_render_gpu_environment_state(world, handle, out);
}

/** Render frames until the cubemap of a source was processed for the version of its
 * EnvironmentStatus and the filter of its Environment.
 *
 * @param world The world
 * @param source The source
 * @param out Receives the state
 * @return Whether it was processed while env_waiting
 */
static bool env_wait(
    ecs_world_t *world,
    ecs_entity_t source,
    box_render_environment_state_t *out)
{
    bool result = false;
    int32_t frame;
    ecs_time_t start;
    ecs_os_get_time(&start);
    for (frame = 0; env_waiting(frame, &start) && !result; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        int32_t version = ecs_get(world, source, EnvironmentStatus)->version;
        uint32_t filter = (uint32_t)ecs_get(world, source, Environment)->filter;
        result = env_state(world, source, out) && version && out->processed == version &&
            out->filter == filter;
    }

    return result;
}

/** Render frames until the EnvironmentStatus of a source reaches a version, which it does once
 * the image of a changed EnvironmentImage loaded.
 *
 * @param world The world
 * @param source The source
 * @param version The version
 * @return Whether it reached it while env_waiting
 */
static bool env_wait_version(
    ecs_world_t *world,
    ecs_entity_t source,
    int32_t version)
{
    bool result = false;
    int32_t frame;
    ecs_time_t start;
    ecs_os_get_time(&start);
    for (frame = 0; env_waiting(frame, &start) && !result; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        result = ecs_get(world, source, EnvironmentStatus)->version >= version;
    }

    return result;
}

/** Render frames.
 *
 * @param world The world
 * @param count Frames
 */
static void env_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t frame;
    for (frame = 0; frame < count; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }
}

/** Check that the backend reported no error.
 *
 * @param world The world
 */
static void env_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

void Environment_axes_faces_show_axis_colors(void) {
    // environment/image
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .resolution_scale = 1});
    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Environment, {.resolution = RES});
    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/axes.hdr"});

    static float texels[RES * RES * 4];
    bool read = false;
    int32_t frame;
    for (frame = 0; frame < 100 && !read; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        uint32_t handle = box_render_environment_find(world, source);
        read = handle && box_render_gpu_read_environment(world, handle, 0, texels,
            RES * RES * 4);
    }

    test_assert(read);
    test_int(ecs_get(world, source, EnvironmentStatus)->version, 1);
    test_assert(ecs_get(world, source, EnvironmentStatus)->error == NULL);

    uint32_t handle = box_render_environment_find(world, source);
    uint32_t face;
    int32_t x, y, c;
    for (face = 0; face < 6; face ++) {
        test_assert(box_render_gpu_read_environment(world, handle, face, texels,
            RES * RES * 4));
        for (y = RES / 4; y < RES * 3 / 4; y ++) {
            for (x = RES / 4; x < RES * 3 / 4; x ++) {
                const float *t = &texels[(y * RES + x) * 4];
                for (c = 0; c < 3; c ++) {
                    test_assert(fabsf(t[c] - face_colors[face][c]) < 0.02f);
                }

                test_flt(t[3], 1);
            }
        }
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void Environment_unchanged_source_processed_once(void) {
    // environment/processing
    ecs_world_t *world = env_world();
    ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", RES,
        EnvironmentFilterGgx);
    box_render_environment_state_t state;
    test_assert(env_wait(world, source, &state));
    test_int(state.written, 1);
    test_int(state.processed, 1);
    test_int(state.runs, 1);

    env_frames(world, 10);
    test_assert(env_state(world, source, &state));
    test_int(state.processed, 1);
    test_int(state.runs, 1);
    env_no_errors(world);

    ecs_fini(world);
}

void Environment_changed_path_reprocesses(void) {
    // environment/processing
    ecs_world_t *world = env_world();
    ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", RES,
        EnvironmentFilterGgx);
    box_render_environment_state_t state;
    test_assert(env_wait(world, source, &state));
    test_int(state.runs, 1);

    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/sky.hdr"});
    test_assert(env_wait_version(world, source, 2));
    test_assert(env_wait(world, source, &state));
    test_int(ecs_get(world, source, EnvironmentStatus)->version, 2);
    test_int(state.written, 2);
    test_int(state.processed, 2);
    test_int(state.runs, 2);

    env_frames(world, 10);
    test_assert(env_state(world, source, &state));
    test_int(state.runs, 2);
    env_no_errors(world);

    ecs_fini(world);
}

void Environment_changed_filter_reprocesses(void) {
    // environment/processing
    ecs_world_t *world = env_world();
    ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", RES,
        EnvironmentFilterGgx);
    box_render_environment_state_t state;
    test_assert(env_wait(world, source, &state));
    test_int(state.filter, EnvironmentFilterGgx);
    test_int(state.runs, 1);

    ecs_set(world, source, Environment, {.resolution = RES, .filter = EnvironmentFilterBox});
    test_assert(env_wait(world, source, &state));
    test_int(state.filter, EnvironmentFilterBox);
    test_int(state.processed, 1);
    test_int(state.runs, 2);

    env_frames(world, 10);
    test_assert(env_state(world, source, &state));
    test_int(state.runs, 2);

    ecs_set(world, source, Environment, {.resolution = RES, .filter = EnvironmentFilterGgx});
    test_assert(env_wait(world, source, &state));
    test_int(state.filter, EnvironmentFilterGgx);
    test_int(state.runs, 3);
    env_no_errors(world);

    ecs_fini(world);
}

void Environment_shared_source_processed_once(void) {
    // environment/processing
    ecs_world_t *world = env_world();
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", RES,
        EnvironmentFilterGgx);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = camera,
            .lighting = {.enabled = true, .environment = {.source = source, .intensity = 1}}
        });
    }

    box_render_environment_state_t state;
    test_assert(env_wait(world, source, &state));
    test_int(state.processed, 1);
    test_int(state.runs, 1);

    env_frames(world, 10);
    test_assert(env_state(world, source, &state));
    test_int(state.runs, 1);
    env_no_errors(world);

    ecs_fini(world);
}

void Environment_axes_sh_signs(void) {
    // environment/processing
    EnvironmentFilter filters[2] = { EnvironmentFilterGgx, EnvironmentFilterBox };
    int32_t f;
    for (f = 0; f < 2; f ++) {
        ecs_world_t *world = env_world();
        ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", 64, filters[f]);
        box_render_environment_state_t state;
        test_assert(env_wait(world, source, &state));

        float sh[BOX_RENDER_SH_COEFFICIENTS][4];
        test_assert(box_render_gpu_read_environment_sh(world,
            box_render_environment_find(world, source), sh));

        int32_t axis, c;
        for (c = 0; c < 3; c ++) {
            test_assert(sh[0][c] > 0.5f);
        }

        for (axis = 0; axis < 3; axis ++) {
            const float *k = sh[axis_coefficient[axis]];
            for (c = 0; c < 3; c ++) {
                if (c == axis) {
                    test_assert(k[c] > 0.1f);
                } else {
                    test_assert(k[c] < -0.1f);
                }
            }
        }

        env_no_errors(world);
        ecs_fini(world);
    }
}

void Environment_processing_time_measured(void) {
    // environment/processing
    static const int32_t resolutions[3] = { 64, 128, 256 };
    static const EnvironmentFilter filters[2] = { EnvironmentFilterGgx, EnvironmentFilterBox };
    static const char *names[2] = { "Ggx", "Box" };
    ecs_world_t *world = env_world();
    box_render_bake_timing_t timing;
    int32_t r, f, frame;
    for (r = 0; r < 3; r ++) {
        for (f = 0; f < 2; f ++) {
            ecs_entity_t source = env_source(world, "etc/environments/sky.hdr",
                resolutions[r], filters[f]);
            box_render_environment_state_t state;
            test_assert(box_render_gpu_environment_timing(world, &timing));
            test_assert(env_wait(world, source, &state));
            test_int(state.runs, 1);

            int32_t processed = 0;
            double ms = 0;
            ecs_time_t start;
            ecs_os_get_time(&start);
            for (frame = 0; env_waiting(frame, &start) && !processed; frame ++) {
                test_assert(box_render_gpu_environment_timing(world, &timing));
                processed += timing.probes;
                ms += timing.ms;
                env_frames(world, 1);
            }

            test_int(processed, 1);
            test_assert(ms > 0);
            printf("environment %d^2 %s: %.3f ms (%s)\n", resolutions[r], names[f], ms,
                timing.measured ? "timestamps" : "cpu");
            ecs_delete(world, source);
            env_frames(world, 1);
        }
    }

    env_no_errors(world);
    ecs_fini(world);
}

/** Edge of the image of the background tests */
#define BACKGROUND_SIZE (32)

/** Render the background of a view that looks along -Z at nothing, lit by axes.hdr, and get the
 * pixel at the center of the image.
 *
 * @param yaw EnvironmentSettings.yaw
 * @param background EnvironmentSettings.background
 * @param out Receives red, green and blue
 */
static void background_center(
    float yaw,
    bool background,
    int out[3])
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = BACKGROUND_SIZE, .height = BACKGROUND_SIZE,
        .write_to_file = "background.png", .resolution_scale = 1});
    ecs_entity_t source = env_source(world, "etc/environments/axes.hdr", RES,
        EnvironmentFilterGgx);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .environment = {
            .source = source, .intensity = 1, .yaw = yaw, .background = background
        }}
    });

    box_render_environment_state_t state;
    test_assert(env_wait(world, source, &state));
    env_frames(world, 3);
    env_no_errors(world);

    int w = 0, h = 0, n = 0;
    char *path = flecs_asprintf("%s/background.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *pixels = stbi_load(path, &w, &h, &n, 4);
    test_assert(pixels != NULL);
    test_int(w, BACKGROUND_SIZE);
    test_int(h, BACKGROUND_SIZE);
    const unsigned char *p = pixels +
        ((BACKGROUND_SIZE / 2) * BACKGROUND_SIZE + BACKGROUND_SIZE / 2) * 4;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    stbi_image_free(pixels);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Check that a pixel shows a color of axes.hdr.
 *
 * @param pixel Red, green and blue of the pixel
 * @param color The linear color, each channel 0 or 1
 */
static void expect_axis_color(
    const int pixel[3],
    const float color[3])
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        if (color[c] > 0.5f) {
            test_assert(pixel[c] > 230);
        } else {
            test_assert(pixel[c] < 25);
        }
    }
}

void Environment_background_shows_environment(void) {
    // environment/background
    int pixel[3];
    background_center(0, true, pixel);
    expect_axis_color(pixel, face_colors[5]);
}

/* Turned a quarter about +Y, as Rotation.y turns an entity, +X of the environment looks along
 * -Z, toward the center of the image; turned the other way -X does, and turned half +Z does. */
void Environment_background_follows_yaw(void) {
    // environment/background
    int pixel[3];
    background_center((float)M_PI * 0.5f, true, pixel);
    expect_axis_color(pixel, face_colors[0]);
    background_center((float)M_PI * -0.5f, true, pixel);
    expect_axis_color(pixel, face_colors[1]);
    background_center((float)M_PI, true, pixel);
    expect_axis_color(pixel, face_colors[4]);
}

void Environment_background_off_keeps_clear_color(void) {
    // environment/background
    int pixel[3];
    background_center(0, false, pixel);
    test_int(pixel[0], 0);
    test_int(pixel[1], 0);
    test_int(pixel[2], 0);
}
