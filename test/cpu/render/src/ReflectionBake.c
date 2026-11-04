/**
 * @file ReflectionBake.c
 * @brief Tests for the reflection probe settings, the probe registry and its status.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

/** Get the status of the ReflectionBake of an entity.
 *
 * @param world The world
 * @param e The entity with ReflectionBake
 * @return The status
 */
static const ReflectionBakeStatus* status_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ReflectionBakeStatus *result = ecs_get(world, e, ReflectionBakeStatus);
    test_assert(result != NULL);

    return result;
}

/** Create a scene root with a ReflectionBake and a static floor.
 *
 * @param world The world
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, ReflectionBake);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {20, 1, 20});

    return scene;
}

/** Create a probe in a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param x Position along x
 * @param priority Priority of the probe
 * @return The probe entity
 */
static ecs_entity_t probe_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    int32_t priority)
{
    ecs_entity_t probe = scene ? ecs_new_w_pair(world, EcsChildOf, scene) : ecs_new(world);
    ecs_set(world, probe, Position, {x, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ecs_get_mut(world, probe, ReflectionProbe)->priority = priority;
    ecs_modified(world, probe, ReflectionProbe);

    return probe;
}

/** Find the slot of the resident probe captured at a position along x.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x of the capture point
 * @return The slot, -1 when no resident probe is there
 */
static int32_t slot_at(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x)
{
    int32_t i, count, result = -1;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    for (i = 0; i < count; i ++) {
        if (!probes[i].scene && probes[i].capture[0] == x) {
            result = (int32_t)probes[i].slot;
        }
    }

    return result;
}

void ReflectionBake_script_defaults(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n ReflectionBake: {}\n probe { ReflectionProbe: {} }\n}\n"
        "custom {\n Scene: {}\n ReflectionBake: {resolution: 256, budget_ms: 3, "
        "scene_probe: false, scene_probe_height: 4}\n"
        " probe { ReflectionProbe: {extent: {1, 2, 3}, blend: 0.5, priority: 2, "
        "projection: {4, 5, 6}, offset: {0, 1, 0}} }\n}\n", NULL) == 0);

    const ReflectionBake *bake = ecs_get(world, ecs_lookup(world, "scene"), ReflectionBake);
    test_assert(bake != NULL);
    test_int(bake->resolution, 128);
    test_flt(bake->budget_ms, 1);
    test_bool(bake->scene_probe, true);
    test_flt(bake->scene_probe_height, 2);

    const ReflectionProbe *probe = ecs_get(world, ecs_lookup(world, "scene.probe"),
        ReflectionProbe);
    test_assert(probe != NULL);
    test_flt(probe->extent[0], 5);
    test_flt(probe->extent[1], 5);
    test_flt(probe->extent[2], 5);
    test_flt(probe->blend, 1);
    test_int(probe->priority, 0);
    test_flt(probe->projection[0], 5);
    test_flt(probe->projection[1], 5);
    test_flt(probe->projection[2], 5);
    test_flt(probe->offset[0], 0);
    test_flt(probe->offset[1], 0);
    test_flt(probe->offset[2], 0);

    bake = ecs_get(world, ecs_lookup(world, "custom"), ReflectionBake);
    test_assert(bake != NULL);
    test_int(bake->resolution, 256);
    test_flt(bake->budget_ms, 3);
    test_bool(bake->scene_probe, false);
    test_flt(bake->scene_probe_height, 4);

    probe = ecs_get(world, ecs_lookup(world, "custom.probe"), ReflectionProbe);
    test_assert(probe != NULL);
    test_flt(probe->extent[0], 1);
    test_flt(probe->extent[1], 2);
    test_flt(probe->extent[2], 3);
    test_flt(probe->blend, 0.5f);
    test_int(probe->priority, 2);
    test_flt(probe->projection[0], 4);
    test_flt(probe->projection[1], 5);
    test_flt(probe->projection[2], 6);
    test_flt(probe->offset[1], 1);

    ecs_fini(world);
}

