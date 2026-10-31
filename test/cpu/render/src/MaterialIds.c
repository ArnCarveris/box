/**
 * @file MaterialIds.c
 * @brief Tests for the material table: which prefabs and shapes get a slot, which shapes share
 * one by content, what a slot is packed from, and what a material change uploads.
 *
 * Every case renders with the null backend and reads the counters of the extract, the slots of
 * the material table it keeps and the shading of the mesh instances.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

/** Instances of the cases with many instances */
#define COUNT (1000)

/** Bytes of one record of the material table */
#define MATERIAL ((int64_t)sizeof(box_render_material_t))

/** Bytes of one mesh instance in the shading stream */
#define SHADING ((int64_t)sizeof(box_render_mesh_shading_t))

/** Bytes of one instance in the transform stream */
#define TRANSFORM ((int64_t)sizeof(box_render_transform_t))

/** Create a world that renders with a null backend and has a view.
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
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 * @return The counters of the batches of the frame
 */
static box_render_draws_stats_t frame(
    ecs_world_t *world)
{
    box_render_draws_stats_t stats;
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);

    return stats;
}

/** Get the state of the batches of a world.
 *
 * @param world The world
 * @return The state
 */
static box_render_draws_t* draws_of(
    ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL && s->ctx != NULL);

    return s->ctx;
}

/** Get the shading of the mesh shape of an entity as the extract keeps it.
 *
 * @param world The world
 * @param e The entity
 * @return The shading of its slot
 */
static const box_render_mesh_shading_t* shading_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(draws_of(world), slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);

    return box_draws_slot_mesh_shading(b, slot->slot);
}

/** Get the record of a slot of the material table as the extract packed it.
 *
 * @param world The world
 * @param id The slot
 * @return The record
 */
static const box_render_material_t* record_of(
    ecs_world_t *world,
    uint32_t id)
{
    box_render_draws_t *d = draws_of(world);
    test_assert(id < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id);
}

/** Get the MaterialId an entity owns or inherits.
 *
 * @param world The world
 * @param e The entity
 * @return The slot, UINT32_MAX without a MaterialId
 */
static uint32_t id_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const MaterialId *id = ecs_get(world, e, MaterialId);

    return id ? id->value : UINT32_MAX;
}

/** Pack a color as the material table holds it.
 *
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return The packed color
 */
static uint32_t color_word(
    uint32_t r,
    uint32_t g,
    uint32_t b)
{
    return r | (g << 8) | (b << 16);
}

/** Create a prefab with a box shape.
 *
 * @param world The world
 * @return The prefab
 */
static ecs_entity_t prefab_new(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, result, Box, {1, 1, 1});

    return result;
}

/** Create an instance of a prefab.
 *
 * @param world The world
 * @param prefab The prefab
 * @param x Its x position
 * @return The instance
 */
static ecs_entity_t instance_new(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, result, Position, {x, 0, 0});

    return result;
}

