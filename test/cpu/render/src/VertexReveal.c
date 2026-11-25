/**
 * @file VertexReveal.c
 * @brief Tests for the per vertex data of meshes as the extract sends it and the VertexReveal
 * material component: its defaults, the material record, the alpha tested batches of its shapes
 * and a threshold that animates without moving shapes between batches.
 *
 * The extract cases render with the null backend and read the batch keys, the material table
 * and the packets it received.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

/** Create a world that renders into the null backend with a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* reveal_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    ecs_modified(world, view, View);

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 * @param null The backend
 * @return The packet the backend received
 */
static const box_render_packet_t* reveal_frame(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 1.0f / 60.0f);
    box_render_sync(world);

    return &null->last;
}

/** Set a Mesh3 of two separate triangles on an entity, each with its own vertices; with data
 * the vertices of the first triangle have 0 in data0.x and those of the second 1, and data1.w
 * holds the vertex index.
 *
 * @param world The world
 * @param e The entity
 * @param data Whether the mesh has per vertex data
 */
static void reveal_mesh_set(
    ecs_world_t *world,
    ecs_entity_t e,
    bool data)
{
    static const float corners[6][2] = {
        { -1, -1 }, { 0, -1 }, { -1, 1 }, { 0, -1 }, { 1, -1 }, { 1, 1 }
    };
    Mesh3 m = {0};
    int32_t i;
    ecs_vec_init_t(NULL, &m.vertices, vec3, 6);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 6);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (i = 0; i < 6; i ++) {
        glm_vec3_copy((vec3){ corners[i][0], corners[i][1], 0.0f }, p[i]);
    }

    if (data) {
        ecs_vec_init_t(NULL, &m.data0, vec4, 6);
        ecs_vec_set_count_t(NULL, &m.data0, vec4, 6);
        ecs_vec_init_t(NULL, &m.data1, vec4, 6);
        ecs_vec_set_count_t(NULL, &m.data1, vec4, 6);
        vec4 *d0 = ecs_vec_first_t(&m.data0, vec4);
        vec4 *d1 = ecs_vec_first_t(&m.data1, vec4);
        for (i = 0; i < 6; i ++) {
            glm_vec4_copy((vec4){ (float)(i / 3), 0, 0, 0 }, d0[i]);
            glm_vec4_copy((vec4){ 0, 0, 0, (float)i }, d1[i]);
        }
    }

    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.data0, vec4);
    ecs_vec_fini_t(NULL, &m.data1, vec4);
}

/** Add a shape with the two triangle Mesh3.
 *
 * @param world The world
 * @param x Its x position
 * @param data Whether the mesh has per vertex data
 * @return The shape
 */
static ecs_entity_t reveal_shape(
    ecs_world_t *world,
    float x,
    bool data)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    reveal_mesh_set(world, e, data);

    return e;
}

/** Get the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* reveal_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Get the material record of a shape as the extract packed it.
 *
 * @param world The world
 * @param e The shape
 * @return The record
 */
static const box_render_material_t* reveal_record(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = box_draws_state(world);
    const MaterialId *id = ecs_get(world, e, MaterialId);
    test_assert(id != NULL);
    test_assert(id->value < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id->value);
}

/** Find the create of a mesh in a packet.
 *
 * @param packet The packet
 * @param handle The mesh handle
 * @param size Receives the bytes of its description
 * @return Its description, NULL when the packet does not create it
 */
static const box_render_mesh_desc_t* reveal_mesh_desc(
    const box_render_packet_t *packet,
    uint32_t handle,
    uint32_t *size)
{
    const box_render_mesh_desc_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&packet->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_MESH && r->op == BOX_RENDER_CREATE && r->handle == handle) {
            result = (const box_render_mesh_desc_t*)ECS_ELEM(
                ecs_vec_first(&packet->data), 1, (int32_t)r->data);
            *size = r->size;
        }
    }

    return result;
}

void VertexReveal_defaults(void) {
    // materials/vertex_reveal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, VertexReveal);
    const VertexReveal *r = ecs_get(world, e, VertexReveal);
    test_assert(r != NULL);
    test_int(r->channel, 0);
    test_flt(r->threshold, 0);
    test_flt(r->softness, 0);
    test_bool(r->invert, false);
    test_assert(ecs_has_pair(world, ecs_id(VertexReveal), EcsOnInstantiate, EcsInherit));
    test_assert(ecs_has(world, e, MaterialId));
    test_assert(ecs_lookup(world, "box.material.VertexReveal") == ecs_id(VertexReveal));
    ecs_fini(world);
}

