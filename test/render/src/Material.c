/**
 * @file Material.c
 * @brief Tests for the material data of instances, blended batches and their sort order.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Material_default_material(void) {
    // materials/pbr
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
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_int(b->view, -1);
    test_uint(b->blend, Opaque);
    test_uint(b->back_face, BackFaceCull);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->color[3], 1);
    test_flt(inst->model[0][3], 0);
    test_flt(inst->model[1][3], 0.5f);
    test_flt(inst->model[2][3], 0);
    test_flt(inst->normal[0][3], 0);
    test_flt(inst->normal[1][3], 0);
    test_flt(inst->normal[2][3], 0);
    test_assert(fabsf(inst->color[0] - 0.6038f) < 1e-3f);
    test_flt(inst->model[3][0], 1);
    test_flt(inst->model[3][3], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_base_color_linear(void) {
    // materials/pbr
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
    ecs_set(world, e, Rgb, {255, 0, 128});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->color[0], 1);
    test_flt(inst->color[1], 0);
    test_assert(fabsf(inst->color[2] - 0.2158f) < 1e-3f);
    test_flt(inst->color[3], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_pbr_material(void) {
    // materials/pbr
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
    ecs_set(world, e, Position, {4, 5, 6});
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->model[0][3], 1);
    test_flt(inst->model[1][3], 0.25f);
    test_flt(inst->model[0][0], 2);
    test_flt(inst->model[3][0], 4);
    test_flt(inst->model[3][1], 5);
    test_flt(inst->model[3][2], 6);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_pbr_material_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, PbrMaterial);
    ecs_add(world, e, Opacity);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 0.5f);
    test_flt(ecs_get(world, e, Opacity)->value, 1);

    ecs_fini(world);
}

void Material_emissive(void) {
    // materials/pbr
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
    ecs_set(world, e, Emissive, {.strength = 2, .color = {255, 0, 128}});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->normal[0][3], 2);
    test_flt(inst->normal[1][3], 0);
    test_assert(fabsf(inst->normal[2][3] - 0.4317f) < 1e-3f);
    test_flt(inst->normal[0][0], 1);
    test_flt(inst->color[3], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_opacity_blends(void) {
    // materials/pbr
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
    ecs_set(world, e, Position, {0, 0, -5});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Opacity, {0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_int(b->view, 0);
    test_uint(b->blend, AlphaBlend);
    test_uint(b->first_instance, 0);
    test_uint(b->instance_count, 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, b->instances);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->color[3], 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_full_opacity_stays_opaque(void) {
    // materials/pbr
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
    ecs_set(world, a, Opacity, {1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *batch = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_int(batch->view, -1);
    test_uint(batch->blend, Opaque);
    test_uint(batch->instance_count, 2);
    test_int(ecs_get(world, a, DrawSlot)->batch, ecs_get(world, b, DrawSlot)->batch);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_opacity_hierarchy(void) {
    // materials/pbr
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
    ecs_set(world, root, Opacity, {0.5f});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_set(world, child, Opacity, {0.5f});
    ecs_entity_t parented = ecs_new(world);
    ecs_set(world, parented, EcsParent, {root});
    ecs_set(world, parented, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_flt(ecs_get(world, child, DrawSlot)->fade, 0.5f);
    test_flt(ecs_get(world, parented, DrawSlot)->fade, 0.5f);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 2);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->size, 2 * sizeof(box_render_instance_t));
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    int32_t c = ecs_get(world, child, DrawSlot)->slot;
    int32_t q = ecs_get(world, parented, DrawSlot)->slot;
    test_flt(inst[c].color[3], 0.25f);
    test_flt(inst[q].color[3], 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_parent_opacity_updates_children(void) {
    // materials/pbr
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
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_uint(ecs_vec_first_t(&null->last.batches, box_render_batch_t)->blend, Opaque);

    ecs_set(world, root, Opacity, {0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_uint(b->blend, AlphaBlend);
    test_int(b->view, 0);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->color[3], 0.25f);

    ecs_remove(world, root, Opacity);
    ecs_progress(world, 0);
    box_render_sync(world);
    b = ecs_vec_first_t(&null->last.batches, box_render_batch_t);
    test_uint(b->blend, Opaque);
    test_int(b->view, -1);
    test_flt(ecs_get(world, child, DrawSlot)->fade, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blend_mode_batch(void) {
    // materials/pbr
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
    ecs_set(world, a, BlendMode, {Additive});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    const box_render_batch_t *opaque = ecs_vec_get_t(&p->batches, box_render_batch_t, 0);
    const box_render_batch_t *added = ecs_vec_get_t(&p->batches, box_render_batch_t, 1);
    test_uint(opaque->blend, Opaque);
    test_int(opaque->view, -1);
    test_uint(added->blend, Additive);
    test_int(added->view, 0);
    test_assert(ecs_get(world, a, DrawSlot)->batch != ecs_get(world, b, DrawSlot)->batch);

    ecs_set(world, a, BlendMode, {Opaque});
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    test_uint(ecs_vec_first_t(&p->batches, box_render_batch_t)->instance_count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_back_face_mode_batch(void) {
    // materials/pbr
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
    ecs_set(world, a, Quad, {1, 1});
    ecs_set(world, a, BackFaceMode, {BackFacePlain});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Quad, {1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 2);
    int32_t i, plain = 0, cull = 0;
    for (i = 0; i < 2; i ++) {
        const box_render_batch_t *batch = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        test_uint(batch->blend, Opaque);
        test_int(batch->view, -1);
        plain += batch->back_face == BackFacePlain;
        cull += batch->back_face == BackFaceCull;
    }

    test_int(plain, 1);
    test_int(cull, 1);

    ecs_remove(world, a, BackFaceMode);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blended_after_opaque_back_to_front(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    float z[4] = { -3, -9, -7, -5 };
    ecs_entity_t shapes[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {0, 0, z[i]});
        if (i % 2) {
            ecs_set(world, shapes[i], Sphere, {.segments = 8, .radius = 1});
        } else {
            ecs_set(world, shapes[i], Box, {1, 1, 1});
        }

        ecs_set(world, shapes[i], Opacity, {0.5f});
    }

    ecs_entity_t solid = ecs_new(world);
    ecs_set(world, solid, Position, {0, 0, -20});
    ecs_set(world, solid, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 5);
    const box_render_batch_t *first = ecs_vec_get_t(&p->batches, box_render_batch_t, 0);
    test_uint(first->blend, Opaque);
    test_int(first->view, -1);
    int32_t order[4] = { 1, 2, 3, 0 };
    for (i = 0; i < 4; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i + 1);
        const DrawSlot *s = ecs_get(world, shapes[order[i]], DrawSlot);
        test_int(b->view, 0);
        test_uint(b->blend, AlphaBlend);
        test_uint(b->instance_count, 1);
        test_uint(b->first_instance, (uint32_t)s->slot);
        test_uint(b->mesh, ecs_get(world, shapes[order[i]], ShapeMesh)->mesh);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blended_runs_merge(void) {
    // materials/pbr
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
    for (i = 0; i < 6; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, -10.0f + (float)i});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Opacity, {0.5f});
    }

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_int(b->view, 0);
    test_uint(b->first_instance, 0);
    test_uint(b->instance_count, 6);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blended_sorted_per_view(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Camera, {.fov = 60});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 0, -10});
    ecs_set(world, back, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, back, Camera, {.fov = 60});
    ecs_entity_t v1 = ecs_new(world);
    ecs_set(world, v1, View, {.camera = front});
    ecs_entity_t v2 = ecs_new(world);
    ecs_set(world, v2, View, {.camera = back});
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, -2});
    ecs_set(world, near, Box, {1, 1, 1});
    ecs_set(world, near, Opacity, {0.5f});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, -8});
    ecs_set(world, far, Box, {1, 1, 1});
    ecs_set(world, far, Opacity, {0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    test_int(ecs_vec_count(&p->batches), 3);
    uint32_t sn = (uint32_t)ecs_get(world, near, DrawSlot)->slot;
    uint32_t sf = (uint32_t)ecs_get(world, far, DrawSlot)->slot;
    test_uint(sf, sn + 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_int(b[0].view, 0);
    test_uint(b[0].first_instance, sf);
    test_uint(b[0].instance_count, 1);
    test_int(b[1].view, 0);
    test_uint(b[1].first_instance, sn);
    test_uint(b[1].instance_count, 1);
    test_int(b[2].view, 1);
    test_uint(b[2].first_instance, sn);
    test_uint(b[2].instance_count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blended_scene_filter(void) {
    // materials/pbr
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
    ecs_entity_t other = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, other, Box, {1, 1, 1});
    ecs_set(world, other, Opacity, {0.5f});
    ecs_entity_t mine = ecs_new(world);
    ecs_set(world, mine, Box, {1, 1, 1});
    ecs_set(world, mine, Opacity, {0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_uint(b->scene, 0);
    test_int(b->view, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_change_scatters_slot(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[20];
    int32_t i;
    for (i = 0; i < 20; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.uploads), 0);

    ecs_set(world, shapes[7], PbrMaterial, {.metallic = 0.75f, .roughness = 0.1f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->scatters), 1);
    test_uint(ecs_vec_first_t(&p->scatters, box_render_scatter_t)->count, 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, BOX_RENDER_BUFFER_CHANGES);
    const uint32_t *words = box_render_packet_data(p, u->data);
    test_uint(words[4], (uint32_t)ecs_get(world, shapes[7], DrawSlot)->slot);
    const box_render_instance_t *inst = (const box_render_instance_t*)&words[5];
    test_flt(inst->model[0][3], 0.75f);
    test_flt(inst->model[1][3], 0.1f);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.uploads), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_dynamic_material_per_frame(void) {
    // materials/pbr
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
    ecs_add(world, e, DynamicMaterial);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    ecs_set(world, e, Emissive, {.strength = 0, .color = {255, 255, 255}});
    ecs_progress(world, 0);

    PbrMaterial *pbr = ecs_get_mut(world, e, PbrMaterial);
    pbr->metallic = 0.5f;
    pbr->roughness = 0.9f;
    Emissive *emissive = ecs_get_mut(world, e, Emissive);
    emissive->strength = 3;
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->model[0][3], 0.5f);
    test_flt(inst->model[1][3], 0.9f);
    test_flt(inst->normal[0][3], 3);
    test_flt(inst->normal[1][3], 3);
    test_flt(inst->normal[2][3], 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_dynamic_transform_keeps_material(void) {
    // materials/pbr
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
    ecs_add(world, a, DynamicTransform);
    ecs_set(world, a, Position, {1, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, PbrMaterial, {.metallic = 0.25f, .roughness = 0.75f});
    ecs_set(world, a, Emissive, {.strength = 2, .color = {255, 255, 255}});
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, DynamicTransform);
    ecs_add(world, b, DynamicMaterial);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_get_mut(world, a, Position)->y = 3;
    ecs_get_mut(world, b, Position)->y = 4;
    ecs_get_mut(world, a, PbrMaterial)->metallic = 1;
    ecs_get_mut(world, b, PbrMaterial)->metallic = 1;
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->uploads), 1);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    const box_render_instance_t *ia = &inst[ecs_get(world, a, DrawSlot)->slot];
    const box_render_instance_t *ib = &inst[ecs_get(world, b, DrawSlot)->slot];
    test_flt(ia->model[3][1], 3);
    test_flt(ia->model[0][3], 0.25f);
    test_flt(ia->model[1][3], 0.75f);
    test_flt(ia->normal[0][3], 2);
    test_flt(ia->normal[0][0], 1);
    test_flt(ib->model[3][1], 4);
    test_flt(ib->model[0][3], 1);

    ecs_set(world, a, PbrMaterial, {.metallic = 0.5f, .roughness = 0.75f});
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    inst = box_render_packet_data(p, u->data);
    test_flt(inst[ecs_get(world, a, DrawSlot)->slot].model[0][3], 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_dynamic_opacity_moves_batch(void) {
    // materials/pbr
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
    ecs_add(world, parent, DynamicMaterial);
    ecs_set(world, parent, Opacity, {1});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_uint(ecs_vec_first_t(&null->last.batches, box_render_batch_t)->blend, Opaque);

    ecs_get_mut(world, parent, Opacity)->value = 0.5f;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    const box_render_batch_t *b = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_uint(b->blend, AlphaBlend);
    test_int(b->view, 0);
    const box_render_upload_t *u = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(u->buffer, b->instances);
    const box_render_instance_t *inst = box_render_packet_data(p, u->data);
    test_flt(inst->color[3], 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_blended_no_alloc_steady_state(void) {
    // materials/pbr
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
    for (i = 0; i < 200; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 10), 0, -(float)((i * 7) % 50)});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Opacity, {0.5f});
    }

    for (i = 0; i < 10; i ++) {
        ecs_set(world, camera, Position, {0, 0, (float)i});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 50; i ++) {
        ecs_set(world, camera, Position, {0, 0, (float)(i % 10)});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);
    test_assert(ecs_vec_count(&null->last.batches) > 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_gpu_alpha_blend(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t pane = ecs_new(world);
    ecs_set(world, pane, Position, {-1.5f, 0, 0});
    ecs_set(world, pane, Box, {2, 2, 0.1f});
    ecs_set(world, pane, Rgb, {0, 0, 0});
    ecs_set(world, pane, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, pane, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, pane, Opacity, {0.5f});
    ecs_entity_t glow = ecs_new(world);
    ecs_set(world, glow, Position, {1.5f, 0, 0});
    ecs_set(world, glow, Box, {2, 2, 0.1f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, glow, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_set(world, glow, BlendMode, {Additive});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "blend.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/blend.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    unsigned char *red = &px[(16 * 64 + 2) * 4];
    unsigned char *mixed = &px[(16 * 64 + 25) * 4];
    unsigned char *added = &px[(16 * 64 + 39) * 4];
    test_assert(red[0] > 240);
    test_assert(red[2] < 40);
    test_assert(mixed[0] > 150 && mixed[0] < 220);
    test_assert(mixed[2] > 150 && mixed[2] < 220);
    test_assert(mixed[1] < 40);
    test_assert(added[0] > 240);
    test_assert(added[1] > 240);
    test_assert(added[2] < 40);

    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.errors, 0);
    test_int(stats.shader_errors, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_gpu_back_face_plain(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t culled = ecs_new(world);
    ecs_set(world, culled, Position, {-1.5f, 0, 0});
    ecs_set(world, culled, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, culled, Quad, {2, 2});
    ecs_set(world, culled, Rgb, {0, 0, 0});
    ecs_set(world, culled, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, culled, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {1.5f, 0, 0});
    ecs_set(world, plain, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, plain, Quad, {2, 2});
    ecs_set(world, plain, Rgb, {0, 0, 0});
    ecs_set(world, plain, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, plain, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, plain, BackFaceMode, {BackFacePlain});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "back.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/back.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *left = &px[(16 * 64 + 25) * 4];
    unsigned char *right = &px[(16 * 64 + 39) * 4];
    test_int(left[0], 0);
    test_int(left[1], 0);
    test_int(left[2], 0);
    test_assert(right[0] > 240);
    test_assert(right[1] > 240);
    test_assert(right[2] > 240);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