void MaterialIds_prefab_instances_share_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0.5f, .roughness = 0.25f});
    ecs_entity_t instances[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        instances[i] = instance_new(world, prefab, (float)i);
    }

    box_render_draws_stats_t stats = frame(world);
    uint32_t id = id_of(world, prefab);
    test_assert(id != 0 && id != UINT32_MAX);
    test_int(stats.materials, 2);
    test_int(stats.material_bytes, 2 * MATERIAL);
    for (i = 0; i < COUNT; i ++) {
        test_bool(ecs_owns(world, instances[i], MaterialId), false);
        test_uint(id_of(world, instances[i]), id);
        test_uint(shading_of(world, instances[i])->material, id);
    }

    const box_render_material_t *record = record_of(world, id);
    test_uint(record->color, color_word(200, 100, 50));
    test_flt(record->metallic, 0.5f);
    test_flt(record->roughness, 0.25f);

    stats = frame(world);
    test_int(stats.material_bytes, 0);
    test_int(stats.upload_bytes, 0);

    for (i = 0; i < COUNT; i ++) {
        instance_new(world, prefab, (float)(COUNT + i));
    }

    /* The batch outgrows its run: the GPU copies the instances it had, the new ones upload */
    stats = frame(world);
    test_int(stats.materials, 2);
    test_int(stats.material_bytes, 0);
    test_int(stats.transform_bytes, COUNT * TRANSFORM);
    test_int(stats.shading_bytes, COUNT * SHADING);
    test_int(stats.copies, 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_prefab_color_uploads_one_record(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set(world, prefab, PbrMaterial, {.roughness = 0.5f});
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        instance_new(world, prefab, (float)i);
    }

    frame(world);
    frame(world);
    ecs_set(world, prefab, Rgb, {10, 20, 30});
    box_render_draws_stats_t stats = frame(world);
    uint32_t id = id_of(world, prefab);
    test_int(stats.material_bytes, MATERIAL);
    test_int(stats.upload_bytes, 0);
    test_int(stats.scatters, 0);
    test_uint(record_of(world, id)->color, color_word(10, 20, 30));
    test_flt(record_of(world, id)->roughness, 0.5f);

    const box_render_upload_t *found = NULL;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_MATERIALS) {
            test_assert(found == NULL);
            found = u;
        }
    }

    test_assert(found != NULL);
    test_uint(found->offset, id * (uint32_t)MATERIAL);
    test_uint(found->size, (uint32_t)MATERIAL);

    stats = frame(world);
    test_int(stats.material_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_override_gets_own_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0.25f, .roughness = 0.5f});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    frame(world);
    uint32_t shared = id_of(world, prefab);

    ecs_set(world, a, Rgb, {0, 255, 0});
    box_render_draws_stats_t stats = frame(world);
    test_bool(ecs_owns(world, a, MaterialId), true);
    uint32_t own = id_of(world, a);
    test_assert(own != shared && own != 0 && own != UINT32_MAX);
    test_int(stats.materials, 3);
    test_int(stats.material_bytes, MATERIAL);
    test_uint(shading_of(world, a)->material, own);
    test_uint(shading_of(world, b)->material, shared);
    test_uint(record_of(world, own)->color, color_word(0, 255, 0));
    test_flt(record_of(world, own)->metallic, 0.25f);
    test_flt(record_of(world, own)->roughness, 0.5f);

    ecs_set(world, prefab, PbrMaterial, {.metallic = 0.25f, .roughness = 0.75f});
    stats = frame(world);
    test_int(stats.material_bytes, 2 * MATERIAL);
    test_int(stats.upload_bytes, 0);
    test_flt(record_of(world, own)->roughness, 0.75f);
    test_flt(record_of(world, shared)->roughness, 0.75f);

    ecs_set(world, prefab, Rgb, {10, 20, 30});
    stats = frame(world);
    test_int(stats.material_bytes, MATERIAL);
    test_uint(record_of(world, own)->color, color_word(0, 255, 0));
    test_uint(record_of(world, shared)->color, color_word(10, 20, 30));

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_override_slot_freed_and_reused(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    ecs_entity_t c = instance_new(world, prefab, 2);
    frame(world);
    uint32_t shared = id_of(world, prefab);

    ecs_set(world, a, Rgb, {0, 255, 0});
    box_render_draws_stats_t stats = frame(world);
    uint32_t own = id_of(world, a);
    test_int(stats.materials, 3);

    ecs_remove(world, a, MaterialId);
    stats = frame(world);
    test_int(stats.materials, 2);
    test_uint(id_of(world, a), shared);
    test_uint(shading_of(world, a)->material, shared);

    ecs_set(world, b, Emissive, {.strength = 1, .color = {255, 0, 0}});
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, b), own);
    test_uint(shading_of(world, b)->material, own);
    test_uint(record_of(world, own)->color, color_word(200, 100, 50));
    test_assert(record_of(world, own)->emissive[0] > 0.0f);

    ecs_delete(world, b);
    stats = frame(world);
    test_int(stats.materials, 2);

    ecs_set(world, c, PbrMaterial, {.roughness = 0.5f});
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, c), own);
    test_uint(shading_of(world, c)->material, own);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_nested_prefab_own_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t p1 = prefab_new(world);
    ecs_set(world, p1, Rgb, {200, 100, 50});
    ecs_entity_t p2 = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, p2, EcsIsA, p1);
    ecs_set(world, p2, Emissive, {.strength = 2, .color = {0, 0, 255}});
    ecs_entity_t p3 = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, p3, EcsIsA, p1);
    ecs_entity_t i2 = instance_new(world, p2, 0);
    ecs_entity_t i3 = instance_new(world, p3, 1);
    box_render_draws_stats_t stats = frame(world);
    uint32_t id1 = id_of(world, p1), id2 = id_of(world, p2);
    test_int(stats.materials, 3);
    test_assert(id2 != id1 && id2 != 0 && id2 != UINT32_MAX);
    test_bool(ecs_owns(world, p3, MaterialId), false);
    test_uint(id_of(world, p3), id1);
    test_uint(shading_of(world, i2)->material, id2);
    test_uint(shading_of(world, i3)->material, id1);
    test_uint(record_of(world, id2)->color, color_word(200, 100, 50));
    test_assert(record_of(world, id2)->emissive[2] > 1.0f);
    test_flt(record_of(world, id1)->emissive[2], 0);

    ecs_set(world, p1, Rgb, {10, 20, 30});
    stats = frame(world);
    test_int(stats.material_bytes, 2 * MATERIAL);
    test_int(stats.upload_bytes, 0);
    test_uint(record_of(world, id1)->color, color_word(10, 20, 30));
    test_uint(record_of(world, id2)->color, color_word(10, 20, 30));

    ecs_set(world, p2, Emissive, {.strength = 1, .color = {0, 255, 0}});
    stats = frame(world);
    test_int(stats.material_bytes, MATERIAL);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_plain_entity_own_slot_keeps_it_with_base(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {0, 255, 0});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {2, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    box_render_draws_stats_t stats = frame(world);
    uint32_t own = id_of(world, e);
    test_int(stats.materials, 2);
    test_assert(own != 0 && own != UINT32_MAX);
    test_uint(shading_of(world, e)->material, own);
    test_uint(id_of(world, plain), UINT32_MAX);
    test_uint(shading_of(world, plain)->material, 0);
    test_uint(record_of(world, 0)->color, color_word(204, 204, 204));
    test_flt(record_of(world, 0)->roughness, 1);
    test_flt(record_of(world, own)->roughness, 1);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, PbrMaterial, {.metallic = 1, .roughness = 0.25f});
    ecs_set(world, prefab, Rgb, {255, 0, 0});
    frame(world);
    uint32_t base = id_of(world, prefab);
    test_assert(base != own);

    ecs_add_pair(world, e, EcsIsA, prefab);
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, e), own);
    test_uint(shading_of(world, e)->material, own);
    test_int(stats.material_bytes, MATERIAL);
    test_uint(record_of(world, own)->color, color_word(0, 255, 0));
    test_flt(record_of(world, own)->metallic, 1);
    test_flt(record_of(world, own)->roughness, 0.25f);

    ecs_remove_pair(world, e, EcsIsA, prefab);
    stats = frame(world);
    test_uint(id_of(world, e), own);
    test_flt(record_of(world, own)->roughness, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_prefab_delete_frees_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, a, Rgb, {200, 100, 50});
    ecs_entity_t b = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, b, Rgb, {50, 100, 200});
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.materials, 3);
    uint32_t id = id_of(world, a);

    ecs_delete(world, a);
    stats = frame(world);
    test_int(stats.materials, 2);

    ecs_entity_t c = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, c, PbrMaterial, {.roughness = 0.5f});
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, c), id);
    test_flt(record_of(world, id)->roughness, 0.5f);
    test_uint(record_of(world, id)->color, color_word(204, 204, 204));

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_script_reload_keeps_slot_count(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    const char *code =
        "using box.material\n"
        "using box.geometry\n"
        "using box.transform\n"
        "prefab Red {\n"
        "  Box: {1, 1, 1}\n"
        "  Rgb: {255, 0, 0}\n"
        "}\n"
        "prefab Shiny : Red {\n"
        "  PbrMaterial: {metallic: 1, roughness: 0.2}\n"
        "}\n"
        "a : Red { Position: {0, 0, 0} }\n"
        "b : Shiny { Position: {2, 0, 0} }\n";
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = code
    });
    test_assert(script != 0);
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.materials, 3);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        test_assert(ecs_script_update(world, script, 0, code) == 0);
        stats = frame(world);
        test_int(stats.materials, 3);
    }

    ecs_entity_t red = ecs_lookup(world, "Red");
    ecs_entity_t b = ecs_lookup(world, "b");
    test_assert(red != 0 && b != 0);
    test_uint(shading_of(world, b)->material, id_of(world, ecs_lookup(world, "Shiny")));
    test_uint(shading_of(world, ecs_lookup(world, "a"))->material, id_of(world, red));

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_tinted_instance_keeps_prefab_id(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_entity_t plain = instance_new(world, prefab, 0);
    ecs_entity_t tinted = instance_new(world, prefab, 1);
    ecs_set(world, tinted, Tint, {255, 0, 0, 255});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Tint, {0, 0, 255, 128});
    ecs_entity_t child = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, child, EcsChildOf, parent);
    box_render_draws_stats_t stats = frame(world);
    uint32_t id = id_of(world, prefab);
    test_int(stats.materials, 2);

    const box_render_mesh_shading_t *s = shading_of(world, plain);
    test_uint(s->material, id);
    test_uint(s->tint[0], 0);
    test_uint(s->tint[1], 0xFFFFu << 16);
    test_flt(s->alpha, 1);

    s = shading_of(world, tinted);
    test_bool(ecs_owns(world, tinted, MaterialId), false);
    test_uint(s->material, id);
    test_uint(s->tint[0], 0xFFFFu);
    test_uint(s->tint[1], 0);

    s = shading_of(world, child);
    test_uint(s->material, id);
    test_uint(s->tint[0], 0);
    test_uint(s->tint[1] & 0xFFFFu, (uint32_t)(128.0f / 255.0f * 65535.0f + 0.5f));
    test_uint(s->tint[1] >> 16, (uint32_t)((1.0f - 128.0f / 255.0f) * 65535.0f + 0.5f));

    ecs_set(world, tinted, Tint, {0, 255, 0, 255});
    stats = frame(world);
    test_int(stats.material_bytes, 0);
    test_int(stats.shading_bytes > 0, true);
    test_uint(shading_of(world, tinted)->tint[0], 0xFFFFu << 16);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_lights_2d_texts_get_no_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 2, 0});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 10});
    ecs_set(world, light, Rgb, {255, 200, 100});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {10, 10});
    ecs_set(world, rect, Rgb, {0, 255, 0});
    ecs_entity_t text = ecs_new(world);
    ecs_set(world, text, Position, {0, 1, 0});
    ecs_set(world, text, Text, {.value = "material"});
    ecs_set(world, text, Rgb, {255, 255, 255});
    box_render_draws_stats_t stats = frame(world);
    frame(world);
    stats = frame(world);
    test_int(stats.materials, 1);
    test_bool(ecs_owns(world, light, MaterialId), true);
    test_uint(id_of(world, light), UINT32_MAX);
    test_uint(id_of(world, rect), UINT32_MAX);
    test_uint(id_of(world, text), UINT32_MAX);

    ecs_set(world, rect, Rgb, {255, 0, 0});
    stats = frame(world);
    test_int(stats.materials, 1);
    test_int(stats.material_bytes, 0);
    test_assert(stats.shading_bytes > 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_dynamic_material_own_slot_repacks(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {200, 100, 50});
    ecs_add_id(world, e, DynamicMaterial);
    frame(world);
    uint32_t own = id_of(world, e);
    test_assert(own != 0 && own != UINT32_MAX);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_get_mut(world, e, Rgb)->g = (uint8_t)(10 * i);
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.material_bytes, MATERIAL);
        test_int(stats.shading_bytes, SHADING);
        test_uint(record_of(world, own)->color, color_word(200, 10 * (uint32_t)i, 50));
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_dynamic_material_instance_uploads_shading(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_entity_t inst = instance_new(world, prefab, 0);
    ecs_add_id(world, inst, DynamicMaterial);
    frame(world);
    test_bool(ecs_owns(world, inst, MaterialId), false);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.material_bytes, 0);
        test_int(stats.shading_bytes, SHADING);
        test_int(stats.materials, 2);
        test_uint(shading_of(world, inst)->material, id_of(world, prefab));
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** Get the size the last packet created the material table with.
 *
 * @param null The backend
 * @return The bytes of the buffer, 0 when the packet did not create it
 */
