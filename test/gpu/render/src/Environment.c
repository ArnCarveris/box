/**
 * @file Environment.c
 * @brief Tests for the cubemap the GPU converts an EnvironmentImage into.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "environment.h"

/** Resolution of the cubemap of the tests */
#define RES (16)

/** Color of each face of axes.hdr in WebGPU cube order: +X, -X, +Y, -Y, +Z, -Z */
static const float face_colors[6][3] = {
    {1, 0, 0}, {0, 1, 1}, {0, 1, 0}, {1, 0, 1}, {0, 0, 1}, {1, 1, 0}
};

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
    ecs_set(world, source, EnvironmentImage, {.path = "test/assets/environments/axes.hdr"});

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
