/**
 * @file ReflectionInvalidation.c
 * @brief Tests for the changes that queue reflection probes for a new capture.
 *
 * Without the GPU backend no probe is ever captured, so each test first marks the probes of its
 * scene captured with box_render_reflect_settle and then checks which probes a change queued.
 */

#include <box.h>
#include <bake_test.h>
#include "bake.h"

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

/** Create a scene root with a ReflectionBake and a lit view whose camera is at x = 40.
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
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {40, 1, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});

    return scene;
}

/** Create a probe in a scene whose influence and projection boxes have the same size.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x
 * @param extent Half size of both boxes on every axis
 * @return The probe entity
 */
static ecs_entity_t probe_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    float extent)
{
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {x, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ReflectionProbe *p = ecs_get_mut(world, probe, ReflectionProbe);
    glm_vec3_fill(p->extent, extent);
    glm_vec3_fill(p->projection, extent);
    ecs_modified(world, probe, ReflectionProbe);

    return probe;
}

/** Create a unit box shape.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param x Position along x
 * @param z Position along z
 * @return The shape
 */
static ecs_entity_t shape_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float z)
{
    ecs_entity_t shape = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, shape, Position, {x, 1, z});
    ecs_set(world, shape, Box, {1, 1, 1});

    return shape;
}

/** Create a point light.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x
 * @param z Position along z
 * @param range Range of the light
 * @return The light
 */
static ecs_entity_t light_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    float z,
    float range)
{
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, Position, {x, 1, z});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = range});

    return light;
}

/** Count the probes of a scene that wait for a capture.
 *
 * @param world The world
 * @param scene The scene
 * @return The probes
 */
static int32_t queued_count(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    return box_render_reflect_queue(world, scene, NULL, 0);
}

/** Check whether a probe of a scene waits for a capture.
 *
 * @param world The world
 * @param scene The scene
 * @param probe Entity of the probe, 0 for the scene probe
 * @return Whether it waits
 */
static bool is_queued(
    ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t probe)
{
    ecs_entity_t queue[64];
    int32_t i, count = box_render_reflect_queue(world, scene, queue, 64);
    bool result = false;
    test_assert(count <= 64);
    for (i = 0; i < count; i ++) {
        result |= queue[i] == probe;
    }

    return result;
}

/** Mark the probes of a scene captured and run a frame in which nothing is queued.
 *
 * @param world The world
 * @param scene The scene
 */
static void settle(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    box_render_reflect_settle(world, scene);
    ecs_progress(world, 0);
    test_int(queued_count(world, scene), 0);
    test_bool(status_of(world, scene)->converged, true);
    test_int(status_of(world, scene)->captured, status_of(world, scene)->resident);
}

/** Run a frame and check the number of queued probes and the status they give.
 *
 * @param world The world
 * @param scene The scene
 * @param count Probes expected to be queued
 */
static void frame_expect(
    ecs_world_t *world,
    ecs_entity_t scene,
    int32_t count)
{
    ecs_progress(world, 0);
    const ReflectionBakeStatus *status = status_of(world, scene);
    test_int(queued_count(world, scene), count);
    test_bool(status->converged, count == 0);
    test_int(status->captured, status->resident - count);
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

/** Set a Position.
 *
 * @param world The world
 * @param e The entity
 * @param x Position along x
 * @param z Position along z
 */
static void move(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float z)
{
    ecs_set(world, e, Position, {x, 1, z});
}

void ReflectionInvalidation_settled_scene_queues_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    shape_new(world, scene, 0, 30);
    light_new(world, scene, 0, 30, 3);
    ecs_progress(world, 0);
    test_int(queued_count(world, scene), 3);
    test_bool(status_of(world, scene)->converged, false);
    test_int(status_of(world, scene)->captured, 0);

    settle(world, scene);
    frame_expect(world, scene, 0);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_static_move_queues_reached_probes(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    ecs_entity_t b = probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, 0, 30);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, -10, 2.4f);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, a), true);
    test_bool(is_queued(world, scene, b), false);
    settle(world, scene);

    move(world, shape, 10, -2.4f);
    frame_expect(world, scene, 3);
    test_bool(is_queued(world, scene, a), true);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    move(world, shape, 10, -2.6f);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    move(world, shape, 0, 30);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, 0), true);

    ecs_fini(world);
}

void ReflectionInvalidation_static_look_change_queues_reached_probes(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_set(world, shape, Rgb, {255, 0, 0});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_set(world, shape, Opacity, {0.5f});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_set(world, shape, Box, {2, 1, 1});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_static_add_and_delete_queue_reached_probes(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    ecs_entity_t b = probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_entity_t shape = shape_new(world, scene, 11, 0);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    ecs_delete(world, shape);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, b), true);

    ecs_fini(world);
}