void ReflectionBake_c_defaults(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, ReflectionBake);
    const ReflectionBake *bake = ecs_get(world, scene, ReflectionBake);
    test_assert(bake != NULL);
    test_int(bake->resolution, 128);
    test_flt(bake->budget_ms, 1);
    test_bool(bake->scene_probe, true);
    test_flt(bake->scene_probe_height, 2);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ReflectionProbe);
    ecs_set(world, e, Position, {0, 0, 0});
    const ReflectionProbe *probe = ecs_get(world, e, ReflectionProbe);
    test_assert(probe != NULL);
    test_flt(probe->extent[0], 5);
    test_flt(probe->extent[1], 5);
    test_flt(probe->extent[2], 5);
    test_flt(probe->blend, 1);
    test_int(probe->priority, 0);
    test_flt(probe->projection[0], 5);
    test_flt(probe->projection[1], 5);
    test_flt(probe->projection[2], 5);
    test_flt(probe->offset[0], 0);
    test_flt(probe->offset[1], 0);
    test_flt(probe->offset[2], 0);

    ecs_fini(world);
}

void ReflectionBake_zero_is_literal(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "zero",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n ReflectionBake: {resolution: 0, budget_ms: 0, "
        "scene_probe: false, scene_probe_height: 0}\n"
        " probe { ReflectionProbe: {extent: {0, 0, 0}, blend: 0, "
        "projection: {0, 0, 0}} }\n}\n", NULL) == 0);

    const ReflectionBake *bake = ecs_get(world, ecs_lookup(world, "scene"), ReflectionBake);
    test_assert(bake != NULL);
    test_int(bake->resolution, 0);
    test_flt(bake->budget_ms, 0);
    test_bool(bake->scene_probe, false);
    test_flt(bake->scene_probe_height, 0);

    const ReflectionProbe *probe = ecs_get(world, ecs_lookup(world, "scene.probe"),
        ReflectionProbe);
    test_assert(probe != NULL);
    test_flt(probe->extent[0], 0);
    test_flt(probe->extent[2], 0);
    test_flt(probe->blend, 0);
    test_flt(probe->projection[0], 0);
    test_flt(probe->projection[1], 0);
    test_flt(probe->projection[2], 0);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, ReflectionBake, {0});
    bake = ecs_get(world, literal, ReflectionBake);
    test_int(bake->resolution, 0);
    test_flt(bake->budget_ms, 0);
    test_bool(bake->scene_probe, false);
    test_flt(bake->scene_probe_height, 0);

    ecs_set(world, literal, ReflectionProbe, {0});
    ecs_set(world, literal, Position, {0, 0, 0});
    probe = ecs_get(world, literal, ReflectionProbe);
    test_flt(probe->extent[0], 0);
    test_flt(probe->blend, 0);
    test_flt(probe->projection[1], 0);

    ecs_fini(world);
}

void ReflectionBake_status_follows_bake(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    test_assert(!ecs_has(world, scene, ReflectionBakeStatus));

    ecs_add(world, scene, ReflectionBake);
    test_assert(ecs_has(world, scene, ReflectionBakeStatus));
    const ReflectionBakeStatus *status = status_of(world, scene);
    test_int(status->probes, 0);
    test_int(status->resident, 0);
    test_int(status->captured, 0);
    test_bool(status->converged, false);
    test_assert(status->error == NULL);

    ecs_remove(world, scene, ReflectionBake);
    test_assert(!ecs_has(world, scene, ReflectionBakeStatus));

    ecs_set(world, scene, ReflectionBake, {.resolution = 64});
    test_assert(ecs_has(world, scene, ReflectionBakeStatus));

    test_assert(ecs_script_run(world, "status",
        "using box.light\n"
        "using box.view\n"
        "other {\n Scene: {}\n ReflectionBake: {}\n}\n", NULL) == 0);
    test_assert(ecs_has(world, ecs_lookup(world, "other"), ReflectionBakeStatus));

    ecs_delete(world, scene);
    ecs_fini(world);
}

