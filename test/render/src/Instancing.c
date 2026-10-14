/**
 * @file Instancing.c
 * @brief Tests for the instanced batches: membership, uploads per frame and the scatter pass.
 */

#include <box.h>
#include <bake_test.h>
#include "draw.h"

void Instancing_batch_per_mesh(void) {
    // renderer/instancing
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
    for (i = 0; i < 3; i ++) {
        ecs_set(world, ecs_new(world), Box, {1, 1, (float)i + 1});
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Sphere, {.segments = 8, .radius = 1});
    ecs_set(world, ecs_new(world), Sphere, {.segments = 8, .radius = 3});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    uint32_t sphere = ecs_get(world, s, ShapeMesh)->mesh;
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(b->instance_count, b->mesh == sphere ? 2 : 3);
        test_uint(b->first_instance, 0);
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.batches, 2);
    test_int(stats.instances, 5);
    test_int(stats.dynamic, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_batch_per_scene(void) {
    // renderer/instancing
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
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    int32_t i, found = 0;
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *batch = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(batch->mesh, ecs_get(world, a, ShapeMesh)->mesh);
        if (batch->scene == root) {
            test_uint(batch->instance_count, 2);
            found ++;
        } else {
            test_uint(batch->scene, 0);
            test_uint(batch->instance_count, 1);
            found ++;
        }
    }

    test_int(found, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_add_joins_batch(void) {
    // renderer/instancing
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
    for (i = 0; i < 4; i ++) {
        ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {7, 8, 9});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->resources), 1);
    const box_render_resource_t *r = ecs_vec_first_t(&p->resources, box_render_resource_t);
    test_int(r->kind, BOX_RENDER_BUFFER);
    test_int(r->op, BOX_RENDER_CREATE);
    test_uint(r->handle, BOX_RENDER_BUFFER_CHANGES);
    const box_render_buffer_desc_t *desc = box_render_packet_data(p, r->data);
    test_uint(desc->usage, BOX_RENDER_BUFFER_STORAGE);
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_uint(b->instance_count, 5);
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_int(slot->slot, 4);
    test_int(ecs_vec_count(&p->scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first_t(&p->scatters, box_render_scatter_t);
    test_uint(sc->buffer, b->instances);
    test_uint(sc->changes, BOX_RENDER_BUFFER_CHANGES);
    test_uint(sc->count, 1);
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, BOX_RENDER_BUFFER_CHANGES);
    const uint32_t *words = box_render_packet_data(p, u->data);
    test_uint(words[0], 1);
    test_uint(words[1], sizeof(box_render_instance_t) / 4);
    test_uint(words[4], 4);
    const box_render_instance_t *inst = (const box_render_instance_t*)&words[5];
    test_flt(inst->model[3][0], 7);
    test_flt(inst->model[3][1], 8);
    test_flt(inst->model[3][2], 9);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_remove_swaps_last(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[10];
    int32_t i;
    for (i = 0; i < 10; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, shapes[3], DrawSlot)->slot, 3);
    test_int(ecs_get(world, shapes[9], DrawSlot)->slot, 9);
    ecs_delete(world, shapes[3]);
    test_int(ecs_get(world, shapes[9], DrawSlot)->slot, 3);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 9);
    test_int(ecs_vec_count(&p->scatters), 1);
    test_uint(ecs_vec_first_t(&p->scatters, box_render_scatter_t)->count, 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const uint32_t *words = box_render_packet_data(p, u->data);
    test_uint(words[0], 1);
    test_uint(words[4], 3);
    const box_render_instance_t *inst = (const box_render_instance_t*)&words[5];
    test_flt(inst->model[3][0], 9);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&p->uploads), 0);
    test_int(ecs_vec_count(&p->scatters), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_mesh_change_moves_batch(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    uint32_t box = ecs_get(world, shapes[0], ShapeMesh)->mesh;
    ecs_remove(world, shapes[1], Box);
    ecs_set(world, shapes[1], Sphere, {.segments = 8, .radius = 1});
    uint32_t sphere = ecs_get(world, shapes[1], ShapeMesh)->mesh;
    test_assert(sphere != box);
    ecs_set(world, shapes[2], Box, {2, 3, 4});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(b->instance_count, b->mesh == sphere ? 1 : 4);
    }

    const DrawSlot *s0 = ecs_get(world, shapes[0], DrawSlot);
    const DrawSlot *s1 = ecs_get(world, shapes[1], DrawSlot);
    const DrawSlot *s2 = ecs_get(world, shapes[2], DrawSlot);
    test_assert(s0->batch != s1->batch);
    test_int(s0->batch, s2->batch);
    test_int(s1->slot, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_scene_change_moves_batch(void) {
    // renderer/instancing
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
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->scene, 0);

    ecs_add_pair(world, b, EcsChildOf, root);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&p->batches), 2);
    int32_t i;
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *batch = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(batch->instance_count, 1);
    }

    ecs_set(world, group, Scene, {0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&p->batches), 2);
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *batch = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_assert(batch->scene == root || batch->scene == group);
        test_uint(batch->instance_count, 1);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_unchanged_uploads_nothing(void) {
    // renderer/instancing
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
    for (i = 0; i < 100; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 0, 0});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.upload_bytes, 100 * sizeof(box_render_instance_t));
    test_int(stats.processed, 100);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->uploads), 0);
    test_int(ecs_vec_count(&p->scatters), 0);
    test_int(ecs_vec_count(&p->resources), 0);
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 100);
    box_render_draws_stats(world, &stats);
    test_int(stats.upload_bytes, 0);
    test_int(stats.processed, 0);
    test_int(stats.scattered, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_changed_scatters_slot(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_set(world, shapes[42], Position, {1, 2, 3});
    ecs_set(world, shapes[17], Rgb, {0, 255, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, BOX_RENDER_BUFFER_CHANGES);
    test_uint(u->offset, 0);
    test_uint(u->size, BOX_RENDER_SCATTER_ALIGN * 2);
    test_int(ecs_vec_count(&p->scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first_t(&p->scatters, box_render_scatter_t);
    test_uint(sc->count, 2);
    test_uint(sc->offset, 0);
    test_uint(sc->size, 16 + 2 * (4 + sizeof(box_render_instance_t)));
    const uint32_t *words = box_render_packet_data(p, u->data);
    test_uint(words[0], 2);
    int32_t found = 0;
    for (i = 0; i < 2; i ++) {
        const uint32_t *entry = &words[4 + i * 33];
        const box_render_instance_t *inst = (const box_render_instance_t*)&entry[1];
        if (entry[0] == (uint32_t)ecs_get(world, shapes[42], DrawSlot)->slot) {
            test_flt(inst->model[3][0], 1);
            test_flt(inst->model[3][1], 2);
            test_flt(inst->model[3][2], 3);
            found ++;
        } else if (entry[0] == (uint32_t)ecs_get(world, shapes[17], DrawSlot)->slot) {
            test_flt(inst->color[0], 0);
            test_flt(inst->color[1], 1);
            test_flt(inst->model[3][0], 17);
            found ++;
        }
    }

    test_int(found, 2);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.scattered, 2);
    test_int(stats.upload_bytes, BOX_RENDER_SCATTER_ALIGN * 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_parent_move_updates_children(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, parent);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, i < 2 ? parent : group);
        ecs_set(world, c, Position, {(float)i, 0, 0});
        ecs_set(world, c, Box, {1, 1, 1});
    }

    for (i = 0; i < 60; i ++) {
        ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_set(world, parent, Position, {0, 10, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first_t(&p->scatters, box_render_scatter_t);
    test_uint(sc->count, 4);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const uint32_t *words = box_render_packet_data(p, u->data);
    for (i = 0; i < 4; i ++) {
        const box_render_instance_t *inst = (const box_render_instance_t*)&words[5 + i * 33];
        test_flt(inst->model[3][1], 10);
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.processed, 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_dynamic_uploads_every_frame(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t mover = 0;
    int32_t i;
    for (i = 0; i < 50; i ++) {
        ecs_set(world, ecs_new(world), Box, {1, 1, 1});
    }

    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        mover = e;
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    Position *pos = ecs_get_mut(world, mover, Position);
    pos->y = 5;
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    test_int(ecs_vec_count(&p->scatters), 0);
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->size, 10 * sizeof(box_render_instance_t));
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    const DrawSlot *slot = ecs_get(world, mover, DrawSlot);
    test_flt(inst[slot->slot].model[3][0], 9);
    test_flt(inst[slot->slot].model[3][1], 5);
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(b->instance_count, b->instances == u->buffer ? 10 : 50);
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 50);
    test_int(stats.dynamic, 10);
    test_int(stats.processed, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_dynamic_material_descendants(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t parent = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Rgb, {255, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_add(world, parent, DynamicMaterial);
    ecs_progress(world, 0);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 0);
    test_int(stats.dynamic, 2);

    Rgb *color = ecs_get_mut(world, a, Rgb);
    color->g = 255;
    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst[ecs_get(world, a, DrawSlot)->slot].color[1], 1);

    ecs_remove(world, parent, DynamicMaterial);
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 2);
    test_int(stats.dynamic, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_dynamic_tag_moves_batch(void) {
    // renderer/instancing
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
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    int32_t batch = ecs_get(world, a, DrawSlot)->batch;

    ecs_add(world, a, DynamicTransform);
    ecs_progress(world, 0);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.batches, 2);
    test_int(stats.instances, 1);
    test_int(stats.dynamic, 1);
    test_assert(ecs_get(world, a, DrawSlot)->batch != batch);
    test_int(ecs_get(world, b, DrawSlot)->slot, 0);

    ecs_remove(world, a, DynamicTransform);
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    test_int(stats.batches, 1);
    test_int(stats.instances, 2);
    test_int(stats.dynamic, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_many_changes_upload_all(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[10];
    int32_t i;
    for (i = 0; i < 10; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    for (i = 0; i < 8; i ++) {
        ecs_set(world, shapes[i], Rgb, {0, 0, 255});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->scatters), 0);
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, ecs_vec_first_t(&p->batches, box_render_batch_t)->instances);
    test_uint(u->size, 10 * sizeof(box_render_instance_t));

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_empty_batch_released(void) {
    // renderer/instancing
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
    ecs_set(world, b, Sphere, {.segments = 8, .radius = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t buffer = 0;
    const box_render_packet_t *p = &null->last;
    int32_t i;
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *batch = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        if (batch->mesh == ecs_get(world, a, ShapeMesh)->mesh) {
            buffer = batch->instances;
        }
    }

    test_assert(buffer != 0);
    ecs_delete(world, a);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&p->batches), 1);
    int32_t destroyed = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        destroyed += r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_DESTROY &&
            r->handle == buffer;
    }

    test_int(destroyed, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.batches, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_no_alloc_steady_state(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[1000];
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    for (i = 0; i < 100; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)i, 1, 0});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 1});
    }

    for (i = 0; i < 10; i ++) {
        ecs_set(world, shapes[i % 4], Position, {(float)i, 2, 0});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 100; i ++) {
        ecs_set(world, shapes[i % 4], Position, {(float)i, 3, 0});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);
    test_int(ecs_vec_count(&null->last.scatters), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_gpu_scatter_readback(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16});
    ecs_entity_t shapes[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_set(world, shapes[7], Position, {7, 42, 0});
    ecs_set(world, shapes[50], Rgb, {0, 255, 0});
    ecs_delete(world, shapes[3]);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 3);
    test_int(stats.errors, 0);
    test_int(stats.shader_errors, 0);

    box_render_instance_t *inst = ecs_os_malloc_n(box_render_instance_t, 99);
    test_assert(box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0,
        99 * sizeof(box_render_instance_t), inst));
    int32_t wrong = 0;
    for (i = 0; i < 100; i ++) {
        if (i == 3) {
            continue;
        }

        const DrawSlot *s = ecs_get(world, shapes[i], DrawSlot);
        const box_render_instance_t *v = &inst[s->slot];
        wrong += v->model[3][0] != (float)i;
        wrong += v->model[3][1] != (i == 7 ? 42.0f : 0.0f);
        wrong += (v->color[1] == 1.0f) != (i == 50);
    }

    test_int(wrong, 0);
    test_int(ecs_get(world, shapes[99], DrawSlot)->slot, 3);

    ecs_os_free(inst);
    ecs_fini(world);
}