static uint64_t table_created(
    box_render_null_t *null)
{
    uint64_t result = 0;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_CREATE &&
            r->handle == BOX_RENDER_BUFFER_MATERIALS)
        {
            result = ((const box_render_buffer_desc_t*)box_render_packet_data(
                &null->last, r->data))->size;
        }
    }

    return result;
}

void MaterialIds_table_grows(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    instance_new(world, prefab, 0);
    box_render_draws_stats_t stats = frame(world);
    test_int(table_created(null), BOX_RENDER_MATERIALS_MIN * MATERIAL);
    test_int(stats.material_bytes, 2 * MATERIAL);

    int32_t i;
    for (i = 0; i < 62; i ++) {
        ecs_set(world, ecs_new_w_id(world, EcsPrefab), Rgb, {(uint8_t)i, 0, 0});
    }

    stats = frame(world);
    test_int(stats.materials, 64);
    test_int(table_created(null), 0);
    test_int(stats.material_bytes, 62 * MATERIAL);

    ecs_entity_t last = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, last, Rgb, {1, 2, 3});
    stats = frame(world);
    test_int(stats.materials, 65);
    test_int(table_created(null), 2 * BOX_RENDER_MATERIALS_MIN * MATERIAL);
    test_int(stats.material_bytes, 65 * MATERIAL);
    test_uint(id_of(world, last), 64);
    test_uint(record_of(world, 64)->color, color_word(1, 2, 3));

    stats = frame(world);
    test_int(table_created(null), 0);
    test_int(stats.material_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Check the Transmission fields of a record.
 *
 * @param record The record
 * @param t The Transmission it was packed from
 */
static void transmission_check(
    const box_render_material_t *record,
    const Transmission *t)
{
    test_flt(record->transmission_factor, t->transmission_factor);
    test_flt(record->ior, t->ior);
    test_flt(record->thickness_factor, t->thickness_factor);
    test_flt(record->attenuation_distance, t->attenuation_distance);
    test_uint(record->attenuation_color, color_word(t->attenuation_color.r,
        t->attenuation_color.g, t->attenuation_color.b));
}

void MaterialIds_transmission_brings_material_id(void) {
    // materials/transmission
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t glass = ecs_new(world);
    ecs_set(world, glass, Box, {1, 1, 1});
    ecs_set(world, glass, Transmission, {.transmission_factor = 0.9f, .ior = 1.2f,
        .thickness_factor = 2, .attenuation_distance = 3, .attenuation_color = {75, 200, 255}});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {2, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    frame(world);

    test_bool(ecs_owns(world, glass, MaterialId), true);
    uint32_t id = id_of(world, glass);
    test_assert(id != 0 && id != UINT32_MAX);
    test_uint(shading_of(world, glass)->material, id);
    transmission_check(record_of(world, id), ecs_get(world, glass, Transmission));
    test_uint(record_of(world, id)->attenuation_color, 75u | (200u << 8) | (255u << 16));

    test_uint(shading_of(world, plain)->material, 0);
    const box_render_material_t *def = record_of(world, 0);
    test_flt(def->transmission_factor, 0);
    test_flt(def->ior, 1.5f);
    test_flt(def->thickness_factor, 0);
    test_flt(def->attenuation_distance, 1e10f);
    test_uint(def->attenuation_color, 0xFFFFFFu);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_transmission_prefab_slot(void) {
    // materials/transmission
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Transmission, {.transmission_factor = 1, .ior = 1.45f,
        .thickness_factor = 0.5f, .attenuation_distance = 2,
        .attenuation_color = {150, 210, 255}});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    box_render_draws_stats_t stats = frame(world);

    uint32_t id = id_of(world, prefab);
    test_assert(id != 0 && id != UINT32_MAX);
    test_int(stats.materials, 2);
    test_bool(ecs_owns(world, a, MaterialId), false);
    test_bool(ecs_owns(world, b, MaterialId), false);
    test_uint(shading_of(world, a)->material, id);
    test_uint(shading_of(world, b)->material, id);
    transmission_check(record_of(world, id), ecs_get(world, prefab, Transmission));

    const DrawSlot *slot = ecs_get(world, a, DrawSlot);
    test_bool(box_draws_batch(draws_of(world), slot->batch)->key.transmissive, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_transmission_override_composes(void) {
    // materials/transmission
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0.25f, .roughness = 0.5f});
    ecs_set(world, prefab, Transmission, {.transmission_factor = 0.5f, .ior = 1.3f,
        .thickness_factor = 1, .attenuation_distance = 4, .attenuation_color = {10, 20, 30}});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    frame(world);
    uint32_t shared = id_of(world, prefab);

    ecs_set(world, a, Transmission, {.transmission_factor = 1, .ior = 1.7f,
        .thickness_factor = 2, .attenuation_distance = 8, .attenuation_color = {40, 50, 60}});
    box_render_draws_stats_t stats = frame(world);
    test_bool(ecs_owns(world, a, MaterialId), true);
    uint32_t own = id_of(world, a);
    test_assert(own != shared && own != 0 && own != UINT32_MAX);
    test_int(stats.materials, 3);
    test_uint(shading_of(world, a)->material, own);
    test_uint(shading_of(world, b)->material, shared);
    const box_render_material_t *record = record_of(world, own);
    transmission_check(record, ecs_get(world, a, Transmission));
    test_uint(record->color, color_word(200, 100, 50));
    test_flt(record->metallic, 0.25f);
    test_flt(record->roughness, 0.5f);
    transmission_check(record_of(world, shared), ecs_get(world, prefab, Transmission));

    ecs_set(world, b, Rgb, {0, 255, 0});
    frame(world);
    uint32_t tinted = id_of(world, b);
    test_assert(tinted != shared && tinted != own);
    test_uint(record_of(world, tinted)->color, color_word(0, 255, 0));
    transmission_check(record_of(world, tinted), ecs_get(world, prefab, Transmission));

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_transmission_prefab_change_reaches_slot(void) {
    // materials/transmission
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, Transmission, {.transmission_factor = 0.5f, .ior = 1.3f,
        .thickness_factor = 1, .attenuation_distance = 4, .attenuation_color = {10, 20, 30}});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    ecs_set(world, b, Rgb, {0, 255, 0});
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, prefab);
    uint32_t own = id_of(world, b);
    test_uint(shading_of(world, a)->material, shared);
    test_assert(own != shared);

    ecs_set(world, prefab, Transmission, {.transmission_factor = 1, .ior = 1.6f,
        .thickness_factor = 3, .attenuation_distance = 5, .attenuation_color = {70, 80, 90}});
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.material_bytes, 2 * MATERIAL);
    test_int(stats.upload_bytes, 0);
    const Transmission *t = ecs_get(world, prefab, Transmission);
    transmission_check(record_of(world, shared), t);
    transmission_check(record_of(world, own), t);
    test_uint(record_of(world, own)->color, color_word(0, 255, 0));

    ecs_remove(world, prefab, Transmission);
    frame(world);
    test_flt(record_of(world, shared)->transmission_factor, 0);
    test_flt(record_of(world, shared)->ior, 1.5f);
    test_uint(record_of(world, shared)->attenuation_color, 0xFFFFFFu);
    test_flt(record_of(world, own)->transmission_factor, 0);
    test_bool(box_draws_batch(draws_of(world),
        ecs_get(world, a, DrawSlot)->batch)->key.transmissive, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_clear_coat_prefab_change_reaches_slot(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t prefab = prefab_new(world);
    ecs_set(world, prefab, ClearCoat, {.strength = 1, .roughness = 0.5f});
    ecs_entity_t a = instance_new(world, prefab, 0);
    ecs_entity_t b = instance_new(world, prefab, 1);
    ecs_set(world, b, Rgb, {0, 255, 0});
    ecs_entity_t c = instance_new(world, prefab, 2);
    ecs_set(world, c, ClearCoat, {.strength = 0.5f, .roughness = 0});
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, prefab);
    uint32_t own = id_of(world, b);
    uint32_t coat = id_of(world, c);
    test_uint(shading_of(world, a)->material, shared);
    test_assert(own != shared);
    test_assert(coat != shared && coat != own);
    test_uint(record_of(world, shared)->clear_coat, 0xFFFFu | (32768u << 16));
    test_uint(record_of(world, own)->clear_coat, 0xFFFFu | (32768u << 16));
    test_uint(record_of(world, coat)->clear_coat, 32768u);
    test_uint(record_of(world, 0)->clear_coat, 0);

    ecs_set(world, prefab, ClearCoat, {.strength = 0.25f, .roughness = 1});
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.material_bytes, 2 * MATERIAL);
    test_int(stats.upload_bytes, 0);
    test_uint(record_of(world, shared)->clear_coat, 16384u | (0xFFFFu << 16));
    test_uint(record_of(world, own)->clear_coat, 16384u | (0xFFFFu << 16));
    test_uint(record_of(world, own)->color, color_word(0, 255, 0));
    test_uint(record_of(world, coat)->clear_coat, 32768u);

    ecs_remove(world, prefab, ClearCoat);
    frame(world);
    test_uint(record_of(world, shared)->clear_coat, 0);
    test_uint(record_of(world, own)->clear_coat, 0);
    test_uint(record_of(world, coat)->clear_coat, 32768u);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_clear_coat_dynamic_material_repacks(void) {
    // materials/material_ids
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicMaterial);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, ClearCoat, {.strength = 0, .roughness = 0});
    frame(world);
    uint32_t id = id_of(world, e);
    test_assert(id != 0 && id != UINT32_MAX);
    test_uint(record_of(world, id)->clear_coat, 0);

    ClearCoat *coat = ecs_get_mut(world, e, ClearCoat);
    coat->strength = 1;
    frame(world);
    test_uint(record_of(world, id)->clear_coat, 0xFFFFu);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create a box shape with a color.
 *
 * @param world The world
 * @param x Its x position
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return The shape
 */
