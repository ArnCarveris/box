/**
 * @file MultiViewCull.c
 * @brief Tests for the cull of a view and its shadow cascades in one pass per page.
 *
 * The camera looks down -z from the origin into a 64x64 image, with four cascades up to 40 units
 * and a sun at 45 degrees. The expected counts come from the frustums worked out on the CPU: the
 * view's from its projection, each cascade's from the cascade fit of its slice of the view.
 */

#include <box.h>
#include <bake_test.h>
#include <float.h>
#include <math.h>
#include "render.h"

/** Cascades of the views of the tests */
#define CASCADES (4)

/** Plane distance below which a box is on the edge of a frustum and left out of a scene */
#define EDGE (0.05f)

/** Copies of cascade 0 that make a view cull more frustums than one cull group holds */
#define COPIES (40)

/** World space planes of the view and of each cascade, as the cull pass makes them */
typedef struct frustums_t {
    vec4 planes[1 + CASCADES][6]; /**< The view, then the cascades */
} frustums_t;

/** What the cull of a scene must keep, worked out on the CPU */
typedef struct expect_t {
    int32_t tested;             /**< Boxes */
    int32_t visible;            /**< Boxes in the view */
    int32_t casters;            /**< Boxes that cast shadows */
    int32_t cascade[CASCADES];  /**< Casters in each cascade */
    int32_t offscreen;          /**< Casters outside the view in a cascade */
    int32_t unshadowed;         /**< Boxes without shadows in the view */
} expect_t;

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create the camera, its view with cascaded shadows, the sun and the surface.
 *
 * @param world The world
 * @param occlusion Whether the view culls by occlusion
 * @param camera Receives the camera
 * @param sun Receives the sun
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    bool occlusion,
    ecs_entity_t *camera,
    ecs_entity_t *sun)
{
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {.camera = *camera,
        .culling = {.enabled = true, .occlusion = occlusion},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = CASCADES, .resolution = 1024, .max_distance = 40,
            .split_lambda = 0.5f, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    *sun = ecs_new(world);
    ecs_set(world, *sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, *sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    return result;
}

/** Write the normalized world space planes of a clip space matrix, as the cull pass does.
 *
 * @param m World to clip space
 * @param out Receives the six planes
 */
static void planes_of(
    mat4 m,
    vec4 *out)
{
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        int32_t row = i / 2;
        float sign = (i & 1) ? -1.0f : 1.0f;
        for (j = 0; j < 4; j ++) {
            out[i][j] = m[j][3] + sign * m[j][row];
        }
    }

    for (j = 0; j < 4; j ++) {
        out[4][j] = m[j][3] - m[j][2];
        out[5][j] = m[j][2];
    }

    for (i = 0; i < 6; i ++) {
        float len = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1] +
            out[i][2] * out[i][2]);
        if (len < 1e-12f) {
            glm_vec4_copy((vec4){0, 0, 0, 1}, out[i]);
        } else {
            glm_vec4_scale(out[i], 1.0f / len, out[i]);
        }
    }
}

/** Get how far inside a frustum a box reaches: the smallest distance of its farthest corner
 * inside each plane, negative when it is outside one.
 *
 * @param planes The planes
 * @param center Center of the box
 * @param extent Half size of the box
 * @return The distance
 */
static float inside_by(
    vec4 *planes,
    const float *center,
    const float *extent)
{
    float result = FLT_MAX;
    int32_t i;
    for (i = 0; i < 6; i ++) {
        float d = planes[i][0] * center[0] + planes[i][1] * center[1] +
            planes[i][2] * center[2] + planes[i][3] + fabsf(planes[i][0]) * extent[0] +
            fabsf(planes[i][1]) * extent[1] + fabsf(planes[i][2]) * extent[2];
        result = d < result ? d : result;
    }

    return result;
}

/** Work out the frustums of the view of view_new after a frame computed the transforms.
 *
 * @param world The world
 * @param camera The camera
 * @param sun The sun
 * @param out Receives the planes
 */
