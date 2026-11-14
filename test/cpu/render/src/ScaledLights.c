/**
 * @file ScaledLights.c
 * @brief Tests for point and spot lights under a scaled parent: the range of the light data
 * follows the uniform world scale s of the light and its intensity s squared.
 *
 * The cases render into the null backend with a camera at (0, 2, 10) looking down -Z and read
 * the lights the extract lists for the views of the default scene.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "bake.h"

/** Create a world that renders into the null backend with one view that renders shadows.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* scaled_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});

    return world;
}

/** Create a parent at the origin with a scale.
 *
 * @param world The world
 * @param x Scale along X
 * @param y Scale along Y
 * @param z Scale along Z
 * @return The parent
 */
static ecs_entity_t scaled_parent(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Scale, {x, y, z});

    return e;
}

/** Create a point light as a child of a parent.
 *
 * @param world The world
 * @param parent The parent
 * @param position Local position of the light
 * @param intensity Its intensity
 * @param range Its range
 * @param shadows Whether it casts shadows
 * @return The light
 */
static ecs_entity_t scaled_point(
    ecs_world_t *world,
    ecs_entity_t parent,
    vec3 position,
    float intensity,
    float range,
    bool shadows)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_add(world, e, PointLight);
    PointLight *p = ecs_get_mut(world, e, PointLight);
    p->intensity = intensity;
    p->range = range;
    p->shadows = shadows;
    ecs_modified(world, e, PointLight);

    return e;
}

/** Create a spot light shining straight down as a child of a parent.
 *
 * @param world The world
 * @param parent The parent
 * @param position Local position of the light
 * @param intensity Its intensity
 * @param range Its range
 * @return The light
 */
static ecs_entity_t scaled_spot(
    ecs_world_t *world,
    ecs_entity_t parent,
    vec3 position,
    float intensity,
    float range)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_add(world, e, SpotLight);
    SpotLight *s = ecs_get_mut(world, e, SpotLight);
    s->intensity = intensity;
    s->range = range;
    ecs_modified(world, e, SpotLight);

    return e;
}

/** Run a frame into the null backend.
 *
 * @param world The world
 * @param null The backend
 * @return The packet the backend received
 */
static const box_render_packet_t* scaled_frame(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 0);
    box_render_sync(world);

    return &null->last;
}

/** Find the light data the view list of the default scene holds for a light.
 *
 * @param world The world
 * @param light The light
 * @return Its data, NULL when the light is not listed
 */
static const box_render_light_t* scaled_light(
    ecs_world_t *world,
    ecs_entity_t light)
{
    box_bake_light_set_t set;
    const box_render_light_t *result = NULL;
    int32_t i;
    box_bake_view_lights(box_render_lights_state(world), 0, &set);
    for (i = 0; i < set.count; i ++) {
        if (set.entities[i] == light) {
            result = &set.lights[i];
        }
    }

    return result;
}

/** Find the shadow tile of a light in a packet.
 *
 * @param p The packet
 * @param light The light
 * @return Its tile, NULL for none
 */
static const box_render_shadow_tile_t* scaled_tile(
    const box_render_packet_t *p,
    ecs_entity_t light)
{
    const box_render_shadow_tile_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->shadow_tiles); i ++) {
        const box_render_shadow_tile_t *t = ecs_vec_get_t(
            &p->shadow_tiles, box_render_shadow_tile_t, i);
        if (t->light == light) {
            result = t;
        }
    }

    return result;
}

void ScaledLights_world_scale(void) {
    // lighting/lights
    WorldTransform t;
    glm_mat4_identity(t.m);
    test_assert(box_transform_world_scale(&t) == 1.0f);

    glm_rotate(t.m, 0.7f, (vec3){0.3f, 0.8f, 0.5f});
    test_assert(box_transform_world_scale(&t) == 1.0f);

    glm_mat4_identity(t.m);
    glm_scale(t.m, (vec3){2, 2, 2});
    test_flt(box_transform_world_scale(&t), 2.0f);

    glm_mat4_identity(t.m);
    glm_scale(t.m, (vec3){1, 3, 9});
    test_flt(box_transform_world_scale(&t), 3.0f);

    glm_mat4_identity(t.m);
    glm_scale(t.m, (vec3){-2, 2, 2});
    test_flt(box_transform_world_scale(&t), 2.0f);

    glm_mat4_identity(t.m);
    glm_scale(t.m, (vec3){0, 1, 1});
    test_flt(box_transform_world_scale(&t), 0.0f);
}