static ecs_entity_t shape_new(
    ecs_world_t *world,
    float x,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {x, 0, 0});
    ecs_set(world, result, Box, {1, 1, 1});
    ecs_set(world, result, Rgb, {r, g, b});

    return result;
}

void MaterialIds_equal_materials_share_slot(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t shapes[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        shapes[i] = shape_new(world, (float)i, 10, 200, 30);
        ecs_set(world, shapes[i], PbrMaterial, {.metallic = 0.5f, .roughness = 0.25f});
    }

    ecs_entity_t plain = shape_new(world, -1, 204, 204, 204);
    box_render_draws_stats_t stats = frame(world);
    uint32_t id = id_of(world, shapes[0]);
    test_assert(id != 0 && id != UINT32_MAX);
    test_int(stats.materials, 2);
    test_int(stats.material_bytes, 2 * MATERIAL);
    for (i = 0; i < COUNT; i ++) {
        test_bool(ecs_owns(world, shapes[i], MaterialId), true);
        test_uint(id_of(world, shapes[i]), id);
        test_uint(shading_of(world, shapes[i])->material, id);
    }

    test_uint(record_of(world, id)->color, color_word(10, 200, 30));
    test_flt(record_of(world, id)->metallic, 0.5f);
    test_flt(record_of(world, id)->roughness, 0.25f);
    test_uint(id_of(world, plain), 0);
    test_uint(shading_of(world, plain)->material, 0);

    stats = frame(world);
    test_int(stats.material_bytes, 0);
    test_int(stats.upload_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_changed_material_takes_other_slot(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t b = shape_new(world, 1, 10, 200, 30);
    ecs_entity_t c = shape_new(world, 2, 10, 200, 30);
    ecs_entity_t d = shape_new(world, 3, 50, 60, 70);
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, a);
    uint32_t other = id_of(world, d);
    test_assert(other != shared);

    ecs_set(world, b, Rgb, {1, 2, 3});
    box_render_draws_stats_t stats = frame(world);
    uint32_t moved = id_of(world, b);
    test_assert(moved != shared && moved != other && moved != 0);
    test_int(stats.materials, 4);
    test_int(stats.material_bytes, MATERIAL);
    test_int(stats.scattered, 1);
    test_int(stats.transform_bytes, 0);
    test_int(stats.surface_bytes, 0);
    test_uint(shading_of(world, b)->material, moved);
    test_uint(record_of(world, moved)->color, color_word(1, 2, 3));
    test_uint(shading_of(world, a)->material, shared);
    test_uint(shading_of(world, c)->material, shared);
    test_uint(id_of(world, a), shared);
    test_uint(id_of(world, c), shared);
    test_uint(record_of(world, shared)->color, color_word(10, 200, 30));

    ecs_set(world, b, Rgb, {50, 60, 70});
    stats = frame(world);
    test_int(stats.materials, 3);
    test_int(stats.material_bytes, 0);
    test_int(stats.scattered, 1);
    test_uint(id_of(world, b), other);
    test_uint(shading_of(world, b)->material, other);
    test_uint(shading_of(world, d)->material, other);

    ecs_set(world, a, Rgb, {90, 90, 90});
    ecs_set(world, a, Rgb, {10, 200, 30});
    stats = frame(world);
    test_int(stats.materials, 3);
    test_int(stats.material_bytes, 0);
    test_int(stats.upload_bytes, 0);
    test_uint(shading_of(world, a)->material, shared);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_alone_changed_material_keeps_slot(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t b = shape_new(world, 1, 50, 60, 70);
    frame(world);
    frame(world);
    uint32_t own = id_of(world, a);

    ecs_set(world, a, Rgb, {1, 2, 3});
    box_render_draws_stats_t stats = frame(world);
    test_uint(id_of(world, a), own);
    test_int(stats.materials, 3);
    test_int(stats.material_bytes, MATERIAL);
    test_int(stats.upload_bytes, 0);
    test_int(stats.scatters, 0);
    test_uint(record_of(world, own)->color, color_word(1, 2, 3));

    ecs_set(world, a, Rgb, {50, 60, 70});
    stats = frame(world);
    test_uint(id_of(world, a), id_of(world, b));
    test_uint(shading_of(world, a)->material, id_of(world, b));
    test_int(stats.materials, 2);
    test_int(stats.material_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_shared_slot_freed_with_last_owner(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t b = shape_new(world, 1, 10, 200, 30);
    ecs_entity_t c = shape_new(world, 2, 10, 200, 30);
    ecs_entity_t keep = shape_new(world, 3, 50, 60, 70);
    box_render_draws_stats_t stats = frame(world);
    uint32_t shared = id_of(world, a);
    test_int(stats.materials, 3);

    ecs_delete(world, a);
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(shading_of(world, b)->material, shared);

    ecs_remove(world, b, MaterialId);
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(shading_of(world, b)->material, 0);
    test_uint(shading_of(world, c)->material, shared);

    ecs_delete(world, c);
    stats = frame(world);
    test_int(stats.materials, 2);
    test_uint(shading_of(world, keep)->material, id_of(world, keep));

    ecs_entity_t e = shape_new(world, 4, 1, 2, 3);
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, e), shared);
    test_uint(shading_of(world, e)->material, shared);
    test_int(stats.material_bytes, MATERIAL);
    test_uint(record_of(world, shared)->color, color_word(1, 2, 3));

    ecs_fini(world);
    box_render_null_free(null);
}

/** Run a frame of a fixed quarter second, which material transitions advance by.
 *
 * @param world The world
 * @return The counters of the batches of the frame
 */
static box_render_draws_stats_t step(
    ecs_world_t *world)
{
    box_render_draws_stats_t stats;
    ecs_progress(world, 0.25f);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);

    return stats;
}

/** Check that shapes render with a slot that holds a color.
 *
 * @param world The world
 * @param shapes The shapes
 * @param count Number of shapes
 * @param slot The slot
 * @param color The packed color of its record
 */
static void sharers_check(
    ecs_world_t *world,
    const ecs_entity_t *shapes,
    int32_t count,
    uint32_t slot,
    uint32_t color)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        test_uint(id_of(world, shapes[i]), slot);
        test_uint(shading_of(world, shapes[i])->material, slot);
    }

    test_uint(record_of(world, slot)->color, color);
}

