/**
 * @file ReflectionShading.c
 * @brief Tests for the views that sample the reflection probes of their scene.
 *
 * The probes are blended on the GPU; these tests check the LightSettings.reflections flag and
 * what the views of a packet carry for the forward pass: the probe buffer of their scene, only
 * for lit views that sample reflections, and which probes hold a capture to sample.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Create a scene root with a ReflectionBake, a probe and a static floor.
 *
 * @param world The world
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.baked = true, .reflections = true});
    ecs_add(world, scene, ReflectionBake);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {20, 1, 20});
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);

    return scene;
}

/** Create a view whose camera is in a scene.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @param lighting The light settings of the view
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    LightSettings lighting)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting = lighting});

    return view;
}

/** Run a frame and get the only view of the packet the backend received.
 *
 * @param world The world
 * @param null The backend
 * @return The view
 */
static const box_render_view_t* frame_view(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.views), 1);

    return ecs_vec_first(&null->last.views);
}

void ReflectionShading_view_reflections_defaults(void) {
    // reflection_probes/shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "view",
        "using box.view\n"
        "view { View: {} }\n"
        "off { View: {lighting: {enabled: true, reflections: false}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.reflections, true);
    test_bool(view->lighting.baked, true);
    view = ecs_get(world, ecs_lookup(world, "off"), View);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.reflections, false);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    test_bool(ecs_get(world, literal, View)->lighting.reflections, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, View);
    test_bool(ecs_get(world, added, View)->lighting.reflections, true);

    ecs_fini(world);
}

void ReflectionShading_view_without_reflections_lists_none(void) {
    // reflection_probes/shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world);
    view_new(world, scene, (LightSettings){.enabled = true, .baked = true});

    const box_render_view_t *v = frame_view(world, null);
    test_int(v->reflections, 0);
    test_int(v->reflection_count, 0);
    test_int(v->reflection_listed, 0);

    int32_t count = 0;
    test_assert(box_render_reflect_probes(world, scene, &count) != NULL);
    test_int(count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionShading_view_with_reflections_lists_probes(void) {
    // reflection_probes/shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t view = view_new(world, scene,
        (LightSettings){.enabled = true, .reflections = true});

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->reflections != 0);
    test_int(v->reflection_count, 2);
    test_int(v->reflection_listed, 1);

    ecs_get_mut(world, view, View)->lighting.reflections = false;
    ecs_modified(world, view, View);
    v = frame_view(world, null);
    test_int(v->reflections, 0);
    test_int(v->reflection_count, 0);
    test_int(v->reflection_listed, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionShading_unlit_view_lists_none(void) {
    // reflection_probes/shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world);
    view_new(world, scene, (LightSettings){.reflections = true});

    const box_render_view_t *v = frame_view(world, null);
    test_int(v->reflections, 0);
    test_int(v->reflection_count, 0);
    test_int(v->reflection_listed, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionShading_probes_wait_for_capture(void) {
    // reflection_probes/shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world);
    view_new(world, scene, (LightSettings){.enabled = true, .reflections = true});

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->reflections != 0);
    int32_t i, count = 0;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 2);
    for (i = 0; i < count; i ++) {
        test_int(probes[i].ready, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}
