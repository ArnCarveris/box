/**
 * @file Mesh3.c
 * @brief Tests for Mesh3: upload, bounds, validation, sharing, replacing and freeing.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Mesh3_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Mesh3_teardown(void)
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

void Mesh3_upload(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(-1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 2, 0);
    ecs_vec_init_t(NULL, &m->indices, uint32_t, 3);
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 0;
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 1;
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 2;
    ecs_modified(w, e, Mesh3);

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->scale.x, 1);
    test_flt(mesh->scale.y, 1);
    test_flt(mesh->scale.z, 1);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_uint(cmd->handle, mesh->handle);
    test_int(cmd->vertex_count, 3);
    test_int(cmd->index_count, 3);
    test_flt(cmd->vertices[0], -1);
    test_flt(cmd->vertices[BOX_VERTEX_FLOATS * 2 + 1], 2);
    test_uint(cmd->indices[2], 2);
}

void Mesh3_bounds(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(-2, 0, 1);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(3, 0, -4);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 5, 0);
    ecs_modified(w, e, Mesh3);

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->bounds_min.x, -2);
    test_flt(mesh->bounds_min.y, 0);
    test_flt(mesh->bounds_min.z, -4);
    test_flt(mesh->bounds_max.x, 3);
    test_flt(mesh->bounds_max.y, 5);
    test_flt(mesh->bounds_max.z, 1);
}

void Mesh3_derives_normals(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 1);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    uint32_t v;
    for (v = 0; v < cmd->vertex_count; v ++) {
        const float *n = &cmd->vertices[v * BOX_VERTEX_FLOATS + 3];

        test_flt(n[0], 0);
        test_flt(n[1], 1);
        test_flt(n[2], 0);
    }
}

void Mesh3_keeps_given_normals(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);
    int32_t i;

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 1);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_init_t(NULL, &m->normals, vec3, 3);
    for (i = 0; i < 3; i ++) {
        ecs_vec_append_t(NULL, &m->normals, vec3)[0] = vec3_new(0, 0, -1);
    }

    ecs_modified(w, e, Mesh3);
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    uint32_t v;
    for (v = 0; v < cmd->vertex_count; v ++) {
        const float *n = &cmd->vertices[v * BOX_VERTEX_FLOATS + 3];

        test_flt(n[1], 0);
        test_flt(n[2], -1);
    }
}

void Mesh3_without_indices_draws_in_order(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->index_count, 3);
    test_uint(cmd->indices[0], 0);
    test_uint(cmd->indices[1], 1);
    test_uint(cmd->indices[2], 2);
}

void Mesh3_uvs_default_to_zero(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->uvs, vec2, 3);
    ecs_vec_append_t(NULL, &m->uvs, vec2)[0] = (vec2){0.25f, 0.5f};
    ecs_vec_append_t(NULL, &m->uvs, vec2)[0] = (vec2){0, 0};
    ecs_vec_append_t(NULL, &m->uvs, vec2)[0] = (vec2){0, 0};
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_flt(cmd->vertices[6], 0.25f);
    test_flt(cmd->vertices[7], 0.5f);
    test_flt(cmd->vertices[BOX_VERTEX_FLOATS + 6], 0);
}

void Mesh3_shared_between_entities(void)
{
    // meshes/mesh3
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    Mesh3 *ma = ecs_ensure(w, a, Mesh3);

    ecs_vec_init_t(NULL, &ma->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, a, Mesh3);

    Mesh3 *mb = ecs_ensure(w, b, Mesh3);
    ecs_vec_init_t(NULL, &mb->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, b, Mesh3);

    const RenderMesh *mesh_a = ecs_get(w, a, RenderMesh);
    const RenderMesh *mesh_b = ecs_get(w, b, RenderMesh);
    test_assert(mesh_a != NULL);
    test_assert(mesh_b != NULL);
    test_uint(mesh_a->handle, mesh_b->handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
}

void Mesh3_prefab_instances_share(void)
{
    // meshes/mesh3
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    Mesh3 *m = ecs_ensure(w, prefab, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, prefab, Mesh3);

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

void Mesh3_replacing_the_data_uploads_again(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *first = box_vec_at(&packet.mesh_create, 0);
    uint32_t handle = first->handle;
    free(first->vertices);
    free(first->indices);
    box_packet_reset(&packet);

    Mesh3 *again = ecs_ensure(w, e, Mesh3);
    ecs_vec_fini_t(NULL, &again->vertices, vec3);
    ecs_vec_init_t(NULL, &again->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &again->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &again->vertices, vec3)[0] = vec3_new(4, 0, 0);
    ecs_vec_append_t(NULL, &again->vertices, vec3)[0] = vec3_new(0, 4, 0);
    ecs_modified(w, e, Mesh3);

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh->handle != handle);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
    test_int(packet.mesh_destroy.count, 1);
    test_uint(*(uint32_t*)box_vec_at(&packet.mesh_destroy, 0), handle);
}

void Mesh3_freed_with_the_last_user(void)
{
    // meshes/mesh3
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    Mesh3 *ma = ecs_ensure(w, a, Mesh3);

    ecs_vec_init_t(NULL, &ma->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, a, Mesh3);

    Mesh3 *mb = ecs_ensure(w, b, Mesh3);
    ecs_vec_init_t(NULL, &mb->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, b, Mesh3);

    uint32_t handle = ecs_get(w, a, RenderMesh)->handle;
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_delete(w, a);
    box_render_extract(w, &packet);
    test_int(packet.mesh_destroy.count, 0);

    box_packet_reset(&packet);
    ecs_delete(w, b);
    box_render_extract(w, &packet);
    test_int(packet.mesh_destroy.count, 1);
    test_uint(*(uint32_t*)box_vec_at(&packet.mesh_destroy, 0), handle);
}

void Mesh3_removing_the_component_removes_the_mesh(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, e, Mesh3);

    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_remove(w, e, Mesh3);
    test_assert(ecs_get(w, e, RenderMesh) == NULL);
    test_assert(ecs_get(w, e, RenderInstance) == NULL);

    box_render_extract(w, &packet);
    test_int(packet.mesh_destroy.count, 1);
    test_uint(*(uint32_t*)box_vec_at(&packet.mesh_destroy, 0), handle);
}

void Mesh3_rejects_an_index_out_of_range(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->indices, uint32_t, 3);
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 0;
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 1;
    ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = 3;

    ecs_log_set_level(-4);
    ecs_modified(w, e, Mesh3);
    ecs_log_set_level(-1);

    test_assert(ecs_get(w, e, RenderMesh) == NULL);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 0);
}

void Mesh3_rejects_a_partial_triangle(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);
    int32_t i;

    ecs_vec_init_t(NULL, &m->vertices, vec3, 4);
    for (i = 0; i < 4; i ++) {
        ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new((float)i, 0, 0);
    }

    ecs_vec_init_t(NULL, &m->indices, uint32_t, 4);
    for (i = 0; i < 4; i ++) {
        ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = (uint32_t)i;
    }

    ecs_log_set_level(-4);
    ecs_modified(w, e, Mesh3);
    ecs_log_set_level(-1);

    test_assert(ecs_get(w, e, RenderMesh) == NULL);
}

void Mesh3_rejects_a_normal_count_mismatch(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->normals, vec3, 2);
    ecs_vec_append_t(NULL, &m->normals, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_append_t(NULL, &m->normals, vec3)[0] = vec3_new(0, 1, 0);

    ecs_log_set_level(-4);
    ecs_modified(w, e, Mesh3);
    ecs_log_set_level(-1);

    test_assert(ecs_get(w, e, RenderMesh) == NULL);
}

void Mesh3_rejects_a_color_count_mismatch(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->colors, rgb, 1);
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){255, 0, 0};

    ecs_log_set_level(-4);
    ecs_modified(w, e, Mesh3);
    ecs_log_set_level(-1);

    test_assert(ecs_get(w, e, RenderMesh) == NULL);
}

void Mesh3_rejects_an_empty_mesh(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);

    ecs_ensure(w, e, Mesh3);
    ecs_log_set_level(-4);
    ecs_modified(w, e, Mesh3);
    ecs_log_set_level(-1);

    test_assert(ecs_get(w, e, RenderMesh) == NULL);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 0);
}

void Mesh3_colors_are_accepted_and_uploaded(void)
{
    // meshes/mesh3
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);
    int32_t i;

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->colors, rgb, 3);
    for (i = 0; i < 3; i ++) {
        ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){255, 128, 0};
    }

    ecs_modified(w, e, Mesh3);
    test_assert(ecs_get(w, e, RenderMesh) != NULL);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->vertex_count, 3);
}

void Mesh3_differing_colors_upload_separately(void)
{
    // meshes/mesh3
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    Mesh3 *ma = ecs_ensure(w, a, Mesh3);
    int32_t i;

    ecs_vec_init_t(NULL, &ma->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &ma->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &ma->colors, rgb, 3);
    for (i = 0; i < 3; i ++) {
        ecs_vec_append_t(NULL, &ma->colors, rgb)[0] = (rgb){255, 0, 0};
    }

    ecs_modified(w, a, Mesh3);

    Mesh3 *mb = ecs_ensure(w, b, Mesh3);
    ecs_vec_init_t(NULL, &mb->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &mb->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &mb->colors, rgb, 3);
    for (i = 0; i < 3; i ++) {
        ecs_vec_append_t(NULL, &mb->colors, rgb)[0] = (rgb){0, 0, 255};
    }

    ecs_modified(w, b, Mesh3);

    test_assert(ecs_get(w, a, RenderMesh)->handle != ecs_get(w, b, RenderMesh)->handle);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);
}

void Mesh3_colors_are_uploaded(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->colors, rgb, 3);
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){255, 0, 0};
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){0, 128, 0};
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){0, 0, 64};
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    const uint8_t *bytes = (const uint8_t*)cmd->vertices;
    size_t stride = (size_t)BOX_VERTEX_FLOATS * sizeof(float);

    test_int(bytes[BOX_VERTEX_COLOR_OFFSET], 255);
    test_int(bytes[BOX_VERTEX_COLOR_OFFSET + 1], 0);
    test_int(bytes[BOX_VERTEX_COLOR_OFFSET + 2], 0);
    test_int(bytes[BOX_VERTEX_COLOR_OFFSET + 3], 255);
    test_int(bytes[stride + BOX_VERTEX_COLOR_OFFSET + 1], 128);
    test_int(bytes[stride * 2 + BOX_VERTEX_COLOR_OFFSET + 2], 64);
    test_int(bytes[stride * 2 + BOX_VERTEX_COLOR_OFFSET + 3], 255);
}

void Mesh3_colors_default_to_white(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    Mesh3 *m = ecs_ensure(w, e, Mesh3);

    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_modified(w, e, Mesh3);

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    const uint8_t *bytes = (const uint8_t*)cmd->vertices;
    uint32_t v, c;
    for (v = 0; v < cmd->vertex_count; v ++) {
        const uint8_t *color = bytes + (size_t)v * BOX_VERTEX_FLOATS * sizeof(float) +
            BOX_VERTEX_COLOR_OFFSET;

        for (c = 0; c < 4; c ++) {
            test_int(color[c], 255);
        }
    }
}