void MaterialIds_transition_takes_private_slot(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t others[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        others[i] = shape_new(world, (float)i, 200, 100, 50);
    }

    ecs_entity_t e = shape_new(world, 3, 200, 100, 50);
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    step(world);
    box_render_draws_stats_t stats = step(world);
    uint32_t shared = id_of(world, e);
    uint32_t a = color_word(200, 100, 50);
    test_int(stats.materials, 2);
    sharers_check(world, others, 3, shared, a);

    ecs_set(world, e, Rgb, {0, 0, 0});
    stats = step(world);
    uint32_t own = id_of(world, e);
    test_assert(own != shared && own != 0 && own != UINT32_MAX);
    test_uint(shading_of(world, e)->material, own);
    test_int(stats.materials, 3);
    test_int(stats.scattered, 1);
    sharers_check(world, others, 3, shared, a);
    for (i = 0; i < 3; i ++) {
        stats = step(world);
        test_uint(id_of(world, e), own);
        test_uint(shading_of(world, e)->material, own);
        test_int(stats.material_bytes, MATERIAL);
        test_int(stats.scattered, 0);
        sharers_check(world, others, 3, shared, a);
    }

    stats = step(world);
    test_uint(id_of(world, e), own);
    test_uint(shading_of(world, e)->material, own);
    test_int(stats.materials, 3);
    test_uint(record_of(world, own)->color, color_word(0, 0, 0));
    sharers_check(world, others, 3, shared, a);

    stats = step(world);
    test_int(stats.material_bytes, 0);
    test_uint(id_of(world, e), own);

    ecs_set(world, e, Rgb, {200, 100, 50});
    stats = step(world);
    test_uint(id_of(world, e), own);
    test_uint(shading_of(world, e)->material, own);
    test_int(stats.materials, 3);
    sharers_check(world, others, 3, shared, a);
    for (i = 0; i < 3; i ++) {
        step(world);
        test_uint(shading_of(world, e)->material, own);
        sharers_check(world, others, 3, shared, a);
    }

    stats = step(world);
    test_int(stats.materials, 2);
    test_int(stats.scattered, 1);
    test_uint(id_of(world, e), shared);
    test_uint(shading_of(world, e)->material, shared);
    sharers_check(world, others, 3, shared, a);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_dynamic_material_keeps_private_slot(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t still = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t a = shape_new(world, 1, 10, 200, 30);
    ecs_entity_t b = shape_new(world, 2, 10, 200, 30);
    ecs_add_id(world, a, DynamicMaterial);
    ecs_add_id(world, b, DynamicMaterial);
    box_render_draws_stats_t stats = frame(world);
    uint32_t shared = id_of(world, still);
    uint32_t ida = id_of(world, a), idb = id_of(world, b);
    test_assert(ida != shared && idb != shared && ida != idb);
    test_assert(ida != 0 && idb != 0);
    test_int(stats.materials, 4);
    test_uint(shading_of(world, a)->material, ida);
    test_uint(shading_of(world, b)->material, idb);
    test_uint(record_of(world, ida)->color, color_word(10, 200, 30));
    test_uint(record_of(world, idb)->color, color_word(10, 200, 30));

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_get_mut(world, a, Rgb)->r = (uint8_t)(20 * i);
        stats = frame(world);
        test_int(stats.materials, 4);
        test_int(stats.material_bytes, 2 * MATERIAL);
        test_uint(id_of(world, a), ida);
        test_uint(id_of(world, b), idb);
        test_uint(record_of(world, ida)->color, color_word(20 * (uint32_t)i, 200, 30));
        test_uint(record_of(world, shared)->color, color_word(10, 200, 30));
    }

    ecs_remove_id(world, b, DynamicMaterial);
    stats = frame(world);
    test_int(stats.materials, 3);
    test_uint(id_of(world, b), shared);
    test_uint(shading_of(world, b)->material, shared);
    test_uint(shading_of(world, still)->material, shared);
    test_uint(id_of(world, a), ida);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_base_slot_change_reaches_instances(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t other = shape_new(world, 1, 10, 200, 30);
    ecs_entity_t inst = instance_new(world, base, 2);
    ecs_entity_t nested = instance_new(world, inst, 3);
    ecs_set(world, inst, Box, {2, 2, 2});
    ecs_set(world, nested, Box, {3, 3, 3});
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, base);
    test_bool(ecs_owns(world, inst, MaterialId), false);
    test_bool(ecs_owns(world, nested, ShapeMesh), true);
    test_uint(id_of(world, other), shared);
    test_uint(shading_of(world, inst)->material, shared);
    test_uint(shading_of(world, nested)->material, shared);

    ecs_set(world, base, Rgb, {1, 2, 3});
    box_render_draws_stats_t stats = frame(world);
    uint32_t moved = id_of(world, base);
    test_assert(moved != shared && moved != 0);
    test_int(stats.scattered, 3);
    test_uint(shading_of(world, base)->material, moved);
    test_uint(shading_of(world, inst)->material, moved);
    test_uint(shading_of(world, nested)->material, moved);
    test_uint(shading_of(world, other)->material, shared);

    ecs_delete(world, other);
    ecs_set(world, base, Rgb, {10, 200, 30});
    stats = frame(world);
    test_int(stats.materials, 2);
    test_uint(shading_of(world, base)->material, moved);
    test_uint(shading_of(world, inst)->material, moved);
    test_uint(shading_of(world, nested)->material, moved);

    stats = frame(world);
    test_int(stats.scattered, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_prefab_from_shared_slot_reaches_instances(void) {
    // gpu_instance_data/material_prefab_slot
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t inst = instance_new(world, base, 2);
    ecs_entity_t other = shape_new(world, 1, 10, 200, 30);
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, base);
    test_assert(shared != 0 && shared != UINT32_MAX);
    test_uint(id_of(world, other), shared);
    test_uint(shading_of(world, inst)->material, shared);

    ecs_add_id(world, base, EcsPrefab);
    ecs_set(world, base, Rgb, {1, 2, 3});
    frame(world);
    frame(world);
    uint32_t own = id_of(world, base);
    test_assert(own != shared);
    test_uint(shading_of(world, inst)->material, own);
    test_uint(record_of(world, own)->color, color_word(1, 2, 3));
    test_uint(id_of(world, other), shared);
    test_uint(shading_of(world, other)->material, shared);
    test_uint(record_of(world, shared)->color, color_word(10, 200, 30));

    ecs_fini(world);
    box_render_null_free(null);
}

void MaterialIds_prefab_from_shared_slot_same_color(void) {
    // gpu_instance_data/material_prefab_slot
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t inst = instance_new(world, base, 2);
    ecs_entity_t other = shape_new(world, 1, 10, 200, 30);
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, base);
    test_uint(id_of(world, other), shared);
    test_uint(shading_of(world, inst)->material, shared);

    ecs_add_id(world, base, EcsPrefab);
    frame(world);
    frame(world);
    uint32_t own = id_of(world, base);
    test_assert(own != shared);
    test_uint(id_of(world, other), shared);
    test_uint(shading_of(world, inst)->material, own);
    test_uint(record_of(world, own)->color, color_word(10, 200, 30));

    ecs_fini(world);
    box_render_null_free(null);
}