void ReflectionInvalidation_shape_reaches_rotated_probe_box(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t turned = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, turned, Position, {-10, 1, 0});
    ecs_set(world, turned, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_set(world, turned, ReflectionProbe, {
        .extent = {4, 1, 0.5f}, .blend = 1, .projection = {4, 1, 0.5f}});
    ecs_entity_t straight = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, straight, Position, {10, 1, 0});
    ecs_set(world, straight, ReflectionProbe, {
        .extent = {4, 1, 0.5f}, .blend = 1, .projection = {4, 1, 0.5f}});
    ecs_entity_t near_turned = shape_new(world, scene, -10, 30);
    ecs_entity_t near_straight = shape_new(world, scene, 10, 30);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, near_turned, -10, 4);
    move(world, near_straight, 10, 4);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, turned), true);
    settle(world, scene);

    move(world, near_straight, 6, 0);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, straight), true);

    ecs_fini(world);
}

void ReflectionInvalidation_shape_reaches_projection_box(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_set(world, probe, ReflectionProbe, {
        .extent = {1, 1, 1}, .blend = 1, .projection = {6, 6, 6}});
    ecs_entity_t other = probe_new(world, scene, 20, 1);
    ecs_entity_t shape = shape_new(world, scene, 0, 30);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, 0, 6);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, probe), true);
    test_bool(is_queued(world, scene, other), false);
    settle(world, scene);

    move(world, shape, 0, 7);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, probe), true);
    settle(world, scene);

    move(world, shape, 0, 8);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_flag_change_queues_static_shape(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_add_id(world, shape, NoBake);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_remove_id(world, shape, NoBake);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_add_id(world, shape, DynamicTransform);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_dynamic_shape_queues_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_add_id(world, shape, DynamicTransform);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, -10, 1);
    frame_expect(world, scene, 0);

    ecs_set(world, shape, Rgb, {0, 255, 0});
    frame_expect(world, scene, 0);

    ecs_delete(world, shape);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_manual_shape_queues_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_add_id(world, shape, ManualTransform);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, -10, 1);
    frame_expect(world, scene, 0);

    ecs_set(world, shape, Rgb, {0, 255, 0});
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_nobake_shape_queues_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {40, 1, 40});
    probe_new(world, scene, -10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_add_id(world, shape, NoBake);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, -10, 1);
    frame_expect(world, scene, 0);

    ecs_set(world, shape, Rgb, {0, 255, 0});
    frame_expect(world, scene, 0);

    ecs_delete(world, shape);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_point_light_change_queues_old_and_new_range(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    ecs_entity_t b = probe_new(world, scene, 10, 2);
    ecs_entity_t light = light_new(world, scene, -10, 4.5f, 3);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_set(world, light, PointLight, {.intensity = 2, .range = 3});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, 0), true);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    move(world, light, 10, -4.5f);
    frame_expect(world, scene, 3);
    test_bool(is_queued(world, scene, a), true);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    ecs_set(world, light, PointLight, {.intensity = 2, .range = 2});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    ecs_set(world, light, PointLight, {.intensity = 3, .range = 2});
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, 0), true);

    ecs_fini(world);
}

void ReflectionInvalidation_light_added_and_removed_queue_range(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    ecs_entity_t b = probe_new(world, scene, 10, 2);
    light_new(world, scene, 0, 30, 3);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_entity_t light = light_new(world, scene, 10, 3, 3);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    ecs_delete(world, light);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, b), true);
    settle(world, scene);

    light = light_new(world, scene, -10, 3, 3);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_remove(world, light, PointLight);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_spot_light_change_queues_range(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t spot = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, spot, Position, {-10, 1, 4});
    ecs_set(world, spot, SpotLight, {
        .intensity = 1, .range = 3, .inner_angle = 20, .outer_angle = 30});
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_set(world, spot, SpotLight, {
        .intensity = 1, .range = 3, .inner_angle = 10, .outer_angle = 30});
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_directional_light_queues_all(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    light_new(world, scene, 0, 30, 3);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    frame_expect(world, scene, 3);
    settle(world, scene);

    ecs_set(world, sun, Rotation, {0.5f, 0, 0});
    frame_expect(world, scene, 3);
    settle(world, scene);

    ecs_set(world, sun, DirectionalLight, {.intensity = 2});
    frame_expect(world, scene, 3);
    settle(world, scene);

    ecs_delete(world, sun);
    frame_expect(world, scene, 3);

    ecs_fini(world);
}