static void frustums_of(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t sun,
    frustums_t *out)
{
    const WorldTransform *ct = ecs_get(world, camera, WorldTransform);
    const WorldTransform *st = ecs_get(world, sun, WorldTransform);
    mat4 proj, cam, view, view_proj;
    vec3 direction;
    float splits[CASCADES];
    int32_t i;
    box_camera_projection(ecs_get(world, camera, Camera), 64, 64, proj);
    if (ct) {
        box_transform_mat4(ct, NULL, cam);
    } else {
        glm_mat4_identity(cam);
    }

    glm_mat4_inv(cam, view);
    glm_mat4_mul(proj, view, view_proj);
    planes_of(view_proj, out->planes[0]);
    if (st) {
        glm_vec3_copy(ECS_CONST_CAST(float*, st->basis[2]), direction);
    } else {
        glm_vec3_copy((vec3){0, 0, 1}, direction);
    }

    glm_vec3_normalize(direction);
    glm_vec3_negate(direction);
    box_render_shadow_splits(0.1f, 40, 0.5f, CASCADES, splits);
    for (i = 0; i < CASCADES; i ++) {
        box_render_cascade_t c;
        box_render_shadow_cascade(cam, proj, i ? splits[i - 1] : 0.1f, splits[i], direction,
            1024, splits[CASCADES - 1], &c);
        planes_of(c.matrix, out->planes[1 + i]);
    }
}

/** Add unit boxes on a grid around the camera, every fifth without shadows, leaving out the
 * ones on the edge of a frustum, and work out what the cull keeps.
 *
 * @param world The world
 * @param f The frustums
 * @param out Receives the expected counts
 */
static void boxes_new(
    ecs_world_t *world,
    frustums_t *f,
    expect_t *out)
{
    float ys[3] = {-4, 3, 12};
    float extent[3] = {0.5f, 0.5f, 0.5f};
    int32_t x, y, z, i, n = 0;
    ecs_os_memset_t(out, 0, expect_t);
    for (x = -20; x <= 20; x += 5) {
        for (y = 0; y < 3; y ++) {
            for (z = -55; z <= 15; z += 5) {
                float center[3] = {(float)x, ys[y], (float)z};
                bool in[1 + CASCADES], edge = false;
                for (i = 0; i < 1 + CASCADES; i ++) {
                    float d = inside_by(f->planes[i], center, extent);
                    in[i] = d >= 0;
                    edge |= fabsf(d) < EDGE;
                }

                if (edge) {
                    continue;
                }

                bool shadow = (n % 5) != 0;
                ecs_entity_t e = ecs_new(world);
                ecs_set(world, e, Position, {center[0], center[1], center[2]});
                ecs_set(world, e, Box, {1, 1, 1});
                ecs_set(world, e, Rgb, {255, 255, 255});
                if (!shadow) {
                    ecs_add(world, e, NoShadow);
                }

                out->tested ++;
                out->visible += in[0];
                out->unshadowed += !shadow && in[0];
                if (shadow) {
                    bool cast = false;
                    out->casters ++;
                    for (i = 0; i < CASCADES; i ++) {
                        out->cascade[i] += in[1 + i];
                        cast |= in[1 + i];
                    }

                    out->offscreen += cast && !in[0];
                }

                n ++;
            }
        }
    }
}

/** Render a frame and get the cull dispatches it recorded.
 *
 * @param world The world
 * @param all Receives the compute dispatches of the cull of views
 * @param instances Receives the cull_instances dispatches among them
 */
static void frame_dispatches(
    ecs_world_t *world,
    int32_t *all,
    int32_t *instances)
{
    box_render_gpu_stats_t before, after;
    test_bool(box_render_gpu_stats(world, &before), true);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    test_bool(box_render_gpu_stats(world, &after), true);
    *all = after.cull_dispatches - before.cull_dispatches;
    *instances = after.cull_instance_dispatches - before.cull_instance_dispatches;
}