/** VertexOcclusion reads the occlusion channel of foliage at full strength by default and is
 * an inherited material component */
void VertexReveal_occlusion_defaults(void) {
    // materials/vertex_occlusion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, VertexOcclusion);
    const VertexOcclusion *o = ecs_get(world, e, VertexOcclusion);
    test_assert(o != NULL);
    test_int(o->channel, BOX_MATERIAL_OCCLUSION_CHANNEL);
    test_int(o->channel, BOX_MESH_DATA_OCCLUSION);
    test_flt(o->strength, 1);
    test_assert(ecs_has_pair(world, ecs_id(VertexOcclusion), EcsOnInstantiate, EcsInherit));
    test_assert(ecs_has(world, e, MaterialId));
    test_assert(ecs_lookup(world, "box.material.VertexOcclusion") == ecs_id(VertexOcclusion));
    ecs_fini(world);
}

/** The record holds the channel of a VertexOcclusion in the flags and its clamped strength,
 * next to a VertexReveal; a channel out of range packs the record of a material without one */
void VertexReveal_occlusion_pack(void) {
    // materials/vertex_occlusion
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.occlusion_strength, 0);
    test_flt(record.wind_pad, 0);
    plain = record;

    VertexOcclusion occlusion = { .channel = 7, .strength = 0.75f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .occlusion = &occlusion,
        .fade = 1 });
    test_flt(record.occlusion_strength, 0.75f);
    test_uint(record.flags, BOX_RENDER_OCCLUSION_PRESENT |
        (7u << BOX_RENDER_OCCLUSION_CHANNEL_SHIFT));

    /* Next to a VertexReveal both are in the flags */
    VertexReveal reveal = { .channel = 4, .threshold = 0.5f };
    occlusion = (VertexOcclusion){ .channel = 2, .strength = 3 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .reveal = &reveal,
        .occlusion = &occlusion, .fade = 1 });
    test_flt(record.occlusion_strength, 1);
    test_flt(record.reveal_threshold, 0.5f);
    test_uint(record.flags, BOX_RENDER_REVEAL_PRESENT | (4u << BOX_RENDER_REVEAL_CHANNEL_SHIFT) |
        BOX_RENDER_OCCLUSION_PRESENT | (2u << BOX_RENDER_OCCLUSION_CHANNEL_SHIFT));

    /* Broken values are clamped, a channel out of range is none */
    occlusion = (VertexOcclusion){ .channel = 0, .strength = NAN };
    box_draws_material_pack(&record, &(box_draw_material_t){ .occlusion = &occlusion,
        .fade = 1 });
    test_flt(record.occlusion_strength, 0);
    test_uint(record.flags, BOX_RENDER_OCCLUSION_PRESENT);
    occlusion = (VertexOcclusion){ .channel = 8, .strength = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .occlusion = &occlusion,
        .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    /* A 2D shape ignores it */
    Shape2D shape2d = {0};
    occlusion = (VertexOcclusion){ .channel = 7, .strength = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .occlusion = &occlusion, .shape2d = &shape2d, .fade = 1 });
    test_uint(record.flags, 0);
    test_flt(record.occlusion_strength, 0);
}

/** The extract sends the per vertex data of a mesh after its surface assets and flags it in the
 * description; a mesh without data sends none */
