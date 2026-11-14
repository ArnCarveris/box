/**
 * @file PlanetSurfaceLight.c
 * @brief Tests for the sunlight of a PlanetAtmosphere on its meshes: which mesh shapes bind to
 * which planet, through the planet light slot of their batch key and their shading, the planet
 * lights of the packets the null backend receives and the scene_planet_light override.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"

/** Check that two values are within a tolerance of each other.
 *
 * @param a The first value
 * @param b The second value
 * @param eps The tolerance
 */
static void test_near(
    float a,
    float b,
    float eps)
{
    if (fabsf(a - b) > eps) {
        printf("%f != %f\n", (double)a, (double)b);
    }

    test_assert(fabsf(a - b) <= eps);
}

/** Create a world with Box and the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Create a scene root with a view looking at the origin from +Z and a light.
 *
 * @param world The world
 * @param planet_light Scene.planet_light
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    bool planet_light)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.planet_light = planet_light});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, DirectionalLight, {.intensity = 2});
    ecs_set(world, light, Rgb, {255, 255, 255});
    ecs_set(world, light, Rotation, {glm_rad(30.0f), 0, 0});

    return scene;
}

/** Create a planet with the default PlanetAtmosphere and a sphere mesh of its radius.
 *
 * @param world The world
 * @param parent The parent, a scene or another entity
 * @param x Local X of the center
 * @return The planet
 */
static ecs_entity_t planet_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t planet = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, planet, Position, {x, 0, 0});
    ecs_set(world, planet, Sphere, {.radius = 1, .segments = 16});
    ecs_add(world, planet, PlanetAtmosphere);

    return planet;
}

/** Create a box mesh shape.
 *
 * @param world The world
 * @param parent The parent
 * @param x Local X of the box
 * @return The shape
 */
static ecs_entity_t shape_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, shape, Position, {x, 0, 0});
    ecs_set(world, shape, Box, {0.1f, 0.1f, 0.1f});

    return shape;
}

/** Run a frame.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the planet light slot of the batch key of a mesh shape.
 *
 * @param world The world
 * @param shape The shape
 * @return The slot, 0 for none
 */
static uint32_t shape_planet(
    ecs_world_t *world,
    ecs_entity_t shape)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *slot = ecs_get(world, shape, DrawSlot);
    test_assert(d != NULL);
    test_assert(slot != NULL && slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, slot->batch);
    uint32_t result = b->key.planet;
    uint32_t shading = box_draws_slot_mesh_shading(b, slot->slot)->material;
    test_uint(shading >> BOX_RENDER_MESH_MATERIAL_BITS, result);

    return result;
}

/** Find the planet light of an entity in the last packet.
 *
 * @param null The backend
 * @param planet The entity with the PlanetAtmosphere
 * @return The light, NULL when the packet has none for it
 */
static const box_render_planet_light_t* packet_light(
    box_render_null_t *null,
    ecs_entity_t planet)
{
    const box_render_planet_light_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.planet_lights); i ++) {
        const box_render_planet_light_t *l = ecs_vec_get_t(
            &null->last.planet_lights, box_render_planet_light_t, i);
        if (l->entity == planet) {
            result = l;
        }
    }

    return result;
}