/** Render frames.
 *
 * @param world The world
 * @param count Frames
 */
static void frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Add a row of unit casters in front of the camera.
 *
 * @param world The world
 * @param count Boxes
 */
static void row_new(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 5) * 2 - 4, 0, -10 - (float)(i / 5) * 3});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }
}

void MultiViewCull_one_pass_without_occlusion(void) {
    // culling/multi_view
    ecs_world_t *world = world_new();
    ecs_entity_t camera, sun;
    ecs_entity_t view = view_new(world, false, &camera, &sun);
    row_new(world, 20);
    frames(world, 3);

    int32_t all = 0, instances = 0, i;
    for (i = 0; i < 2; i ++) {
        frame_dispatches(world, &all, &instances);
        test_int(instances, 1);
        test_int(all, 3);
    }

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 20);
    test_int(stats.visible, 20);
    test_int(stats.casters_tested, 20 * CASCADES);
    test_assert(stats.casters_visible >= 20);

    ecs_fini(world);
}

void MultiViewCull_one_pass_with_occlusion(void) {
    // culling/multi_view
    ecs_world_t *world = world_new();
    ecs_entity_t camera, sun;
    ecs_entity_t view = view_new(world, true, &camera, &sun);
    row_new(world, 20);
    frames(world, 3);

    int32_t all = 0, instances = 0, i;
    for (i = 0; i < 2; i ++) {
        frame_dispatches(world, &all, &instances);
        test_int(instances, 2);
        test_int(all, 6);
    }

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 20);
    test_int(stats.casters_tested, 20 * CASCADES);
    test_assert(stats.visible > 0);
    test_assert(stats.visible <= 20);

    ecs_fini(world);
}

void MultiViewCull_stats_match_scene(void) {
    // culling/multi_view
    ecs_world_t *world = world_new();
    ecs_entity_t camera, sun;
    ecs_entity_t view = view_new(world, false, &camera, &sun);
    frames(world, 1);

    frustums_t f;
    expect_t expect;
    frustums_of(world, camera, sun, &f);
    boxes_new(world, &f, &expect);
    test_assert(expect.visible > 0);
    test_assert(expect.visible < expect.tested);
    test_assert(expect.offscreen > 0);
    test_assert(expect.unshadowed > 0);
    frames(world, 2);

    box_render_cull_stats_t stats;
    int32_t i, casters = 0;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, expect.tested);
    test_int(stats.visible, expect.visible);
    test_int(stats.casters_tested, expect.casters * CASCADES);
    for (i = 0; i < CASCADES; i ++) {
        box_render_cull_stats_t one;
        test_bool(box_render_gpu_cull_frustum(world, view, i, &one), true);
        test_int(one.tested, expect.casters);
        test_int(one.visible, expect.cascade[i]);
        casters += expect.cascade[i];
    }

    test_int(stats.casters_visible, casters);
    box_render_cull_stats_t own;
    test_bool(box_render_gpu_cull_frustum(world, view, -1, &own), true);
    test_int(own.tested, expect.tested);
    test_int(own.visible, expect.visible);
    test_bool(box_render_gpu_cull_frustum(world, view, CASCADES, &own), false);

    uint32_t word = 0;
    for (i = 0; i < 50; i ++) {
        const CullStatus *st = ecs_get(world, view, CullStatus);
        if (st && st->instances == expect.tested) {
            break;
        }

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    const CullStatus *st = ecs_get(world, view, CullStatus);
    test_assert(st != NULL);
    test_int(st->instances, expect.tested);
    test_int(st->visible, expect.visible);
    test_int(st->frustum_culled, expect.tested - expect.visible);
    test_int(st->occlusion_culled, 0);

    ecs_fini(world);
}

