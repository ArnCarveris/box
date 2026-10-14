/**
 * @file Draws.c
 * @brief Tests for the extract of the forward pass: meshes, instances and batches on the
 * null backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "mesh_cache.h"

void Draws_shape_sends_mesh_and_batch(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    test_assert(shape->mesh != 0);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    int32_t i, meshes = 0, buffers = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_MESH) {
            const box_render_mesh_desc_t *desc = box_render_packet_data(p, r->data);
            test_int(r->op, BOX_RENDER_CREATE);
            test_uint(r->handle, shape->mesh);
            test_uint(desc->vertex_count, mesh->vertex_count);
            test_uint(desc->index_count, mesh->index_count);
            test_uint(r->size, sizeof(box_render_mesh_desc_t) +
                mesh->vertex_count * sizeof(box_vertex_t) +
                mesh->index_count * sizeof(uint32_t));
            const box_vertex_t *vertices = (const box_vertex_t*)(desc + 1);
            const uint32_t *indices = (const uint32_t*)(vertices + desc->vertex_count);
            test_flt(vertices[1].position[0], mesh->vertices[1].position[0]);
            test_uint(indices[2], mesh->indices[2]);
            meshes ++;
        } else if (r->kind == BOX_RENDER_BUFFER) {
            const box_render_buffer_desc_t *desc = box_render_packet_data(p, r->data);
            test_int(r->op, BOX_RENDER_CREATE);
            test_uint(r->handle, b->instances);
            test_assert(desc->size >= sizeof(box_render_instance_t));
            test_uint(desc->usage, BOX_RENDER_BUFFER_INSTANCE | BOX_RENDER_BUFFER_STORAGE);
            buffers ++;
        }
    }

    test_int(meshes, 1);
    test_int(buffers, 1);
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, b->instances);
    test_uint(u->offset, 0);
    test_uint(u->size, sizeof(box_render_instance_t));
    test_int(b->view, -1);
    test_uint(b->scene, 0);
    test_uint(b->pipeline, BOX_RENDER_SHADER_MESH);
    test_uint(b->mesh, shape->mesh);
    test_assert(b->instances >= BOX_RENDER_BUFFER_FIRST);
    test_uint(b->first_instance, 0);
    test_uint(b->instance_count, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_shared_mesh_sent_once(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {2, 3, 4});
    ecs_progress(world, 0);
    box_render_sync(world);

    uint32_t mesh = ecs_get(world, a, ShapeMesh)->mesh;
    test_uint(ecs_get(world, b, ShapeMesh)->mesh, mesh);
    const box_render_packet_t *p = &null->last;
    int32_t i, meshes = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        meshes += ecs_vec_get_t(&p->resources, box_render_resource_t, i)->kind ==
            BOX_RENDER_MESH;
    }

    test_int(meshes, 1);
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->mesh, mesh);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->first_instance, 0);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 2);
    test_int(box_mesh_cache_refs(box_mesh_cache_of(world), mesh), 3);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&p->resources), 0);
    test_int(ecs_vec_count(&p->batches), 1);
    test_int(ecs_vec_count(&p->uploads), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_no_views_no_batches(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.batches), 0);
    test_int(ecs_vec_count(&null->last.resources), 0);
    test_int(ecs_vec_count(&null->last.uploads), 0);
    test_int(box_mesh_cache_refs(box_mesh_cache_of(world), ecs_get(world, e, ShapeMesh)->mesh),
        1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_removed_shape_destroys_mesh(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    test_int(box_mesh_cache_refs(cache, mesh), 2);
    ecs_remove(world, e, Box);
    test_int(box_mesh_cache_refs(cache, mesh), 1);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->resources), 2);
    const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, 0);
    test_int(r->kind, BOX_RENDER_BUFFER);
    test_int(r->op, BOX_RENDER_DESTROY);
    r = ecs_vec_get_t(&p->resources, box_render_resource_t, 1);
    test_int(r->kind, BOX_RENDER_MESH);
    test_int(r->op, BOX_RENDER_DESTROY);
    test_uint(r->handle, mesh);
    test_int(ecs_vec_count(&p->batches), 0);
    test_int(box_mesh_cache_refs(cache, mesh), 0);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_reused_handle_sent_again(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    ecs_delete(world, e);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Sphere, {.segments = 8, .radius = 1});
    test_uint(ecs_get(world, s, ShapeMesh)->mesh, mesh);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_mesh_t *sphere = box_mesh_cache_get(box_mesh_cache_of(world), mesh);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->resources), 2);
    const box_render_resource_t *r = ecs_vec_first_t(&p->resources, box_render_resource_t);
    test_int(r->kind, BOX_RENDER_MESH);
    test_int(r->op, BOX_RENDER_CREATE);
    test_uint(r->handle, mesh);
    const box_render_mesh_desc_t *desc = box_render_packet_data(p, r->data);
    test_uint(desc->vertex_count, sphere->vertex_count);
    test_int(ecs_vec_get_t(&p->resources, box_render_resource_t, 1)->kind, BOX_RENDER_BUFFER);
    test_int(ecs_vec_count(&p->batches), 1);
    test_int(null->resources, 6);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_instance_data(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {1, 2, 3});
    ecs_set(world, a, Box, {2, 1, 4});
    ecs_set(world, a, Rgb, {255, 0, 0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Scale, {1, 0.5f, 1});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->size, 2 * sizeof(box_render_instance_t));
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_int(ecs_vec_count(&p->batches), 1);
    int32_t ia = 0;
    int32_t ib = 1;
    if (inst[ia].color[1] > 0.5f) {
        ia = 1;
        ib = 0;
    }

    test_flt(inst[ia].model[0][0], 2);
    test_flt(inst[ia].model[1][1], 1);
    test_flt(inst[ia].model[2][2], 4);
    test_flt(inst[ia].model[3][0], 1);
    test_flt(inst[ia].model[3][1], 2);
    test_flt(inst[ia].model[3][2], 3);
    test_flt(inst[ia].normal[0][0], 0.5f);
    test_flt(inst[ia].normal[1][1], 1);
    test_flt(inst[ia].normal[2][2], 0.25f);
    test_flt(inst[ia].color[0], 1);
    test_flt(inst[ia].color[1], 0);
    test_flt(inst[ia].color[2], 0);
    test_flt(inst[ia].color[3], 1);

    test_flt(inst[ib].model[1][1], 0.5f);
    test_flt(inst[ib].normal[1][1], 2);
    test_flt(inst[ib].normal[0][0], 1);
    test_assert(inst[ib].color[0] > 0.5f);
    test_flt(inst[ib].color[0], inst[ib].color[1]);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_scene_of_batch(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Scene, {0});
    ecs_set(world, root, Box, {1, 1, 1});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, child, Sphere, {.radius = 1});
    ecs_entity_t outside = ecs_new(world);
    ecs_set(world, outside, Torus, {.radius = 1, .tube = 0.2f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 3);
    int32_t i, found = 0;
    for (i = 0; i < 3; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        if (b->mesh == ecs_get(world, root, ShapeMesh)->mesh) {
            test_uint(b->scene, root);
            found ++;
        } else if (b->mesh == ecs_get(world, child, ShapeMesh)->mesh) {
            test_uint(b->scene, root);
            found ++;
        } else if (b->mesh == ecs_get(world, outside, ShapeMesh)->mesh) {
            test_uint(b->scene, 0);
            found ++;
        }
    }

    test_int(found, 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Draws_new_backend_resends(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->resources, 2);

    box_render_null_t *other = box_render_null_new();
    backend = box_render_null_backend(other);
    box_render_set_backend(world, &backend);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(other->resources, 2);
    test_int(box_mesh_cache_refs(box_mesh_cache_of(world), ecs_get(world, e, ShapeMesh)->mesh),
        2);

    ecs_fini(world);
    box_render_null_free(null);
    box_render_null_free(other);
}

void Draws_instance_buffer_grows(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 64; i ++) {
        ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.resources), 0);

    ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->resources), 1);
    const box_render_resource_t *r = ecs_vec_first_t(&p->resources, box_render_resource_t);
    test_int(r->kind, BOX_RENDER_BUFFER);
    test_int(r->op, BOX_RENDER_CREATE);
    const box_render_buffer_desc_t *desc = box_render_packet_data(p, r->data);
    test_uint(desc->size, 128 * sizeof(box_render_instance_t));
    test_uint(ecs_vec_first_t(&p->uploads, box_render_upload_t)->size,
        65 * sizeof(box_render_instance_t));
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 65);

    ecs_fini(world);
    box_render_null_free(null);
}