/** Stop the light extract, which takes and clears the shadow regions every frame, and have the
 * draws record them, so a case reads the regions of each frame.
 *
 * @param world The world
 * @return box_render_shadow_region_t, the regions recorded since the case cleared them
 */
static ecs_vec_t* regions_of(
    ecs_world_t *world)
{
    ecs_entity_t lights = ecs_lookup(world, "box.render.RenderExtractLights");
    test_assert(lights != 0);
    ecs_enable(world, lights, false);
    box_render_draws_shadow_record(world, true);
    ecs_vec_t *result = box_render_draws_shadow_regions(world);
    test_assert(result != NULL);

    return result;
}

/** Check that shadow regions were recorded since they were cleared, all left of an x.
 *
 * @param regions The regions
 * @param max_x Largest x a region may reach
 */
static void regions_check(
    const ecs_vec_t *regions,
    float max_x)
{
    int32_t i;
    test_assert(ecs_vec_count(regions) > 0);
    for (i = 0; i < ecs_vec_count(regions); i ++) {
        test_assert(ecs_vec_get_t(regions, box_render_shadow_region_t, i)->max[0] <= max_x);
    }
}

void MaterialIds_shared_cutoff_change_records_shadow(void) {
    // gpu_instance_data/material_dedup
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_vec_t *regions = regions_of(world);
    ecs_entity_t a = shape_new(world, 0, 10, 200, 30);
    ecs_entity_t b = shape_new(world, 6, 10, 200, 30);
    ecs_set(world, a, AlphaCutoff, {0.25f});
    ecs_set(world, b, AlphaCutoff, {0.25f});
    frame(world);
    frame(world);
    uint32_t shared = id_of(world, a);
    test_uint(id_of(world, b), shared);
    ecs_vec_clear(regions);

    ecs_set(world, a, AlphaCutoff, {0.5f});
    frame(world);
    uint32_t own = id_of(world, a);
    test_assert(own != shared);
    regions_check(regions, 3);
    ecs_vec_clear(regions);

    ecs_set(world, a, AlphaCutoff, {0.75f});
    box_render_draws_stats_t stats = frame(world);
    test_uint(id_of(world, a), own);
    test_int(stats.scatters, 0);
    test_flt(record_of(world, own)->cutoff, 0.75f);
    regions_check(regions, 3);
    ecs_vec_clear(regions);

    frame(world);
    test_int(ecs_vec_count(regions), 0);

    ecs_fini(world);
    box_render_null_free(null);
}
