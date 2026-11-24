/**
 * @file ReflectionFollow.c
 * @brief Tests for the scene probe of a ReflectionBake that follows the WorldFocus of a
 * streamed scene: it captures again after the focus moved far enough, ignores changes far from
 * it and keeps its capture over a shift of the world origin.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Distance the focus moves before the scene probe captures again in these tests */
#define FOLLOW_DISTANCE (10.0f)

/** Distance beyond which changes do not recapture the scene probe in these tests */
#define FOLLOW_RADIUS (30.0f)

/** Height of the scene probe above the focus in these tests */
#define FOLLOW_HEIGHT (2.0f)

/** Create a headless world that renders with the GPU.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create a scene whose scene probe follows the focus, with a static floor, a camera with a
 * view and a surface.
 *
 * @param world The world
 * @param blend ReflectionBake.follow_blend
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    float blend)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {
        .resolution = 16, .budget_ms = 1, .scene_probe = true,
        .scene_probe_height = FOLLOW_HEIGHT, .follow_focus = true,
        .follow_distance = FOLLOW_DISTANCE, .follow_radius = FOLLOW_RADIUS,
        .follow_blend = blend});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 8});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    return scene;
}

/** Create the focus of a scene at the origin.
 *
 * @param world The world
 * @param scene The scene
 * @return The focus entity
 */
static ecs_entity_t focus_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t focus = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, focus, Position, {0, 0, 0});
    ecs_add(world, focus, WorldFocus);

    return focus;
}

/** Run a frame with a fixed time step and wait for the backend.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0.01f);
    box_render_sync(world);
}

/** Run frames until the reflection probes of a scene converged.
 *
 * @param world The world
 * @param scene The scene
 * @param max Frames to run at most
 * @return Frames it took, max when they did not converge
 */
static int32_t converge(
    ecs_world_t *world,
    ecs_entity_t scene,
    int32_t max)
{
    int32_t i;
    for (i = 0; i < max; i ++) {
        frame(world);
        if (ecs_get(world, scene, ReflectionBakeStatus)->converged) {
            break;
        }
    }

    return i;
}

/** Get the scene probe of a scene as its reflection probe buffer holds it.
 *
 * @param world The world
 * @param scene The scene
 * @return The scene probe, NULL when it is not resident
 */
static const box_render_reflection_probe_t* scene_probe_of(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    int32_t i, count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    const box_render_reflection_probe_t *result = NULL;
    for (i = 0; i < count; i ++) {
        if (probes[i].scene) {
            result = &probes[i];
        }
    }

    return result;
}

/** Check the capture point of the scene probe of a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x Expected position along x
 * @param y Expected position along y
 * @param z Expected position along z
 */
static void capture_expect(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    float y,
    float z)
{
    const box_render_reflection_probe_t *probe = scene_probe_of(world, scene);
    test_assert(probe != NULL);
    test_flt(probe->capture[0], x);
    test_flt(probe->capture[1], y);
    test_flt(probe->capture[2], z);
}

void ReflectionFollow_captures_at_focus(void) {
    // reflection_probes/follow
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 0.1f);
    ecs_entity_t focus = focus_new(world, scene);
    ecs_set(world, focus, Position, {3, 1, -2});

    test_assert(converge(world, scene, 50) < 50);
    capture_expect(world, scene, 3, 1 + FOLLOW_HEIGHT, -2);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_int(status->probes, 1);
    test_int(status->captured, 1);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionFollow_recaptures_after_distance(void) {
    // reflection_probes/follow
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 0.1f);
    ecs_entity_t focus = focus_new(world, scene);
    test_assert(converge(world, scene, 50) < 50);
    capture_expect(world, scene, 0, FOLLOW_HEIGHT, 0);

    int32_t i;
    for (i = 1; i <= 9; i ++) {
        ecs_set(world, focus, Position, {(float)i, 0, 0});
        frame(world);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
        capture_expect(world, scene, 0, FOLLOW_HEIGHT, 0);
    }

    ecs_set(world, focus, Position, {FOLLOW_DISTANCE + 1, 0, 0});
    frame(world);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);

    test_assert(converge(world, scene, 50) < 50);
    capture_expect(world, scene, FOLLOW_DISTANCE + 1, FOLLOW_HEIGHT, 0);

    /* The focus moves back by less than the distance from the new place: no capture */
    ecs_set(world, focus, Position, {FOLLOW_DISTANCE - 5, 0, 0});
    frame(world);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    capture_expect(world, scene, FOLLOW_DISTANCE + 1, FOLLOW_HEIGHT, 0);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionFollow_blends_previous_capture(void) {
    // reflection_probes/follow
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 0.1f);
    ecs_entity_t focus = focus_new(world, scene);
    test_assert(converge(world, scene, 50) < 50);
    const box_render_reflection_probe_t *probe = scene_probe_of(world, scene);
    test_assert(probe != NULL);
    uint32_t old_slot = probe->slot;
    test_flt(probe->fade, 0);

    ecs_set(world, focus, Position, {FOLLOW_DISTANCE + 1, 0, 0});
    int32_t i;
    bool faded = false;
    float fade = 1.0f;
    for (i = 0; i < 100; i ++) {
        frame(world);
        probe = scene_probe_of(world, scene);
        test_assert(probe != NULL);
        test_bool(probe->ready, true);
        if (probe->fade > 0) {
            test_assert(probe->slot != old_slot);
            test_int(probe->previous, old_slot);
            test_assert(probe->fade <= fade);
            fade = probe->fade;
            faded = true;
        } else if (faded) {
            break;
        } else {
            /* Until the new capture is ready the probe shows the old one */
            test_int(probe->slot, old_slot);
        }
    }

    test_assert(faded);
    test_assert(i < 100);
    probe = scene_probe_of(world, scene);
    test_flt(probe->fade, 0);
    test_assert(probe->slot != old_slot);
    capture_expect(world, scene, FOLLOW_DISTANCE + 1, FOLLOW_HEIGHT, 0);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionFollow_far_shapes_do_not_queue(void) {
    // reflection_probes/follow
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 0.1f);
    focus_new(world, scene);
    test_assert(converge(world, scene, 50) < 50);

    ecs_entity_t shapes[32];
    int32_t i, round;
    for (round = 0; round < 3; round ++) {
        for (i = 0; i < 32; i ++) {
            shapes[i] = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, shapes[i], Position, {
                FOLLOW_RADIUS * 4 + (float)(i % 8) * 4, 1, (float)(i / 8) * 4 - 8});
            ecs_set(world, shapes[i], Box, {1, 1, 1});
        }

        frame(world);
        test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);

        for (i = 0; i < 32; i ++) {
            ecs_delete(world, shapes[i]);
        }

        frame(world);
        test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    }

    capture_expect(world, scene, 0, FOLLOW_HEIGHT, 0);

    /* A shape near the probe queues it */
    ecs_entity_t near = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, near, Position, {3, 1, 0});
    ecs_set(world, near, Box, {1, 1, 1});
    frame(world);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);

    ecs_fini(world);
}

