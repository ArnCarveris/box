/**
 * @file Shape.c
 * @brief Tests for the shape registry: registering, sharing, changing and unregistering.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

/** Full turn as a float constant */
#define FAN_TAU (6.28318530717958647692f)

/** Shape of the test project: a flat regular polygon with a radius */
typedef struct Fan {
    int32_t sides; /**< Corners of the polygon */
    float radius;  /**< Circumradius */
} Fan;

ECS_COMPONENT_DECLARE(Fan);

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t registration;

static void fan_key(
    const void *value,
    box_shape_key_t *key,
    vec3 *scale)
{
    const Fan *fan = value;
    float radius = fan->radius > 0.0f ? fan->radius : 1.0f;

    key->i0 = fan->sides;
    *scale = vec3_new(radius, radius, 1.0f);
}

static void fan_build(
    const box_shape_key_t *key,
    box_shape_mesh_t *mesh)
{
    int32_t sides = key->i0 < 3 ? 3 : key->i0;
    vec3 normal = vec3_new(0.0f, 0.0f, -1.0f);
    uint32_t center;
    int32_t i;

    box_shape_reserve(mesh, sides + 1, sides * 3);
    center = box_shape_vertex(mesh, vec3_new(0.0f, 0.0f, 0.0f), normal, 0.5f, 0.5f);
    for (i = 0; i < sides; i ++) {
        float angle = (float)i / (float)sides * FAN_TAU;
        float x = cosf(angle);
        float y = sinf(angle);

        box_shape_vertex(mesh, vec3_new(x, y, 0.0f), normal, x * 0.5f + 0.5f, y * 0.5f + 0.5f);
    }

    for (i = 0; i < sides; i ++) {
        uint32_t a = (uint32_t)(i + 1);
        uint32_t b = (uint32_t)((i + 1) % sides + 1);

        box_shape_triangle(mesh, center, b, a);
    }
}

void Shape_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_COMPONENT_DEFINE(w, Fan);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    registration = ecs_new(w);
    ecs_set(w, registration, Shape, {
        .component = ecs_id(Fan), .key = fan_key, .build = fan_build
    });
}

void Shape_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Shape_writes_a_render_mesh(void)
{
    // meshes/shape_registry
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Fan, {6, 2});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->scale.x, 2);
    test_flt(mesh->scale.y, 2);
    test_flt(mesh->scale.z, 1);
    test_flt(mesh->bounds_min.z, 0);
    test_flt(mesh->bounds_max.z, 0);
    test_assert(mesh->bounds_max.x > 0.99f);
}

void Shape_uploads_the_generated_mesh(void)
{
    // meshes/shape_registry
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Fan, {5, 1});
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_uint(cmd->handle, ecs_get(w, e, RenderMesh)->handle);
    test_int(cmd->vertex_count, 6);
    test_int(cmd->index_count, 15);
}

void Shape_shares_the_mesh_of_one_key(void)
{
    // meshes/shape_registry
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);

    ecs_set(w, a, Fan, {8, 1});
    ecs_set(w, b, Fan, {8, 4});

    const RenderMesh *mesh_a = ecs_get(w, a, RenderMesh);
    const RenderMesh *mesh_b = ecs_get(w, b, RenderMesh);
    test_assert(mesh_a != NULL);
    test_assert(mesh_b != NULL);
    test_uint(mesh_a->handle, mesh_b->handle);
    test_flt(mesh_a->scale.x, 1);
    test_flt(mesh_b->scale.x, 4);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Shape_another_key_builds_another_mesh(void)
{
    // meshes/shape_registry
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);

    ecs_set(w, a, Fan, {5, 1});
    ecs_set(w, b, Fan, {7, 1});
    test_assert(ecs_get(w, a, RenderMesh)->handle != ecs_get(w, b, RenderMesh)->handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);
}

void Shape_changing_a_parameter_changes_the_mesh(void)
{
    // meshes/shape_registry
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Fan, {5, 1});

    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    ecs_set(w, e, Fan, {9, 1});
    test_assert(ecs_get(w, e, RenderMesh)->handle != handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);
}

void Shape_changing_only_the_scale_keeps_the_mesh(void)
{
    // meshes/shape_registry
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Fan, {5, 1});

    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    ecs_set(w, e, Fan, {5, 3});
    test_uint(ecs_get(w, e, RenderMesh)->handle, handle);
    test_flt(ecs_get(w, e, RenderMesh)->scale.x, 3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Shape_prefab_instances_share_the_shape(void)
{
    // meshes/shape_registry
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);

    ecs_set(w, prefab, Fan, {6, 1});

    ecs_entity_t a = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_entity_t b = ecs_new_w_pair(w, EcsIsA, prefab);
    const RenderMesh *mesh_a = ecs_get(w, a, RenderMesh);
    const RenderMesh *mesh_b = ecs_get(w, b, RenderMesh);
    test_assert(mesh_a != NULL);
    test_assert(mesh_b != NULL);
    test_uint(mesh_a->handle, mesh_b->handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Shape_unregistering_stops_the_observer(void)
{
    // meshes/shape_registry
    ecs_entity_t before = ecs_new(w);

    ecs_set(w, before, Fan, {6, 1});
    test_assert(ecs_get(w, before, RenderMesh) != NULL);

    ecs_remove(w, registration, Shape);

    ecs_entity_t after = ecs_new(w);
    ecs_set(w, after, Fan, {6, 1});
    test_assert(ecs_get(w, after, RenderMesh) == NULL);
    test_assert(ecs_get(w, before, RenderMesh) != NULL);
}

void Shape_deleting_the_registration_stops_the_observer(void)
{
    // meshes/shape_registry
    ecs_delete(w, registration);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Fan, {6, 1});
    test_assert(ecs_get(w, e, RenderMesh) == NULL);
}

void Shape_registering_again_replaces_the_observer(void)
{
    // meshes/shape_registry
    ecs_set(w, registration, Shape, {
        .component = ecs_id(Fan), .key = fan_key, .build = fan_build
    });

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Fan, {6, 1});
    test_assert(ecs_get(w, e, RenderMesh) != NULL);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Shape_rejects_a_registration_without_a_component(void)
{
    // meshes/shape_registry
    ecs_entity_t bad = ecs_new(w);

    ecs_log_set_level(-4);
    ecs_set(w, bad, Shape, { .component = 0, .key = fan_key, .build = fan_build });
    ecs_log_set_level(-1);
    test_assert(ecs_get(w, bad, Shape) != NULL);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Fan, {6, 1});
    test_assert(ecs_get(w, e, RenderMesh) != NULL);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Shape_star_example_is_registered(void)
{
    // meshes/shape_registry
    ecs_entity_t star = ecs_lookup(w, "box.geometry.star");
    test_assert(star != 0);
    test_assert(ecs_get(w, star, Shape) != NULL);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Star, {5, 0.45, 2});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->scale.x, 2);
    test_flt(mesh->scale.y, 2);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->vertex_count, 11);
    test_int(cmd->index_count, 30);
}

void Shape_star_shares_one_mesh_per_outline(void)
{
    // meshes/shape_registry
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t c = ecs_new(w);

    ecs_set(w, a, Star, {5, 0.45, 1});
    ecs_set(w, b, Star, {5, 0.45, 3});
    ecs_set(w, c, Star, {6, 0.45, 1});
    test_uint(ecs_get(w, a, RenderMesh)->handle, ecs_get(w, b, RenderMesh)->handle);
    test_assert(ecs_get(w, a, RenderMesh)->handle != ecs_get(w, c, RenderMesh)->handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);
}
