/**
 * @file MaterialIds.c
 * @brief Tests for the material table: which prefabs and shapes get a slot, what a slot is
 * packed from, and what a material change uploads.
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

    stats = frame(world);
    test_int(stats.materials, 2);
    test_int(stats.material_bytes, 0);
    test_int(stats.transform_bytes, 2 * COUNT * TRANSFORM);
    test_int(stats.shading_bytes, 2 * COUNT * SHADING);

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