void ReflectionInvalidation_default_sun_queues_all(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_entity_t light = light_new(world, scene, 0, 30, 3);
    frame_expect(world, scene, 2);
    settle(world, scene);

    ecs_delete(world, light);
    frame_expect(world, scene, 2);

    ecs_fini(world);
}

void ReflectionInvalidation_ambient_change_queues_all(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_set(world, scene, AmbientLight, {
        .sky = {100, 100, 100}, .ground = {50, 50, 50}, .intensity = 1});
    frame_expect(world, scene, 3);
    settle(world, scene);

    ecs_set(world, scene, AmbientLight, {
        .sky = {100, 100, 100}, .ground = {50, 50, 50}, .intensity = 2});
    frame_expect(world, scene, 3);
    settle(world, scene);

    ecs_remove(world, scene, AmbientLight);
    frame_expect(world, scene, 3);

    ecs_fini(world);
}

void ReflectionInvalidation_light_bake_switch_queues_all_once(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_add(world, scene, LightBake);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {20, 1, 20});
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
    test_assert(v != NULL);
    test_assert(v->handle != 0);
    v->switches += 3;
    frame_expect(world, scene, 3);
    settle(world, scene);
    frame_expect(world, scene, 0);

    v = box_bake_find(box_bake_state(world), scene);
    v->switches ++;
    frame_expect(world, scene, 3);

    ecs_fini(world);
}

void ReflectionInvalidation_light_bake_added_queues_all(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {20, 1, 20});
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_add(world, scene, LightBake);
    frame_expect(world, scene, 3);
    settle(world, scene);
    frame_expect(world, scene, 0);

    ecs_remove(world, scene, LightBake);
    frame_expect(world, scene, 3);

    ecs_fini(world);
}

void ReflectionInvalidation_probe_move_queues_probe(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, a, -12, 0);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_set(world, a, Rotation, {0, 1, 0});
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_probe_settings_queue_probe(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_get_mut(world, a, ReflectionProbe)->extent[0] = 3;
    ecs_modified(world, a, ReflectionProbe);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_get_mut(world, a, ReflectionProbe)->projection[1] = 4;
    ecs_modified(world, a, ReflectionProbe);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);
    settle(world, scene);

    ecs_get_mut(world, a, ReflectionProbe)->offset[1] = 1;
    ecs_modified(world, a, ReflectionProbe);
    frame_expect(world, scene, 1);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_priority_and_blend_only_upload(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_get_mut(world, a, ReflectionProbe)->priority = 3;
    ecs_modified(world, a, ReflectionProbe);
    frame_expect(world, scene, 0);
    int32_t count;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 3);
    test_flt(probes[0].capture[0], -10);
    test_int(probes[0].priority, 3);

    ecs_get_mut(world, a, ReflectionProbe)->blend = 0.25f;
    ecs_modified(world, a, ReflectionProbe);
    frame_expect(world, scene, 0);
    probes = box_render_reflect_probes(world, scene, &count);
    test_flt(probes[0].blend, 0.25f);

    ecs_fini(world);
}

void ReflectionInvalidation_resolution_change_recaptures_all(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_progress(world, 0);
    settle(world, scene);

    ecs_get_mut(world, scene, ReflectionBake)->resolution = 64;
    ecs_modified(world, scene, ReflectionBake);
    frame_expect(world, scene, 3);
    test_int(status_of(world, scene)->captured, 0);
    settle(world, scene);

    ecs_get_mut(world, scene, ReflectionBake)->budget_ms = 2;
    ecs_modified(world, scene, ReflectionBake);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_queued_probe_keeps_slot(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, 0, 30);
    ecs_progress(world, 0);
    settle(world, scene);
    int32_t slot = slot_at(world, scene, -10);
    uint32_t texture = box_render_reflect_texture(world, scene);
    test_assert(slot >= 0);
    test_assert(texture != 0);

    move(world, shape, -10, 2);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, a), true);
    test_int(slot_at(world, scene, -10), slot);
    test_int(box_render_reflect_texture(world, scene), texture);
    test_int(status_of(world, scene)->resident, 3);

    int32_t count;
    box_render_reflect_probes(world, scene, &count);
    test_int(count, 3);

    ecs_fini(world);
}