void ReflectionFollow_origin_shift_does_not_recapture(void) {
    // reflection_probes/follow
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 0.1f);
    ecs_entity_t focus = focus_new(world, scene);
    ecs_set(world, focus, Position, {4, 0, 0});
    test_assert(converge(world, scene, 50) < 50);
    capture_expect(world, scene, 4, FOLLOW_HEIGHT, 0);

    box_world_origin_shift(world, scene, (double[3]){ 256, 0, -512 });
    frame(world);
    test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    capture_expect(world, scene, 4 - 256, FOLLOW_HEIGHT, 512);

    int32_t i;
    for (i = 0; i < 5; i ++) {
        frame(world);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    }

    /* The probe follows the focus from its shifted place */
    const Position *p = ecs_get(world, focus, Position);
    test_flt(p->x, 4 - 256);
    ecs_set(world, focus, Position, {p->x + FOLLOW_DISTANCE + 1, 0, 512});
    frame(world);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);
    test_assert(converge(world, scene, 50) < 50);
    capture_expect(world, scene, 4 - 256 + FOLLOW_DISTANCE + 1, FOLLOW_HEIGHT, 512);

    ecs_fini(world);
}

/** Render the next frame of a world into an image file of its temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @param w Receives the width
 * @param h Receives the height
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* frame_image(
    ecs_world_t *world,
    const char *file,
    int *w,
    int *h)
{
    ecs_entity_t e = ecs_lookup(world, "follow_capture");
    if (!e) {
        e = ecs_entity(world, { .name = "follow_capture" });
    }

    ecs_set(world, e, Surface, {.width = 64, .height = 48,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    frame(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int n = 0;
    unsigned char *result = stbi_load(path, w, h, &n, 4);
    test_assert(result != NULL);
    ecs_os_free(path);

    return result;
}

void ReflectionFollow_mover_probe_does_not_recapture(void) {
    // reflection_probes/follow
    /* A probe a ship carries over a mirror floor captures the cabin once and keeps that capture
     * while the ship sails and turns, which the render origin follows: no capture is reported
     * again and the mirror shows the same image at the dock and after the turn. */
    ecs_world_t *world = world_new();
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = false});
    ecs_entity_t ship = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, ship, DynamicTransform);
    ecs_set(world, ship, Position, {300, 0, 0});
    ecs_set(world, ship, Rotation, {0, 0, 0});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 8});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 0.05f});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, wall, Position, {0, 2, -4});
    ecs_set(world, wall, Box, {8, 4, 0.2f});
    ecs_set(world, wall, Rgb, {220, 60, 40});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, light, Position, {0, 3, 0});
    ecs_set(world, light, PointLight, {.intensity = 8, .range = 10});
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, probe, Position, {0, 1.5f, 0});
    ecs_set(world, probe, ReflectionProbe, {.extent = {4, 4, 4}, .blend = 0.5f,
        .projection = {4, 4, 4}});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, camera, Position, {0, 1.5f, 3});
    ecs_set(world, camera, Rotation, {-0.5f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.reflections = true});
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true,
        .render_follow = ship});
    ecs_entity_t surface = ecs_entity(world, { .name = "follow_capture" });
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    test_assert(converge(world, scene, 100) < 100);
    int32_t captures = ecs_get(world, scene, ReflectionBakeStatus)->captures;
    test_assert(captures > 0);
    int w = 0, h = 0, i;
    unsigned char *dock = frame_image(world, "mover_dock.png", &w, &h);
    unsigned char *px = NULL;
    for (i = 1; i <= 24; i ++) {
        float t = (float)i / 24.0f;
        ecs_set(world, ship, Position, {300 + 500 * t, 0, 120 * t});
        ecs_set(world, ship, Rotation, {0, GLM_PIf * 0.5f * t, 0});
        frame(world);
        test_int(ecs_get(world, scene, ReflectionBakeStatus)->captures, captures);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    }

    px = frame_image(world, "mover_turned.png", &w, &h);
    int32_t worst = 0, differ = 0;
    for (i = 0; i < w * h * 4; i ++) {
        int32_t d = abs((int)dock[i] - (int)px[i]);
        worst = d > worst ? d : worst;
        differ += d > 4;
    }

    printf("mirror after sailing: worst channel difference %d, %d channels above 4\n", worst,
        differ);
    test_assert(differ <= w * h * 3 / 100);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captures, captures);
    stbi_image_free(dock);
    stbi_image_free(px);
    ecs_fini(world);
}