void ReflectionBake_singleton_bake_has_status(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, ReflectionBake, {.resolution = 64, .scene_probe = true});
    test_assert(ecs_has(world, ecs_id(ReflectionBake), ReflectionBakeStatus));
    test_int(ecs_singleton_get(world, ReflectionBake)->resolution, 64);

    ecs_singleton_remove(world, ReflectionBake);
    test_assert(!ecs_has(world, ecs_id(ReflectionBake), ReflectionBakeStatus));

    ecs_fini(world);
}

void ReflectionBake_error_is_owned(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, ReflectionBake);
    ecs_set(world, a, ReflectionBakeStatus, {.error = ecs_os_strdup("first")});
    ecs_set(world, a, ReflectionBakeStatus, {.error = ecs_os_strdup("second")});
    test_str(status_of(world, a)->error, "second");

    ecs_entity_t c = ecs_clone(world, 0, a, true);
    ecs_delete(world, a);
    test_str(status_of(world, c)->error, "second");

    ecs_fini(world);
}

void ReflectionBake_invalid_resolution_sets_error(void) {
    // reflection_probes/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error == NULL);

    ecs_get_mut(world, scene, ReflectionBake)->resolution = 4;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error != NULL);
    test_bool(status_of(world, scene)->converged, false);

    ecs_get_mut(world, scene, ReflectionBake)->resolution = 8;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error == NULL);

    ecs_get_mut(world, scene, ReflectionBake)->resolution = 1024;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error != NULL);

    ecs_get_mut(world, scene, ReflectionBake)->resolution = 512;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error == NULL);

    ecs_set(world, scene, ReflectionBake, {0});
    ecs_progress(world, 0);
    test_assert(status_of(world, scene)->error != NULL);
    test_int(status_of(world, scene)->probes, 0);

    ecs_fini(world);
}

void ReflectionBake_scene_probe_counts(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    ecs_progress(world, 0);

    const ReflectionBakeStatus *status = status_of(world, scene);
    test_int(status->probes, 1);
    test_int(status->resident, 1);
    test_int(status->captured, 0);
    test_bool(status->converged, false);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 1);
    test_int(probes[0].scene, 1);
    test_flt(probes[0].capture[0], 0);
    test_flt(probes[0].capture[1], -0.5f + 2);
    test_flt(probes[0].capture[2], 0);
    test_flt(probes[0].projection[0], 0);

    ecs_get_mut(world, scene, ReflectionBake)->scene_probe_height = 5;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    probes = box_render_reflect_probes(world, scene, &count);
    test_int(count, 1);
    test_flt(probes[0].capture[1], -0.5f + 5);

    ecs_fini(world);
}

void ReflectionBake_scene_probe_disabled(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    probe_new(world, scene, 3, 0);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 2);

    ecs_get_mut(world, scene, ReflectionBake)->scene_probe = false;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 1);
    test_int(status_of(world, scene)->resident, 1);

    ecs_get_mut(world, scene, ReflectionBake)->scene_probe = true;
    ecs_modified(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 2);
    test_int(status_of(world, scene)->resident, 2);

    ecs_fini(world);
}

void ReflectionBake_singleton_scene_probe(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_add(world, ReflectionBake);
    ecs_entity_t probe = ecs_new(world);
    ecs_set(world, probe, Position, {1, 2, 3});
    ecs_add(world, probe, ReflectionProbe);
    ecs_progress(world, 0);

    const ReflectionBakeStatus *status = status_of(world, ecs_id(ReflectionBake));
    test_int(status->probes, 2);
    test_int(status->resident, 2);

    ecs_fini(world);
}

