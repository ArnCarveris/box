/**
 * @file Mesh.c
 * @brief Tests for the mesh a glTF primitive becomes: axes, uvs, sharing and freeing.
 */

#include <box.h>
#include <box/gltf.h>
#include <bake_test.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Mesh_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Mesh_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    for (i = 0; i < packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Mesh_primitive_gets_a_render_mesh(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t prim = ecs_lookup_child(w, root, "primitive0");
    const RenderMesh *mesh = ecs_get(w, prim, RenderMesh);
    test_assert(mesh != NULL);
    test_assert(mesh->handle != 0 || ecs_get(w, prim, Mesh3) != NULL);
    test_flt(mesh->scale.x, 1);
    test_assert(ecs_get(w, prim, Position) != NULL);
}

void Mesh_mesh_is_uploaded(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->vertex_count, 3);
    test_int(cmd->index_count, 3);
}

void Mesh_positions_are_mirrored_in_z(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Mesh3 *m = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Mesh3);
    test_assert(m != NULL);
    test_int(ecs_vec_count(&m->vertices), 3);

    const vec3 *v = ecs_vec_first_t(&m->vertices, vec3);
    test_flt(v[1].x, 1);
    test_flt(v[2].z, -1);
}

void Mesh_winding_survives_the_mirror(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Mesh3 *m = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Mesh3);
    const uint32_t *idx = ecs_vec_first_t(&m->indices, uint32_t);
    test_int(ecs_vec_count(&m->indices), 3);
    test_uint(idx[0], 0);
    test_uint(idx[1], 1);
    test_uint(idx[2], 2);
}

void Mesh_normals_are_mirrored_in_z(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Mesh3 *m = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Mesh3);
    const vec3 *n = ecs_vec_first_t(&m->normals, vec3);
    test_int(ecs_vec_count(&m->normals), 3);
    test_flt(n[0].y, 0.5f);
    test_flt(n[0].z, -0.5f);
}

void Mesh_uv_v_is_flipped(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Mesh3 *m = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Mesh3);
    const vec2 *uv = ecs_vec_first_t(&m->uvs, vec2);
    test_int(ecs_vec_count(&m->uvs), 3);
    test_flt(uv[0].y, 0.75f);
    test_flt(uv[1].x, 1);
    test_flt(uv[2].y, 0);
}

void Mesh_primitives_with_the_same_data_share_a_mesh(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    const RenderMesh *a = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), RenderMesh);
    const RenderMesh *b = ecs_get(w, ecs_lookup_child(w, leaf, "primitive0"), RenderMesh);
    const RenderMesh *c = ecs_get(w, ecs_lookup_child(w, root, "primitive1"), RenderMesh);
    test_uint(a->handle, b->handle);
    test_assert(a->handle != c->handle);
}

void Mesh_unloading_frees_the_mesh(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);

    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    for (i = 0; i < packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    box_packet_reset(&packet);
    ecs_remove(w, e, Gltf);
    box_render_extract(w, &packet);
    test_int(packet.mesh_destroy.count, 2);
}

void Mesh_vertex_colors_are_read(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/colors.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Mesh3 *m = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Mesh3);
    test_assert(m != NULL);
    test_int(ecs_vec_count(&m->colors), 3);

    const rgba *colors = ecs_vec_first_t(&m->colors, rgba);
    test_int(colors[0].r, 255);
    test_int(colors[0].g, 0);
    test_int(colors[0].b, 0);
    test_int(colors[0].a, 64);
    test_int(colors[1].g, 255);
    test_int(colors[1].a, 255);
    test_int(colors[2].r, 188);
    test_int(colors[2].g, 188);
    test_int(colors[2].b, 188);
    test_int(colors[2].a, 255);
}

void Mesh_vertex_colors_reach_the_upload(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/colors.gltf") });

    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    const uint8_t *bytes = (const uint8_t*)cmd->vertices;
    size_t stride = (size_t)BOX_VERTEX_FLOATS * sizeof(float);

    test_int(bytes[BOX_VERTEX_COLOR_OFFSET], 255);
    test_int(bytes[BOX_VERTEX_COLOR_OFFSET + 1], 0);
    test_int(bytes[BOX_VERTEX_COLOR_OFFSET + 3], 64);
    test_int(bytes[stride + BOX_VERTEX_COLOR_OFFSET + 1], 255);
    test_int(bytes[stride + BOX_VERTEX_COLOR_OFFSET + 3], 255);
    test_int(bytes[stride * 2 + BOX_VERTEX_COLOR_OFFSET + 2], 188);
}