void VertexReveal_mesh_upload(void) {
    // geometry/vertex_data
    box_render_null_t *null;
    ecs_world_t *world = reveal_world(&null);
    ecs_entity_t plain = reveal_shape(world, -2, false);
    ecs_entity_t data = reveal_shape(world, 2, true);
    uint32_t size = 0;
    uint32_t plain_mesh = ecs_get(world, plain, ShapeMesh)->mesh;
    uint32_t data_mesh = ecs_get(world, data, ShapeMesh)->mesh;
    test_assert(plain_mesh != data_mesh);
    const box_render_packet_t *packet = NULL;
    const box_render_mesh_desc_t *desc = NULL;
    int32_t frame;
    for (frame = 0; frame < 4 && !desc; frame ++) {
        packet = reveal_frame(world, null);
        desc = reveal_mesh_desc(packet, plain_mesh, &size);
    }

    test_assert(desc != NULL);
    test_uint(desc->data, 0);
    test_uint(desc->vertex_count, 6);
    test_uint(size, sizeof(box_render_mesh_desc_t) + 6 * sizeof(box_vertex_t) +
        desc->index_count * sizeof(uint32_t) + desc->surface_asset_count * sizeof(vec4));

    desc = reveal_mesh_desc(packet, data_mesh, &size);
    test_assert(desc != NULL);
    test_uint(desc->data, 1);
    test_uint(desc->vertex_count, 6);
    uint32_t offset = (uint32_t)sizeof(box_render_mesh_desc_t) + 6 * sizeof(box_vertex_t) +
        desc->index_count * sizeof(uint32_t) + desc->surface_asset_count * sizeof(vec4);
    test_uint(size, offset + 6 * BOX_RENDER_DATA_VECS * sizeof(vec4));
    const vec4 *values = (const vec4*)((const uint8_t*)desc + offset);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        const vec4 *v = &values[i * BOX_RENDER_DATA_VECS];
        test_flt(v[0][0], (float)(i / 3));
        test_flt(v[1][3], (float)i);
        /* The mesh has no data2 and data3, so they are zero */
        test_flt(v[2][0], 0);
        test_flt(v[3][3], 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape with a VertexReveal draws in the alpha tested batches of a scene with
 * Scene.alpha_cutout, in its plain batch without it; a shape without one keeps its batch */
void VertexReveal_batches(void) {
    // materials/vertex_reveal
    box_render_null_t *null;
    ecs_world_t *world = reveal_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});
    ecs_entity_t plain = reveal_shape(world, -2, true);
    ecs_entity_t revealed = reveal_shape(world, 2, true);
    ecs_set(world, revealed, VertexReveal, {.channel = 0, .threshold = 0.5f});
    reveal_frame(world, null);
    reveal_frame(world, null);

    test_bool(reveal_batch(world, plain)->key.cutout, false);
    test_bool(reveal_batch(world, revealed)->key.cutout, true);
    test_int(reveal_batch(world, revealed)->key.blend, Opaque);
    const box_render_material_t *r = reveal_record(world, revealed);
    test_flt(r->reveal_threshold, 0.5f);
    test_uint(r->flags, BOX_RENDER_REVEAL_PRESENT);
    test_flt(r->cutoff, 0);

    /* Removed, the shape is no longer alpha tested */
    ecs_remove(world, revealed, VertexReveal);
    reveal_frame(world, null);
    reveal_frame(world, null);
    test_bool(reveal_batch(world, revealed)->key.cutout, false);

    /* Without Scene.alpha_cutout it draws as without it */
    ecs_set(world, revealed, VertexReveal, {.channel = 0, .threshold = 0.5f});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = false});
    reveal_frame(world, null);
    reveal_frame(world, null);
    test_bool(reveal_batch(world, revealed)->key.cutout, false);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Writing the threshold of a VertexReveal that prefab instances inherit changes the record they
 * share, without moving them between batches or slots */
void VertexReveal_threshold_animates(void) {
    // materials/vertex_reveal
    box_render_null_t *null;
    ecs_world_t *world = reveal_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    reveal_mesh_set(world, prefab, true);
    ecs_set(world, prefab, VertexReveal, {.channel = 0, .threshold = 0.1f});
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {2, 0, 0});
    reveal_frame(world, null);
    reveal_frame(world, null);

    const DrawSlot *sa = ecs_get(world, a, DrawSlot);
    const DrawSlot *sb = ecs_get(world, b, DrawSlot);
    int32_t batch_a = sa->batch, slot_a = sa->slot;
    int32_t batch_b = sb->batch, slot_b = sb->slot;
    int32_t batches = ecs_vec_count(&box_draws_state(world)->batches);
    test_int(batch_a, batch_b);
    test_bool(reveal_batch(world, a)->key.cutout, true);
    test_flt(reveal_record(world, a)->reveal_threshold, 0.1f);
    test_flt(reveal_record(world, b)->reveal_threshold, 0.1f);

    int32_t i;
    for (i = 1; i <= 4; i ++) {
        VertexReveal *r = ecs_get_mut(world, prefab, VertexReveal);
        r->threshold = 0.1f + 0.2f * (float)i;
        ecs_modified(world, prefab, VertexReveal);
        reveal_frame(world, null);
        sa = ecs_get(world, a, DrawSlot);
        sb = ecs_get(world, b, DrawSlot);
        test_int(sa->batch, batch_a);
        test_int(sa->slot, slot_a);
        test_int(sb->batch, batch_b);
        test_int(sb->slot, slot_b);
        test_int(ecs_vec_count(&box_draws_state(world)->batches), batches);
        test_flt(reveal_record(world, a)->reveal_threshold, 0.1f + 0.2f * (float)i);
        test_flt(reveal_record(world, b)->reveal_threshold, 0.1f + 0.2f * (float)i);
    }

    ecs_fini(world);
    box_render_null_free(null);
}