void ReflectionInvalidation_queue_order_scene_probe_then_distance(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t far = probe_new(world, scene, -10, 2);
    ecs_entity_t near = probe_new(world, scene, 10, 2);
    ecs_entity_t middle = probe_new(world, scene, 0, 2);
    ecs_progress(world, 0);

    ecs_entity_t queue[4];
    test_int(box_render_reflect_queue(world, scene, queue, 4), 4);
    test_int(queue[0], 0);
    test_int(queue[1], near);
    test_int(queue[2], middle);
    test_int(queue[3], far);
    settle(world, scene);

    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    frame_expect(world, scene, 4);
    test_int(box_render_reflect_queue(world, scene, queue, 2), 4);
    test_int(queue[0], 0);
    test_int(queue[1], near);

    ecs_fini(world);
}

void ReflectionInvalidation_queued_again_not_duplicated(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t a = probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_entity_t light = light_new(world, scene, -10, 4, 3);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, -10, 2.2f);
    ecs_set(world, shape, Rgb, {0, 0, 255});
    ecs_set(world, light, PointLight, {.intensity = 2, .range = 3});
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, a), true);

    move(world, shape, -10, 2.4f);
    move(world, a, -10.5f, 0);
    frame_expect(world, scene, 2);
    test_bool(is_queued(world, scene, a), true);

    ecs_fini(world);
}

void ReflectionInvalidation_unrelated_changes_queue_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, false);
    probe_new(world, scene, -10, 2);
    probe_new(world, scene, 10, 2);
    ecs_entity_t shape = shape_new(world, scene, 0, 30);
    ecs_entity_t light = light_new(world, scene, 0, 30, 3);
    ecs_entity_t empty = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, empty, Position, {0, 1, 0});
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, Scene);
    ecs_entity_t other_shape = shape_new(world, other, 0, 30);
    ecs_entity_t other_light = light_new(world, other, 0, 30, 3);
    ecs_progress(world, 0);
    settle(world, scene);

    move(world, shape, 0, 40);
    frame_expect(world, scene, 0);

    ecs_set(world, light, PointLight, {.intensity = 3, .range = 3});
    move(world, light, 0, 35);
    frame_expect(world, scene, 0);

    move(world, empty, -10, 0);
    ecs_set(world, empty, Rgb, {1, 2, 3});
    frame_expect(world, scene, 0);

    move(world, other_shape, -10, 0);
    move(world, other_light, 10, 0);
    ecs_set(world, other, AmbientLight, {.sky = {1, 2, 3}, .intensity = 1});
    shape_new(world, other, 10, 0);
    frame_expect(world, scene, 0);

    ecs_delete(world, other_shape);
    frame_expect(world, scene, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_scene_without_bake_records_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    probe_new(world, scene, -10, 2);
    ecs_entity_t shape = shape_new(world, scene, -10, 2);
    ecs_progress(world, 0);

    box_render_bake_t *b = box_bake_state(world);
    test_assert(b != NULL);
    ecs_set(world, shape, Rgb, {255, 0, 0});
    test_int(ecs_vec_count(&b->changes), 0);
    move(world, shape, -10, 1);
    ecs_progress(world, 0);
    test_int(ecs_vec_count(&b->regions), 0);
    test_int(queued_count(world, scene), 0);
    test_assert(!ecs_has(world, scene, ReflectionBakeStatus));

    ecs_entity_t baked = scene_new(world, true);
    ecs_entity_t a = probe_new(world, baked, -10, 2);
    ecs_progress(world, 0);
    settle(world, baked);

    move(world, shape, -10, 0);
    ecs_set(world, shape, Rgb, {0, 255, 0});
    frame_expect(world, baked, 0);
    test_bool(is_queued(world, baked, a), false);
    test_int(queued_count(world, scene), 0);

    ecs_fini(world);
}

void ReflectionInvalidation_quit_records_nothing(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, true);
    probe_new(world, scene, -10, 2);
    ecs_entity_t a = shape_new(world, scene, -10, 2);
    ecs_entity_t b = shape_new(world, scene, -10, 0);
    ecs_entity_t mover = shape_new(world, scene, -8, 0);
    ecs_add_id(world, mover, DynamicTransform);
    ecs_progress(world, 0);
    settle(world, scene);

    box_render_bake_t *state = box_bake_state(world);
    test_assert(state != NULL);
    ecs_set(world, a, Rgb, {255, 0, 0});
    test_int(ecs_vec_count(&state->changes), 1);
    ecs_progress(world, 0);
    test_int(ecs_vec_count(&state->changes), 0);

    /* The world deletes its roots once it quits, before it is fini, while the spatial index
     * cannot be queried: the changes of its shapes are not recorded */
    ecs_quit(world);
    ecs_set(world, a, Rgb, {0, 255, 0});
    ecs_delete(world, b);
    ecs_set(world, scene, Tint, {0, 0, 255});
    test_int(ecs_vec_count(&state->changes), 0);

    ecs_fini(world);
}