void ReflectionBake_probe_count_follows_adds_and_removes(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t a = probe_new(world, scene, 1, 0);
    ecs_entity_t b = probe_new(world, scene, 2, 0);
    probe_new(world, scene, 3, 0);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 4);
    test_int(status_of(world, scene)->resident, 4);

    ecs_delete(world, a);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 3);
    test_int(status_of(world, scene)->resident, 3);

    ecs_remove(world, b, ReflectionProbe);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 2);

    ecs_add(world, b, ReflectionProbe);
    probe_new(world, scene, 4, 0);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 4);
    test_int(status_of(world, scene)->resident, 4);

    ecs_fini(world);
}

void ReflectionBake_probe_outside_bake_scene_ignored(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, Scene);
    ecs_entity_t probe = probe_new(world, other, 1, 0);
    probe_new(world, 0, 2, 0);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 1);

    ecs_add_pair(world, probe, EcsChildOf, scene);
    ecs_progress(world, 0);
    test_int(status_of(world, scene)->probes, 2);

    ecs_remove(world, scene, ReflectionBake);
    ecs_progress(world, 0);
    int32_t count;
    test_assert(box_render_reflect_probes(world, scene, &count) == NULL);
    test_int(count, 0);

    ecs_fini(world);
}

void ReflectionBake_slots_are_stable(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t a = probe_new(world, scene, 1, 0);
    probe_new(world, scene, 2, 5);
    probe_new(world, scene, 3, -1);
    ecs_progress(world, 0);

    int32_t s1 = slot_at(world, scene, 1), s2 = slot_at(world, scene, 2);
    int32_t s3 = slot_at(world, scene, 3);
    test_assert(s1 >= 0 && s2 >= 0 && s3 >= 0);
    test_assert(s1 != s2 && s2 != s3 && s1 != s3);

    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 4);
    test_int(probes[0].priority, 5);
    test_int(probes[1].priority, 0);
    test_int(probes[2].priority, -1);
    test_int(probes[3].scene, 1);

    ecs_delete(world, a);
    probe_new(world, scene, 4, 9);
    probe_new(world, scene, 5, 0);
    ecs_progress(world, 0);
    test_int(slot_at(world, scene, 1), -1);
    test_int(slot_at(world, scene, 2), s2);
    test_int(slot_at(world, scene, 3), s3);
    test_assert(slot_at(world, scene, 4) >= 0);
    test_assert(slot_at(world, scene, 5) >= 0);
    test_assert(slot_at(world, scene, 4) != slot_at(world, scene, 5));
    test_int(status_of(world, scene)->probes, 5);

    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {0, -10, 0});
    ecs_set(world, wall, Box, {1, 1, 1});
    ecs_progress(world, 0);
    probes = box_render_reflect_probes(world, scene, &count);
    test_int(probes[count - 1].scene, 1);
    test_flt(probes[count - 1].capture[1], -10.5f + 2);
    test_int(slot_at(world, scene, 2), s2);
    test_int(slot_at(world, scene, 3), s3);

    ecs_fini(world);
}

void ReflectionBake_probes_beyond_capacity(void) {
    // reflection_probes/registry
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t i, capacity = BOX_RENDER_REFLECT_LAYERS / 6;
    ecs_entity_t scene = scene_new(world);
    for (i = 0; i < capacity; i ++) {
        probe_new(world, scene, (float)(i + 1), 0);
    }

    ecs_entity_t high = probe_new(world, scene, -1, 1);
    ecs_progress(world, 0);

    const ReflectionBakeStatus *status = status_of(world, scene);
    test_int(status->probes, capacity + 2);
    test_int(status->resident, capacity);

    int32_t count, scene_probes = 0;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, capacity);
    for (i = 0; i < count; i ++) {
        scene_probes += (int32_t)probes[i].scene;
    }

    test_int(scene_probes, 1);
    test_assert(slot_at(world, scene, -1) >= 0);

    ecs_delete(world, high);
    ecs_progress(world, 0);
    status = status_of(world, scene);
    test_int(status->probes, capacity + 1);
    test_int(status->resident, capacity);

    ecs_fini(world);
}