void PlanetSurfaceLight_planet_mesh_binds(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    frame(world);
    uint32_t slot = shape_planet(world, planet);
    test_assert(slot != 0);
    test_uint(box_render_planet_light_slot(world, planet), slot);
    const box_render_planet_light_t *l = packet_light(null, planet);
    test_assert(l != NULL);
    test_uint(l->slot, slot);
    test_near(l->center[0], 0, 1e-6f);
    test_near(l->center[3], 6360.0f, 1e-2f);
    test_near(l->radii[0], 6360.0f, 1e-2f);
    test_near(l->radii[1], 6460.0f, 1e-2f);
    test_near(l->sun[1], 0.5f, 1e-4f);
    test_near(l->sun[0] * l->sun[0] + l->sun[1] * l->sun[1] + l->sun[2] * l->sun[2], 1,
        1e-4f);
    test_near(l->sun[3], glm_rad(0.266f), 1e-6f);
    test_assert(l->atmosphere != 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_sun_radius(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_get_mut(world, planet, PlanetAtmosphere)->sun_radius = 2;
    ecs_modified(world, planet, PlanetAtmosphere);
    frame(world);
    test_assert(shape_planet(world, planet) != 0);
    const box_render_planet_light_t *l = packet_light(null, planet);
    test_assert(l != NULL);
    test_near(l->sun[3], glm_rad(2.0f), 1e-6f);

    ecs_get_mut(world, planet, PlanetAtmosphere)->sun_radius = 0;
    ecs_modified(world, planet, PlanetAtmosphere);
    frame(world);
    l = packet_light(null, planet);
    test_assert(l != NULL);
    test_flt(l->sun[3], 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_descendants_bind(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t clouds = shape_new(world, planet, 0);
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, planet);
    ecs_set(world, group, Position, {0, 1, 0});
    ecs_entity_t nested = shape_new(world, group, 0);
    frame(world);
    uint32_t slot = shape_planet(world, planet);
    test_assert(slot != 0);
    test_uint(shape_planet(world, clouds), slot);
    test_uint(shape_planet(world, nested), slot);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_nearest_planet_binds(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t moon = planet_new(world, planet, 3);
    ecs_entity_t rock = shape_new(world, moon, 0.5f);
    ecs_entity_t mountain = shape_new(world, planet, 0.5f);
    frame(world);
    uint32_t planet_slot = shape_planet(world, planet);
    uint32_t moon_slot = shape_planet(world, moon);
    test_assert(planet_slot != 0);
    test_assert(moon_slot != 0);
    test_assert(planet_slot != moon_slot);
    test_uint(shape_planet(world, rock), moon_slot);
    test_uint(shape_planet(world, mountain), planet_slot);
    test_assert(packet_light(null, planet) != NULL);
    test_assert(packet_light(null, moon) != NULL);
    test_near(packet_light(null, moon)->center[0], 3, 1e-5f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_not_descendant_unbound(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t station = shape_new(world, scene, 0.5f);
    frame(world);
    test_assert(shape_planet(world, planet) != 0);
    test_uint(shape_planet(world, station), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_reparented_mesh_follows(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t other = planet_new(world, scene, 5);
    ecs_entity_t shape = shape_new(world, scene, 0.5f);
    frame(world);
    test_uint(shape_planet(world, shape), 0);

    ecs_add_pair(world, shape, EcsChildOf, planet);
    frame(world);
    test_uint(shape_planet(world, shape), shape_planet(world, planet));

    ecs_add_pair(world, shape, EcsChildOf, other);
    frame(world);
    test_uint(shape_planet(world, shape), shape_planet(world, other));

    ecs_add_pair(world, shape, EcsChildOf, scene);
    frame(world);
    test_uint(shape_planet(world, shape), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_reparented_group_follows(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_entity_t shape = shape_new(world, group, 0.5f);
    frame(world);
    test_uint(shape_planet(world, shape), 0);

    ecs_add_pair(world, group, EcsChildOf, planet);
    frame(world);
    test_assert(shape_planet(world, shape) != 0);
    test_uint(shape_planet(world, shape), shape_planet(world, planet));
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_added_planet_binds(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t body = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, body, Position, {0, 0, 0});
    ecs_set(world, body, Sphere, {.radius = 1, .segments = 16});
    ecs_entity_t clouds = shape_new(world, body, 0);
    frame(world);
    test_uint(shape_planet(world, body), 0);
    test_uint(shape_planet(world, clouds), 0);

    ecs_add(world, body, PlanetAtmosphere);
    frame(world);
    uint32_t slot = shape_planet(world, body);
    test_assert(slot != 0);
    test_uint(shape_planet(world, clouds), slot);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_removed_planet_unbinds(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t clouds = shape_new(world, planet, 0);
    frame(world);
    test_assert(shape_planet(world, clouds) != 0);

    ecs_remove(world, planet, PlanetAtmosphere);
    frame(world);
    test_uint(shape_planet(world, planet), 0);
    test_uint(shape_planet(world, clouds), 0);
    test_uint(box_render_planet_light_slot(world, planet), 0);
    test_int(ecs_vec_count(&null->last.planet_lights), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_deleted_planet_frees_slot(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t first = planet_new(world, scene, 0);
    frame(world);
    uint32_t slot = shape_planet(world, first);
    test_assert(slot != 0);

    ecs_delete(world, first);
    ecs_entity_t second = planet_new(world, scene, 0);
    frame(world);
    test_uint(shape_planet(world, second), slot);
    test_assert(packet_light(null, first) == NULL);
    test_assert(packet_light(null, second) != NULL);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_no_planet_unbound(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t parent = shape_new(world, scene, 0);
    ecs_entity_t child = shape_new(world, parent, 1);
    frame(world);
    test_uint(shape_planet(world, parent), 0);
    test_uint(shape_planet(world, child), 0);
    test_int(ecs_vec_count(&null->last.planet_lights), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_feature_off_unbound(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, false);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t clouds = shape_new(world, planet, 0);
    frame(world);
    test_uint(shape_planet(world, planet), 0);
    test_uint(shape_planet(world, clouds), 0);
    test_uint(box_render_planet_light_slot(world, planet), 0);
    test_assert(packet_light(null, planet) == NULL);

    ecs_set(world, scene, Scene, {.planet_light = true});
    frame(world);
    test_assert(shape_planet(world, planet) != 0);
    test_uint(shape_planet(world, clouds), shape_planet(world, planet));
    test_assert(packet_light(null, planet) != NULL);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_override_follows_feature(void) {
    // planet_atmosphere/surface_light
    uint32_t mesh = box_render_mesh_overrides(BOX_RENDER_SHADER_MESH,
        BOX_RENDER_FEATURE_PLANET_LIGHT, false, false);
    uint32_t extended = box_render_mesh_overrides(BOX_RENDER_SHADER_MESH_EXTENDED,
        BOX_RENDER_FEATURE_PLANET_LIGHT, true, false);
    test_uint(mesh, BOX_RENDER_OVERRIDE_PLANET_LIGHT);
    test_uint(extended, BOX_RENDER_OVERRIDE_PLANET_LIGHT | BOX_RENDER_OVERRIDE_TRANSMISSIVE);
    test_assert(BOX_RENDER_OVERRIDE_PLANET_LIGHT < (1u << BOX_RENDER_MESH_OVERRIDES_PLAIN));
    test_str(box_render_mesh_override_key(7), "scene_planet_light");
    test_uint(box_render_mesh_overrides(BOX_RENDER_SHADER_MESH, 0, false, false) &
        BOX_RENDER_OVERRIDE_PLANET_LIGHT, 0);
}

void PlanetSurfaceLight_view_features(void) {
    // planet_atmosphere/surface_light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    frame(world);
    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_uint(v->scene, scene);
    test_uint(v->scene_features & BOX_RENDER_FEATURE_PLANET_LIGHT,
        BOX_RENDER_FEATURE_PLANET_LIGHT);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_scaled_parent_scales_light(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t parent = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, parent, Scale, {2, 2, 2});
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t planet = planet_new(world, parent, 0.5f);
    frame(world);
    uint32_t slot = shape_planet(world, planet);
    test_assert(slot != 0);
    const box_render_planet_light_t *l = packet_light(null, planet);
    test_assert(l != NULL);
    test_uint(l->slot, slot);
    test_near(l->center[0], 2, 1e-5f);
    test_near(l->center[1], 0, 1e-5f);
    test_near(l->center[3], 3180.0f, 1e-2f);
    test_near(l->radii[0], 6360.0f, 1e-2f);
    test_near(l->radii[1], 6460.0f, 1e-2f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetSurfaceLight_scaled_planet_scales_light(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, true);
    ecs_entity_t planet = planet_new(world, scene, 0.5f);
    ecs_entity_t clouds = shape_new(world, planet, 0);
    ecs_set(world, planet, Scale, {4, 4, 4});
    frame(world);
    uint32_t slot = shape_planet(world, planet);
    test_assert(slot != 0);
    test_uint(shape_planet(world, clouds), slot);
    const box_render_planet_light_t *l = packet_light(null, planet);
    test_assert(l != NULL);
    test_near(l->center[0], 0.5f, 1e-5f);
    test_near(l->center[3], 1590.0f, 1e-2f);

    ecs_remove(world, planet, Scale);
    frame(world);
    l = packet_light(null, planet);
    test_assert(l != NULL);
    test_assert(l->center[3] == 6360.0f);
    ecs_fini(world);
    box_render_null_free(null);
}
