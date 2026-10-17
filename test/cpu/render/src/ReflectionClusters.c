/**
 * @file ReflectionClusters.c
 * @brief Tests for the blend order of reflection probes and the probes the clusters of a view
 * list.
 *
 * The cluster lists themselves are written on the GPU; these tests check what the views of a
 * packet carry for the cluster pass: the probe buffer of their scene in blend order and how many
 * of its probes the clusters list.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Create a scene root with a ReflectionBake and a static floor.
 *
 * @param world The world
 * @param scene_probe Whether the scene has its scene probe
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    bool scene_probe)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, ReflectionBake);
    ecs_get_mut(world, scene, ReflectionBake)->scene_probe = scene_probe;
    ecs_modified(world, scene, ReflectionBake);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {20, 1, 20});

    return scene;
}

/** Create a probe in a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x, which identifies the probe
 * @param priority Priority of the probe
 * @param extent Half size of its influence box on every axis
 * @return The probe entity
 */
static ecs_entity_t probe_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    int32_t priority,
    float extent)
{
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {x, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ReflectionProbe *p = ecs_get_mut(world, probe, ReflectionProbe);
    p->priority = priority;
    p->extent[0] = extent;
    p->extent[1] = extent;
    p->extent[2] = extent;
    ecs_modified(world, probe, ReflectionProbe);

    return probe;
}

/** Create a lit view that samples reflection probes, whose camera is in a scene.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .lighting = {.enabled = true, .reflections = true}});

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

void ReflectionClusters_blend_order_priority_then_size(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, 1, 0, 5);
    probe_new(world, scene, 2, 0, 1);
    probe_new(world, scene, 3, 1, 8);
    probe_new(world, scene, 4, 0, 3);
    probe_new(world, scene, 5, -1, 1);
    ecs_progress(world, 0);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 6);
    test_flt(probes[0].capture[0], 3);
    test_flt(probes[1].capture[0], 2);
    test_flt(probes[2].capture[0], 4);
    test_flt(probes[3].capture[0], 1);
    test_flt(probes[4].capture[0], 5);
    test_int(probes[5].scene, 1);

    ecs_fini(world);
}

void ReflectionClusters_blend_order_size_tie_by_entity(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, 1, 0, 2);
    ecs_entity_t b = probe_new(world, scene, 2, 0, 2);
    test_assert(a < b);
    ecs_progress(world, 0);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 3);
    test_flt(probes[0].capture[0], 1);
    test_flt(probes[1].capture[0], 2);

    ReflectionProbe *p = ecs_get_mut(world, a, ReflectionProbe);
    p->extent[1] = 3;
    ecs_modified(world, a, ReflectionProbe);
    ecs_progress(world, 0);
    probes = box_render_reflect_probes(world, scene, &count);
    test_int(count, 3);
    test_flt(probes[0].capture[0], 2);
    test_flt(probes[1].capture[0], 1);

    ecs_fini(world);
}

void ReflectionClusters_view_lists_probes_not_scene_probe(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, 1, 0, 5);
    probe_new(world, scene, 2, 3, 1);
    view_new(world, scene);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->reflections != 0);
    test_int(v->reflection_count, 3);
    test_int(v->reflection_listed, 2);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 3);
    test_int(probes[0].scene, 0);
    test_int(probes[1].scene, 0);
    test_int(probes[2].scene, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionClusters_view_lists_without_scene_probe(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world, false);
    probe_new(world, scene, 1, 0, 5);
    probe_new(world, scene, 2, 0, 1);
    view_new(world, scene);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->reflections != 0);
    test_int(v->reflection_count, 2);
    test_int(v->reflection_listed, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionClusters_removed_probe_leaves_lists(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, 1, 0, 1);
    probe_new(world, scene, 2, 0, 5);
    view_new(world, scene);

    const box_render_view_t *v = frame_view(world, null);
    test_int(v->reflection_listed, 2);

    ecs_delete(world, a);
    v = frame_view(world, null);
    test_int(v->reflection_count, 2);
    test_int(v->reflection_listed, 1);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 2);
    test_flt(probes[0].capture[0], 2);
    test_int(probes[1].scene, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionClusters_scene_without_probes_lists_none(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = scene_new(world, true);
    view_new(world, scene);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->reflections != 0);
    test_int(v->reflection_count, 1);
    test_int(v->reflection_listed, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ReflectionClusters_scene_without_bake_lists_none(void) {
    // reflection_probes/clusters
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    view_new(world, scene);

    const box_render_view_t *v = frame_view(world, null);
    test_int(v->reflections, 0);
    test_int(v->reflection_count, 0);
    test_int(v->reflection_listed, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