void MultiViewCull_split_over_32_frustums(void) {
    // culling/multi_view
    ecs_world_t *world = world_new();
    ecs_entity_t camera, sun;
    ecs_entity_t view = view_new(world, false, &camera, &sun);
    frames(world, 1);

    frustums_t f;
    expect_t expect;
    frustums_of(world, camera, sun, &f);
    boxes_new(world, &f, &expect);
    frames(world, 2);

    box_render_cull_stats_t alone, cascade[CASCADES];
    int32_t i, all = 0, instances = 0;
    test_bool(box_render_gpu_cull_stats(world, view, &alone), true);
    for (i = 0; i < CASCADES; i ++) {
        test_bool(box_render_gpu_cull_frustum(world, view, i, &cascade[i]), true);
    }

    test_assert(cascade[0].visible > 0);
    frame_dispatches(world, &all, &instances);
    test_int(instances, 1);
    test_int(all, 3);

    test_bool(box_render_gpu_cull_copies(world, COPIES), true);
    frames(world, 1);
    frame_dispatches(world, &all, &instances);
    test_int(instances, 2);
    test_int(all, 6);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, alone.tested);
    test_int(stats.visible, alone.visible);
    test_int(stats.casters_tested, alone.casters_tested + COPIES * cascade[0].tested);
    test_int(stats.casters_visible, alone.casters_visible + COPIES * cascade[0].visible);
    for (i = 0; i < CASCADES + COPIES; i ++) {
        box_render_cull_stats_t one;
        const box_render_cull_stats_t *same = &cascade[i < CASCADES ? i : 0];
        test_bool(box_render_gpu_cull_frustum(world, view, i, &one), true);
        test_int(one.tested, same->tested);
        test_int(one.visible, same->visible);
    }

    test_bool(box_render_gpu_cull_copies(world, 0), true);
    frames(world, 1);
    frame_dispatches(world, &all, &instances);
    test_int(instances, 1);
    test_bool(box_render_gpu_cull_frustum(world, view, CASCADES, &stats), false);

    ecs_fini(world);
}

void MultiViewCull_views_cull_separately(void) {
    // culling/multi_view
    ecs_world_t *world = world_new();
    ecs_entity_t left_camera = ecs_new(world);
    ecs_set(world, left_camera, Position, {-20, 0, 10});
    ecs_set(world, left_camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t right_camera = ecs_new(world);
    ecs_set(world, right_camera, Position, {20, 0, 10});
    ecs_set(world, right_camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t left = ecs_new(world);
    ecs_set(world, left, View, {.camera = left_camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t right = ecs_new(world);
    ecs_set(world, right, View, {.camera = right_camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    int32_t i, all = 0, instances = 0;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {-20 + (float)i * 2 - 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    for (i = 0; i < 5; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {20 + (float)i * 2 - 4, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    frames(world, 2);
    for (i = 0; i < 4; i ++) {
        box_render_cull_stats_t stats;
        frame_dispatches(world, &all, &instances);
        test_int(instances, 4);
        test_int(all, 12);
        test_bool(box_render_gpu_cull_stats(world, left, &stats), true);
        test_int(stats.tested, 8);
        test_int(stats.visible, 3);
        test_int(stats.casters_tested, 0);
        test_bool(box_render_gpu_cull_stats(world, right, &stats), true);
        test_int(stats.tested, 8);
        test_int(stats.visible, 5);
    }

    uint32_t word = 0;
    for (i = 0; i < 50; i ++) {
        const CullStatus *l = ecs_get(world, left, CullStatus);
        const CullStatus *r = ecs_get(world, right, CullStatus);
        if (l && r && l->visible == 3 && r->visible == 5) {
            break;
        }

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    test_int(ecs_get(world, left, CullStatus)->visible, 3);
    test_int(ecs_get(world, left, CullStatus)->frustum_culled, 5);
    test_int(ecs_get(world, right, CullStatus)->visible, 5);
    test_int(ecs_get(world, right, CullStatus)->frustum_culled, 3);

    ecs_fini(world);
}