void ScaledLights_point_uniform(void) {
    // lighting/lights
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 10, 10, 10);
    ecs_entity_t light = scaled_point(world, parent, (vec3){0, 0.4f, 0}, 2.0f, 1.5f, false);
    scaled_frame(world, null);

    const box_render_light_t *l = scaled_light(world, light);
    test_assert(l != NULL);
    test_flt(l->position[0], 0.0f);
    test_flt(l->position[1], 4.0f);
    test_flt(l->position[2], 0.0f);
    test_flt(l->range, 15.0f);
    test_flt(l->color[0], 200.0f);
    test_flt(l->color[1], 200.0f);
    test_flt(l->color[2], 200.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void ScaledLights_spot_uniform(void) {
    // lighting/lights
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 3, 3, 3);
    ecs_entity_t light = scaled_spot(world, parent, (vec3){0, 1, 0}, 1.0f, 10.0f);
    scaled_frame(world, null);

    const box_render_light_t *l = scaled_light(world, light);
    test_assert(l != NULL);
    test_flt(l->position[1], 3.0f);
    test_flt(l->range, 30.0f);
    test_flt(l->color[0], 9.0f);
    test_flt(l->color[1], 9.0f);
    test_flt(l->color[2], 9.0f);
    test_flt(l->direction[0], 0.0f);
    test_flt(l->direction[1], -1.0f);
    test_flt(l->direction[2], 0.0f);

    float cos_outer = cosf(glm_rad(30.0f));
    float cos_inner = cosf(glm_rad(20.0f));
    test_flt(l->spot_scale, 1.0f / (cos_inner - cos_outer));
    test_flt(l->spot_offset, -cos_outer / (cos_inner - cos_outer));

    ecs_fini(world);
    box_render_null_free(null);
}

void ScaledLights_non_uniform(void) {
    // lighting/lights
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 1, 3, 9);
    ecs_entity_t point = scaled_point(world, parent, (vec3){0, 1, 0}, 1.0f, 2.0f, false);
    ecs_entity_t spot = scaled_spot(world, parent, (vec3){0, 1, 0}, 0.5f, 4.0f);
    scaled_frame(world, null);

    const box_render_light_t *l = scaled_light(world, point);
    test_assert(l != NULL);
    test_flt(l->range, 6.0f);
    test_flt(l->color[0], 9.0f);

    l = scaled_light(world, spot);
    test_assert(l != NULL);
    test_flt(l->range, 12.0f);
    test_flt(l->color[0], 4.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void ScaledLights_scale_one_unchanged(void) {
    // lighting/lights
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 1, 1, 1);
    ecs_set(world, parent, Rotation, {0.3f, 1.1f, -0.7f});
    ecs_entity_t point = scaled_point(world, parent, (vec3){0, 1, 0}, 1.7f, 7.3f, false);
    ecs_entity_t spot = scaled_spot(world, parent, (vec3){0, 1, 0}, 2.3f, 9.1f);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 3, 0});
    ecs_add(world, root, PointLight);
    PointLight *p = ecs_get_mut(world, root, PointLight);
    p->intensity = 0.37f;
    p->range = 3.3f;
    ecs_modified(world, root, PointLight);
    scaled_frame(world, null);

    const box_render_light_t *l = scaled_light(world, point);
    test_assert(l != NULL);
    test_assert(l->range == 7.3f);
    test_assert(l->color[0] == 1.7f);

    l = scaled_light(world, spot);
    test_assert(l != NULL);
    test_assert(l->range == 9.1f);
    test_assert(l->color[0] == 2.3f);

    l = scaled_light(world, root);
    test_assert(l != NULL);
    test_assert(l->range == 3.3f);
    test_assert(l->color[0] == 0.37f);

    ecs_fini(world);
    box_render_null_free(null);
}

void ScaledLights_point_shadow_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 2, 2, 2);
    ecs_entity_t light = scaled_point(world, parent, (vec3){0, 2, 0}, 1.0f, 10.0f, true);
    const box_render_packet_t *p = scaled_frame(world, null);

    const box_render_shadow_tile_t *tile = scaled_tile(p, light);
    test_assert(tile != NULL);
    test_int(tile->projection, BOX_RENDER_SHADOW_OCTAHEDRAL);
    test_flt(tile->position[1], 4.0f);
    test_flt(tile->near_, 0.1f);
    test_flt(tile->far_, 20.0f);

    const box_render_light_t *l = scaled_light(world, light);
    test_assert(l != NULL);
    test_flt(l->range, 20.0f);
    test_assert(l->shadow != BOX_RENDER_SHADOW_NONE);

    ecs_fini(world);
    box_render_null_free(null);
}

void ScaledLights_flat_parent(void) {
    // lighting/lights
    box_render_null_t *null;
    ecs_world_t *world = scaled_world(&null);
    ecs_entity_t parent = scaled_parent(world, 1, 0, 1);
    ecs_entity_t point = scaled_point(world, parent, (vec3){0, 1, 0}, 1.0f, 10.0f, true);
    ecs_entity_t spot = scaled_spot(world, parent, (vec3){0, 1, 0}, 1.0f, 10.0f);
    const box_render_packet_t *p = scaled_frame(world, null);

    test_assert(scaled_light(world, point) == NULL);
    test_assert(scaled_light(world, spot) == NULL);
    test_assert(scaled_tile(p, point) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}
