/**
 * @file Material.c
 * @brief Tests for the material defaults the draws write into the material table and into the
 * shading of the 2D instances.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <stdio.h>
#include <string.h>

void Material_default_roughness_without_material(void) {
    // materials/pbr
    box_render_shading_t inst = {0};
    box_render_material_t record = {0};
    box_draw_material_t material = {0};
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0);
    test_flt(inst.material[1], 1);
    box_draws_material_pack(&record, &material);
    test_flt(record.metallic, 0);
    test_flt(record.roughness, 1);
    test_uint(record.color, 204u | (204u << 8) | (204u << 16));
    test_flt(record.uv[0], 1);
    test_flt(record.uv[1], 1);

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.3f };
    material.pbr = &pbr;
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0.25f);
    test_flt(inst.material[1], 0.3f);
    box_draws_material_pack(&record, &material);
    test_flt(record.metallic, 0.25f);
    test_flt(record.roughness, 0.3f);
}

void Material_default_roughness_metallic_only(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "materials",
        "using box.material\n"
        "added {}\n"
        "metal { PbrMaterial: {metallic: 1} }\n"
        "rough { PbrMaterial: {roughness: 0.3} }\n", NULL) == 0);

    ecs_entity_t added = ecs_lookup(world, "added");
    ecs_add(world, added, PbrMaterial);
    const PbrMaterial *pbr = ecs_get(world, added, PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 1);

    pbr = ecs_get(world, ecs_lookup(world, "metal"), PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 1);
    test_flt(pbr->roughness, 1);

    pbr = ecs_get(world, ecs_lookup(world, "rough"), PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 0.3f);

    box_render_material_t record = {0};
    box_draws_material_pack(&record, &(box_draw_material_t){
        .pbr = ecs_get(world, ecs_lookup(world, "metal"), PbrMaterial)});
    test_flt(record.metallic, 1);
    test_flt(record.roughness, 1);

    ecs_fini(world);
}

void Material_alpha_cutoff_instance(void) {
    // materials/cutout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, AlphaCutoff);
    test_flt(ecs_get(world, e, AlphaCutoff)->value, 0.5f);
    ecs_fini(world);

    box_render_material_t record = {0};
    AlphaCutoff mid = {0.3f}, high = {2}, low = {-1};
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.cutoff, 0);
    box_draws_material_pack(&record, &(box_draw_material_t){ .cutoff = &mid, .fade = 1 });
    test_flt(record.cutoff, 0.3f);
    box_draws_material_pack(&record, &(box_draw_material_t){ .cutoff = &high, .fade = 1 });
    test_flt(record.cutoff, 1);
    box_draws_material_pack(&record, &(box_draw_material_t){ .cutoff = &low, .fade = 1 });
    test_flt(record.cutoff, 0);

    Shape2D shape = {0};
    test_bool(box_draws_cutout(&mid, NULL), true);
    test_bool(box_draws_cutout(NULL, NULL), false);
    test_bool(box_draws_cutout(&mid, &shape), false);

    BlendMode blend = AlphaBlend;
    BlendMode opaque = Opaque;
    test_int(box_draws_blend(NULL, 0.4f, false), AlphaBlend);
    test_int(box_draws_blend(NULL, 0.4f, true), Opaque);
    test_int(box_draws_blend(&opaque, 0.0f, true), Opaque);
    test_int(box_draws_blend(&blend, 1.0f, true), AlphaBlend);
}

/** Find the batch of a mesh in the last packet of a null backend.
 *
 * @param null The backend
 * @param mesh The mesh handle
 * @return The batch, NULL when the packet has none of the mesh
 */
static const box_render_batch_t* cutout_batch(
    const box_render_null_t *null,
    uint32_t mesh)
{
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->mesh == mesh) {
            result = b;
        }
    }

    return result;
}

/** Add a polygon of a side count, whose mesh is its own.
 *
 * @param world The world
 * @param sides Sides of the polygon
 * @return The shape
 */
static ecs_entity_t cutout_shape(
    ecs_world_t *world,
    int32_t sides)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {(float)sides, 0, 0});
    ecs_set(world, e, NGon, {.sides = sides, .radius = 0.5f});

    return e;
}

/** Get the mesh handle a shape draws with.
 *
 * @param world The world
 * @param e The shape
 * @return The mesh handle
 */
static uint32_t cutout_mesh(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);

    return shape->render_mesh ? shape->render_mesh : shape->mesh;
}

void Material_alpha_cutoff_batches(void) {
    // materials/cutout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    ecs_entity_t plain = cutout_shape(world, 3);
    ecs_entity_t faded = cutout_shape(world, 4);
    ecs_set(world, faded, AlphaCutoff, {0.5f});
    ecs_set(world, faded, Opacity, {0.4f});
    ecs_entity_t blended = cutout_shape(world, 5);
    ecs_set(world, blended, AlphaCutoff, {0.5f});
    ecs_set(world, blended, BlendMode, {AlphaBlend});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_batch_t *b = cutout_batch(null, cutout_mesh(world, plain));
    test_assert(b != NULL);
    test_bool(b->cutout, false);
    test_int(b->blend, Opaque);
    b = cutout_batch(null, cutout_mesh(world, faded));
    test_assert(b != NULL);
    test_bool(b->cutout, true);
    test_int(b->blend, Opaque);
    test_int(b->view, -1);
    test_bool(b->shadow, true);
    test_bool(b->listed, false);
    b = cutout_batch(null, cutout_mesh(world, blended));
    test_assert(b != NULL);
    test_bool(b->cutout, true);
    test_int(b->blend, AlphaBlend);
    test_int(b->view, 0);
    test_bool(b->listed, true);

    ecs_remove(world, faded, AlphaCutoff);
    ecs_add(world, plain, AlphaCutoff);
    ecs_progress(world, 0);
    box_render_sync(world);

    b = cutout_batch(null, cutout_mesh(world, plain));
    test_assert(b != NULL);
    test_bool(b->cutout, true);
    test_int(b->blend, Opaque);
    b = cutout_batch(null, cutout_mesh(world, faded));
    test_assert(b != NULL);
    test_bool(b->cutout, false);
    test_int(b->blend, AlphaBlend);
    test_int(b->view, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_alpha_cutoff_prefab_and_dynamic(void) {
    // materials/cutout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, NGon, {.sides = 6, .radius = 0.5f});
    ecs_set(world, prefab, AlphaCutoff, {0.25f});
    ecs_set(world, prefab, Opacity, {0.5f});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {1, 0, 0});
    ecs_entity_t dynamic = cutout_shape(world, 7);
    ecs_add(world, dynamic, DynamicMaterial);
    ecs_set(world, dynamic, AlphaCutoff, {0.25f});
    ecs_set(world, dynamic, Opacity, {1.0f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const AlphaCutoff *cutoff = ecs_get(world, inst, AlphaCutoff);
    test_assert(cutoff != NULL);
    test_flt(cutoff->value, 0.25f);
    const box_render_batch_t *b = cutout_batch(null, cutout_mesh(world, inst));
    test_assert(b != NULL);
    test_bool(b->cutout, true);
    test_int(b->blend, Opaque);
    b = cutout_batch(null, cutout_mesh(world, dynamic));
    test_assert(b != NULL);
    test_bool(b->cutout, true);

    ecs_get_mut(world, dynamic, Opacity)->value = 0.5f;
    ecs_progress(world, 0);
    box_render_sync(world);
    b = cutout_batch(null, cutout_mesh(world, dynamic));
    test_assert(b != NULL);
    test_bool(b->cutout, true);
    test_int(b->blend, Opaque);
    test_int(b->view, -1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Path of this file in the repository, which locates the repository root */
#define MATERIAL_SOURCE "test/cpu/render/src/"

/** Read a file of the repository into a string.
 *
 * @param file Path of the file in the repository
 * @return The contents, owned by the caller, NULL when the file cannot be read
 */
static char* material_file_read(
    const char *file)
{
    char *result = NULL;
    const char *source = strstr(__FILE__, MATERIAL_SOURCE);
    int32_t root = source ? (int32_t)(source - __FILE__) : 0;
    char *path = flecs_asprintf("%.*s%s", root, __FILE__, file);
    FILE *f = fopen(path, "rb");
    ecs_os_free(path);
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result = ecs_os_malloc((ecs_size_t)size + 1);
    size_t read = fread(result, 1, (size_t)size, f);
    result[read] = '\0';
    fclose(f);
done:
    return result;
}

/** Load an Ore Else material script and place two instances of a prefab back to back, the way
 * the buildings wear them, then check that both draw alpha tested at 0.5 in an opaque cutout
 * batch, so the transparent part of their texture does not draw as a square.
 *
 * @param file Path of the material script in the repository
 * @param name Name of the prefab
 */
static void oreelse_cutout(
    const char *file,
    const char *name)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 4, 4});
    ecs_set(world, camera, Rotation, {-0.785f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    char *text = material_file_read(file);
    test_assert(text != NULL);
    test_assert(ecs_script_run(world, file, text, NULL) == 0);
    ecs_os_free(text);
    ecs_entity_t prefab = ecs_lookup(world, name);
    test_assert(prefab != 0);
    test_assert(ecs_has_id(world, prefab, EcsPrefab));

    ecs_entity_t front = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, front, Position, {0, 0.06f, 0});
    ecs_set(world, front, Rotation, {-1.5708f, 0, 0});
    ecs_entity_t back = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, back, Position, {0, -0.06f, 0});
    ecs_set(world, back, Rotation, {1.5708f, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    ecs_entity_t faces[] = {front, back};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        const PbrTextures *textures = ecs_get(world, faces[i], PbrTextures);
        test_assert(textures != NULL);
        test_assert(textures->albedo != 0);
        const AlphaCutoff *cutoff = ecs_get(world, faces[i], AlphaCutoff);
        test_assert(cutoff != NULL);
        test_flt(cutoff->value, 0.5f);
        const box_render_batch_t *b = cutout_batch(null, cutout_mesh(world, faces[i]));
        test_assert(b != NULL);
        test_bool(b->cutout, true);
        test_int(b->blend, Opaque);
        test_bool(b->shadow, true);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/* The fan faces of the factory, round blades on a transparent background */
void Material_oreelse_fan_cutout(void) {
    oreelse_cutout("test/assets/materials/fan.flecs", "FanDisc");
}

/* The landing marking of the drone pad, a ring transparent inside and out */
void Material_oreelse_bay_cutout(void) {
    oreelse_cutout("test/assets/materials/drone_bay.flecs", "BayPad");
}

void Material_transmission_defaults(void) {
    // materials/transmission
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "transmission",
        "using box.material\n"
        "glass { Transmission: {transmission_factor: 0.9, ior: 1.33} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Transmission);
    const Transmission *t = ecs_get(world, added, Transmission);
    test_assert(t != NULL);
    test_flt(t->transmission_factor, 0);
    test_flt(t->ior, 1.5f);
    test_flt(t->thickness_factor, 0);
    test_flt(t->attenuation_distance, 1e10f);
    test_int(t->attenuation_color.r, 255);
    test_int(t->attenuation_color.g, 255);
    test_int(t->attenuation_color.b, 255);

    t = ecs_get(world, ecs_lookup(world, "glass"), Transmission);
    test_assert(t != NULL);
    test_flt(t->transmission_factor, 0.9f);
    test_flt(t->ior, 1.33f);
    test_flt(t->thickness_factor, 0);
    test_flt(t->attenuation_distance, 1e10f);
    test_int(t->attenuation_color.r, 255);
    test_int(t->attenuation_color.g, 255);
    test_int(t->attenuation_color.b, 255);

    ecs_fini(world);
}

/** The record holds the values and the attenuation color of a Transmission and its defaults
 * without one; a transition of another component encodes the record again with them. */
void Material_transmission_pack(void) {
    // materials/transmission
    box_render_material_t record;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.transmission_factor, 0);
    test_flt(record.ior, 1.5f);
    test_flt(record.thickness_factor, 0);
    test_flt(record.attenuation_distance, 1e10f);
    test_uint(record.attenuation_color, 0xFFFFFFu);

    Transmission glass = {
        .transmission_factor = 0.9f,
        .ior = 1.45f,
        .thickness_factor = 0.5f,
        .attenuation_distance = 2,
        .attenuation_color = { 75, 200, 254 }
    };
    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .transmission = &glass, .pbr = &pbr, .fade = 1 });
    test_flt(record.transmission_factor, 0.9f);
    test_flt(record.ior, 1.45f);
    test_flt(record.thickness_factor, 0.5f);
    test_flt(record.attenuation_distance, 2);
    test_uint(record.attenuation_color, 75u | (200u << 8) | (254u << 16));
    test_flt(record.metallic, 0.25f);
    test_flt(record.roughness, 0.125f);
    test_uint(record.color, 204u | (204u << 8) | (204u << 16));

    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .transmission = &glass, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** Count the batches of the extract that hold an entity.
 *
 * @param d The state of the batches
 * @param e The entity
 * @param found Receives the last batch that holds it
 * @return The number of slots of the entity over every batch
 */
static int32_t transmission_slots(
    box_render_draws_t *d,
    ecs_entity_t e,
    box_draw_batch_t **found)
{
    int32_t i, s, result = 0;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (!b->alive) {
            continue;
        }

        for (s = 0; s < ecs_vec_count(&b->entities); s ++) {
            if (*ecs_vec_get_t(&b->entities, ecs_entity_t, s) == e) {
                *found = b;
                result ++;
            }
        }
    }

    return result;
}

/** Get the state of the batches of a world.
 *
 * @param world The world
 * @return The state
 */
static box_render_draws_t* transmission_draws(
    ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL && s->ctx != NULL);

    return s->ctx;
}

/** Where a component of a permutation is: missing, on the instance or on its prefab */
#define TRANSMISSION_NONE (0)
#define TRANSMISSION_OWNED (1)
#define TRANSMISSION_INHERITED (2)

/** Set a component of a permutation where it goes.
 *
 * @param world The world
 * @param where TRANSMISSION_NONE, _OWNED or _INHERITED
 * @param inst The instance
 * @param prefab Its prefab
 * @param id The component
 * @param size Size of the component
 * @param value Value of the component
 */
static void transmission_place(
    ecs_world_t *world,
    int32_t where,
    ecs_entity_t inst,
    ecs_entity_t prefab,
    ecs_id_t id,
    size_t size,
    const void *value)
{
    if (where == TRANSMISSION_OWNED) {
        ecs_set_id(world, inst, id, size, value);
    } else if (where == TRANSMISSION_INHERITED) {
        ecs_set_id(world, prefab, id, size, value);
    }
}

void Material_transmission_override_permutations(void) {
    // materials/transmission
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    Transmission glass = {
        .transmission_factor = 0, .ior = 1.5f, .attenuation_distance = 1e10f,
        .attenuation_color = { 255, 255, 255 }
    };
    BlendMode blend = AlphaBlend;
    Opacity opacity = { 0.5f };
    AlphaCutoff cutoff = { 0.5f };
    Tint tint = { 255, 0, 0, 128 };
    ecs_entity_t shapes[2 * 3 * 3 * 3 * 3];
    int32_t count = 0, t, m, o, c, n;
    for (t = TRANSMISSION_OWNED; t <= TRANSMISSION_INHERITED; t ++) {
        for (m = 0; m < 3; m ++) {
            for (o = 0; o < 3; o ++) {
                for (c = 0; c < 3; c ++) {
                    for (n = 0; n < 3; n ++) {
                        ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
                        ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
                        ecs_set(world, inst, Position, {(float)(count % 10), 0, 0});
                        ecs_set(world, inst, Box, {0.5f, 0.5f, 0.5f});
                        transmission_place(world, t, inst, prefab, ecs_id(Transmission),
                            sizeof(Transmission), &glass);
                        transmission_place(world, m, inst, prefab, ecs_id(BlendMode),
                            sizeof(BlendMode), &blend);
                        transmission_place(world, o, inst, prefab, ecs_id(Opacity),
                            sizeof(Opacity), &opacity);
                        transmission_place(world, c, inst, prefab, ecs_id(AlphaCutoff),
                            sizeof(AlphaCutoff), &cutoff);
                        transmission_place(world, n, inst, prefab, ecs_id(Tint),
                            sizeof(Tint), &tint);
                        shapes[count ++] = inst;
                    }
                }
            }
        }
    }

    int32_t frame;
    for (frame = 0; frame < 2; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_draws_t *d = transmission_draws(world);
        int32_t i;
        for (i = 0; i < count; i ++) {
            box_draw_batch_t *b = NULL;
            test_assert(ecs_get(world, shapes[i], Transmission) != NULL);
            if (transmission_slots(d, shapes[i], &b) != 1) {
                printf("permutation %d is in %d batches\n", i,
                    transmission_slots(d, shapes[i], &b));
            }

            test_int(transmission_slots(d, shapes[i], &b), 1);
            test_assert(b != NULL);
            test_bool(b->key.transmissive, true);
            test_int(b->key.blend, AlphaBlend);
            test_bool(b->key.cutout, false);
            test_bool(b->key.shadow, false);
            test_bool(b->key.bake, false);
            test_bool(box_draws_caster(b), false);
        }
    }

    int32_t i, transmissive = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->transmissive) {
            test_int(b->view, 0);
            test_bool(b->listed, true);
            test_bool(b->cutout, false);
            test_int(b->blend, AlphaBlend);
            transmissive += (int32_t)b->instance_count;
        } else {
            test_assert(b->view != -1 || b->instance_count == 0);
        }
    }

    test_int(transmissive, count);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_transmission_dynamic_material_keeps_batch(void) {
    // materials/transmission
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_add(world, e, DynamicMaterial);
    ecs_set(world, e, Opacity, {0.5f});
    ecs_set(world, e, AlphaCutoff, {0.25f});
    ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1.5f,
        .attenuation_distance = 1, .attenuation_color = {255, 255, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    test_assert(batch >= 0);

    box_render_draws_stats_t stats;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_draws_stats(world, &stats);
    test_int(stats.processed, 0);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);
    test_bool(box_draws_batch(transmission_draws(world), batch)->key.transmissive, true);

    ecs_remove(world, e, Transmission);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_draw_batch_t *b = box_draws_batch(transmission_draws(world),
        ecs_get(world, e, DrawSlot)->batch);
    test_bool(b->key.transmissive, false);
    test_bool(b->key.cutout, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_clear_coat_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "clear_coat",
        "using box.material\n"
        "rough { ClearCoat: {roughness: 0.4} }\n"
        "half { ClearCoat: {strength: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, ClearCoat);
    const ClearCoat *c = ecs_get(world, added, ClearCoat);
    test_assert(c != NULL);
    test_flt(c->strength, 1);
    test_flt(c->roughness, 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    c = ecs_get(world, ecs_lookup(world, "rough"), ClearCoat);
    test_assert(c != NULL);
    test_flt(c->strength, 1);
    test_flt(c->roughness, 0.4f);

    c = ecs_get(world, ecs_lookup(world, "half"), ClearCoat);
    test_assert(c != NULL);
    test_flt(c->strength, 0.5f);
    test_flt(c->roughness, 0);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, ClearCoat, {.strength = 0.25f, .roughness = 0.75f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, ClearCoat), false);
    test_flt(ecs_get(world, instance, ClearCoat)->strength, 0.25f);

    ecs_fini(world);
}

/** The record holds the clear coat as two unorm16 clamped to 0..1 and 0 without one or with a
 * strength of 0, and a transition of another component encodes the record again with it. */
void Material_clear_coat_pack(void) {
    // materials/pbr
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.clear_coat, 0);
    test_uint(record.anisotropy, 0);
    test_uint(record.sheen, 0);
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);
    test_flt(record.diffuse_blur, 0);
    test_uint(record.diffuse_transmission, 0);
    test_flt(record.thickness, 0);
    plain = record;

    ClearCoat full = { .strength = 1, .roughness = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &full, .fade = 1 });
    test_uint(record.clear_coat, 0xFFFFu);

    ClearCoat half = { .strength = 0.5f, .roughness = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &half, .fade = 1 });
    test_uint(record.clear_coat & 0xFFFFu, 32768u);
    test_uint(record.clear_coat >> 16, 16384u);

    ClearCoat out = { .strength = 2, .roughness = -1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &out, .fade = 1 });
    test_uint(record.clear_coat, 0xFFFFu);

    ClearCoat high = { .strength = -1, .roughness = 3 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &high, .fade = 1 });
    test_uint(record.clear_coat, 0);

    ClearCoat bare = { .strength = 0, .roughness = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &bare, .fade = 1 });
    test_uint(record.clear_coat, 0);

    box_draws_material_pack(&record, &(box_draw_material_t){ .clear_coat = &half, .fade = 1 });
    record.clear_coat = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .clear_coat = &half, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .clear_coat = &half, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t a, b;
    box_draws_material(&a, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&b, &(box_draw_material_t){
        .clear_coat = &half, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_shading_t));
}

void Material_specular_anisotropy_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "anisotropy",
        "using box.material\n"
        "turned { SpecularAnisotropy: {rotation: 0.5} }\n"
        "half { SpecularAnisotropy: {strength: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, SpecularAnisotropy);
    const SpecularAnisotropy *a = ecs_get(world, added, SpecularAnisotropy);
    test_assert(a != NULL);
    test_flt(a->strength, BOX_MATERIAL_ANISOTROPY_STRENGTH);
    test_flt(a->rotation, BOX_MATERIAL_ANISOTROPY_ROTATION);
    test_flt(a->strength, 1);
    test_flt(a->rotation, 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    a = ecs_get(world, ecs_lookup(world, "turned"), SpecularAnisotropy);
    test_assert(a != NULL);
    test_flt(a->strength, 1);
    test_flt(a->rotation, 0.5f);
    test_bool(ecs_owns(world, ecs_lookup(world, "turned"), MaterialId), true);

    a = ecs_get(world, ecs_lookup(world, "half"), SpecularAnisotropy);
    test_assert(a != NULL);
    test_flt(a->strength, 0.5f);
    test_flt(a->rotation, 0);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, SpecularAnisotropy, {.strength = 0.25f, .rotation = 0.75f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, SpecularAnisotropy), false);
    test_flt(ecs_get(world, instance, SpecularAnisotropy)->strength, 0.25f);
    test_flt(ecs_get(world, instance, SpecularAnisotropy)->rotation, 0.75f);

    ecs_fini(world);
}

/** The record holds the anisotropy as two unorm16, the strength clamped to 0..1 and the rotation
 * wrapped to 0..2 pi over 2 pi, 0 without one or with a strength of 0, and a transition of
 * another component encodes the record again with it. */
void Material_specular_anisotropy_pack(void) {
    // materials/pbr
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.anisotropy, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 96);

    SpecularAnisotropy full = { .strength = 1, .rotation = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &full, .fade = 1 });
    test_uint(record.anisotropy, 0xFFFFu);

    SpecularAnisotropy half = { .strength = 0.5f, .rotation = GLM_PIf };
    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &half, .fade = 1 });
    test_uint(record.anisotropy & 0xFFFFu, 32768u);
    test_uint(record.anisotropy >> 16, 32768u);

    SpecularAnisotropy negative = { .strength = 2, .rotation = -GLM_PI_2f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .anisotropy = &negative, .fade = 1 });
    test_uint(record.anisotropy & 0xFFFFu, 0xFFFFu);
    test_uint(record.anisotropy >> 16, 49151u);

    SpecularAnisotropy wrapped = { .strength = 1, .rotation = 5.0f * GLM_PI_2f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .anisotropy = &wrapped, .fade = 1 });
    test_uint(record.anisotropy >> 16, 16384u);

    SpecularAnisotropy turn = { .strength = 1, .rotation = 2.0f * GLM_PIf };
    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &turn, .fade = 1 });
    test_uint(record.anisotropy, 0xFFFFu);

    SpecularAnisotropy low = { .strength = -1, .rotation = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &low, .fade = 1 });
    test_uint(record.anisotropy, 0);

    SpecularAnisotropy bare = { .strength = 0, .rotation = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &bare, .fade = 1 });
    test_uint(record.anisotropy, 0);

    box_draws_material_pack(&record, &(box_draw_material_t){ .anisotropy = &half, .fade = 1 });
    record.anisotropy = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .anisotropy = &half, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .anisotropy = &half, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t a, b;
    box_draws_material(&a, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&b, &(box_draw_material_t){
        .anisotropy = &half, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_shading_t));
}

void Material_sheen_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "sheen",
        "using box.material\n"
        "rough { Sheen: {roughness: 0.25} }\n"
        "red { Sheen: {color: {255, 0, 0}} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Sheen);
    const Sheen *s = ecs_get(world, added, Sheen);
    test_assert(s != NULL);
    test_uint(s->color.r, 255);
    test_uint(s->color.g, 255);
    test_uint(s->color.b, 255);
    test_flt(s->roughness, BOX_MATERIAL_SHEEN_ROUGHNESS);
    test_flt(s->roughness, 0.5f);
    test_bool(ecs_owns(world, added, MaterialId), true);

    s = ecs_get(world, ecs_lookup(world, "rough"), Sheen);
    test_assert(s != NULL);
    test_uint(s->color.r, 255);
    test_uint(s->color.g, 255);
    test_uint(s->color.b, 255);
    test_flt(s->roughness, 0.25f);
    test_bool(ecs_owns(world, ecs_lookup(world, "rough"), MaterialId), true);

    s = ecs_get(world, ecs_lookup(world, "red"), Sheen);
    test_assert(s != NULL);
    test_uint(s->color.r, 255);
    test_uint(s->color.g, 0);
    test_uint(s->color.b, 0);
    test_flt(s->roughness, 0.5f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Sheen, {.color = {10, 20, 30}, .roughness = 0.75f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Sheen), false);
    test_uint(ecs_get(world, instance, Sheen)->color.g, 20);
    test_flt(ecs_get(world, instance, Sheen)->roughness, 0.75f);

    ecs_fini(world);
}

/** The record holds the sheen as the display encoded color and the roughness as a unorm8
 * clamped to 0..1, 0 without one or with a black color, and a transition of another component
 * encodes the record again with it. */
void Material_sheen_pack(void) {
    // materials/pbr
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.sheen, 0);
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);
    test_flt(record.diffuse_blur, 0);
    test_uint(record.diffuse_transmission, 0);
    test_flt(record.thickness, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 96);

    Sheen white = { .color = {255, 255, 255}, .roughness = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &white, .fade = 1 });
    test_uint(record.sheen, 0xFFFFFFFFu);

    Sheen mixed = { .color = {0x12, 0x34, 0x56}, .roughness = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &mixed, .fade = 1 });
    test_uint(record.sheen & 0xFFu, 0x12u);
    test_uint((record.sheen >> 8) & 0xFFu, 0x34u);
    test_uint((record.sheen >> 16) & 0xFFu, 0x56u);
    test_uint(record.sheen >> 24, 128u);

    Sheen tight = { .color = {255, 0, 0}, .roughness = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &tight, .fade = 1 });
    test_uint(record.sheen, 0xFFu);

    Sheen high = { .color = {0, 0, 1}, .roughness = 3 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &high, .fade = 1 });
    test_uint(record.sheen, (1u << 16) | (255u << 24));

    Sheen low = { .color = {0, 1, 0}, .roughness = -1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &low, .fade = 1 });
    test_uint(record.sheen, 1u << 8);

    Sheen black = { .color = {0, 0, 0}, .roughness = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &black, .fade = 1 });
    test_uint(record.sheen, 0);

    box_draws_material_pack(&record, &(box_draw_material_t){ .sheen = &mixed, .fade = 1 });
    record.sheen = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .sheen = &mixed, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .sheen = &mixed, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t a, b;
    box_draws_material(&a, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&b, &(box_draw_material_t){
        .sheen = &mixed, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_shading_t));
}

void Material_diffuse_transmission_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "diffuse_transmission",
        "using box.material\n"
        "leaf { DiffuseTransmission: {factor: 0.75} }\n"
        "green { DiffuseTransmission: {color: {0, 255, 0}} }\n"
        "thick { DiffuseTransmission: {thickness: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, DiffuseTransmission);
    const DiffuseTransmission *d = ecs_get(world, added, DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->factor, BOX_MATERIAL_DIFFUSE_TRANSMISSION_FACTOR);
    test_flt(d->factor, 0.5f);
    test_uint(d->color.r, 255);
    test_uint(d->color.g, 255);
    test_uint(d->color.b, 255);
    test_flt(d->thickness, 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    ecs_entity_t leaf = ecs_lookup(world, "leaf");
    d = ecs_get(world, leaf, DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->factor, 0.75f);
    test_uint(d->color.r, 255);
    test_uint(d->color.g, 255);
    test_uint(d->color.b, 255);
    test_flt(d->thickness, 0);
    test_bool(ecs_owns(world, leaf, MaterialId), true);

    d = ecs_get(world, ecs_lookup(world, "green"), DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->factor, 0.5f);
    test_uint(d->color.r, 0);
    test_uint(d->color.g, 255);
    test_uint(d->color.b, 0);
    test_flt(d->thickness, 0);

    d = ecs_get(world, ecs_lookup(world, "thick"), DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->factor, 0.5f);
    test_uint(d->color.g, 255);
    test_flt(d->thickness, 0.5f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, DiffuseTransmission, {
        .factor = 0.25f, .color = {10, 20, 30}, .thickness = 2});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, DiffuseTransmission), false);
    test_flt(ecs_get(world, instance, DiffuseTransmission)->factor, 0.25f);
    test_uint(ecs_get(world, instance, DiffuseTransmission)->color.g, 20);
    test_flt(ecs_get(world, instance, DiffuseTransmission)->thickness, 2);

    ecs_fini(world);
}

/** The record holds the diffuse transmission as the display encoded color and the factor as a
 * unorm8 clamped to 0..1 and the thickness in its own word, 0 without one, in a record of 96
 * bytes, and a transition of another component encodes the record again with it. */
void Material_diffuse_transmission_pack(void) {
    // materials/pbr
    box_render_material_t record, plain;
    test_int(ECS_SIZEOF(box_render_material_t), 96);
    test_int(offsetof(box_render_material_t, diffuse_transmission), 76);
    test_int(offsetof(box_render_material_t, thickness), 80);
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.diffuse_transmission, 0);
    test_flt(record.thickness, 0);
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);
    test_flt(record.diffuse_blur, 0);
    plain = record;

    DiffuseTransmission white = { .factor = 1, .color = {255, 255, 255} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &white, .fade = 1 });
    test_uint(record.diffuse_transmission, 0xFFFFFFFFu);
    test_flt(record.thickness, 0);

    DiffuseTransmission mixed = {
        .factor = 0.5f, .color = {0x12, 0x34, 0x56}, .thickness = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &mixed, .fade = 1 });
    test_uint(record.diffuse_transmission & 0xFFu, 0x12u);
    test_uint((record.diffuse_transmission >> 8) & 0xFFu, 0x34u);
    test_uint((record.diffuse_transmission >> 16) & 0xFFu, 0x56u);
    test_uint(record.diffuse_transmission >> 24, 128u);
    test_flt(record.thickness, 0.25f);

    DiffuseTransmission none = { .factor = 0, .color = {255, 0, 0} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &none, .fade = 1 });
    test_uint(record.diffuse_transmission, 0xFFu);

    DiffuseTransmission high = { .factor = 3, .color = {0, 0, 1}, .thickness = -1 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &high, .fade = 1 });
    test_uint(record.diffuse_transmission, (1u << 16) | (255u << 24));
    test_flt(record.thickness, 0);

    DiffuseTransmission low = { .factor = -1, .color = {0, 1, 0} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &low, .fade = 1 });
    test_uint(record.diffuse_transmission, 1u << 8);

    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &mixed, .fade = 1 });
    record.diffuse_transmission = 0;
    record.thickness = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &mixed, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .diffuse_transmission = &mixed, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t a, b;
    box_draws_material(&a, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&b, &(box_draw_material_t){
        .diffuse_transmission = &mixed, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_shading_t));
}

/** DiffuseTransmission.blur is 0 by default, a script sets it and a partial value keeps the
 * other fields at their defaults; one that sets only another field keeps the blur at 0. */
void Material_diffuse_transmission_blur_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "diffuse_transmission_blur",
        "using box.material\n"
        "frosted { DiffuseTransmission: {blur: 0.25} }\n"
        "leaf { DiffuseTransmission: {factor: 0.75} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, DiffuseTransmission);
    const DiffuseTransmission *d = ecs_get(world, added, DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->blur, 0);

    d = ecs_get(world, ecs_lookup(world, "frosted"), DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->blur, 0.25f);
    test_flt(d->factor, BOX_MATERIAL_DIFFUSE_TRANSMISSION_FACTOR);
    test_uint(d->color.r, 255);
    test_uint(d->color.g, 255);
    test_uint(d->color.b, 255);
    test_flt(d->thickness, 0);

    d = ecs_get(world, ecs_lookup(world, "leaf"), DiffuseTransmission);
    test_assert(d != NULL);
    test_flt(d->factor, 0.75f);
    test_flt(d->blur, 0);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, DiffuseTransmission, {
        .factor = 0.25f, .color = {10, 20, 30}, .blur = 0.5f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_flt(ecs_get(world, instance, DiffuseTransmission)->blur, 0.5f);

    ecs_fini(world);
}

/** The record holds DiffuseTransmission.blur in the word after the thickness, 0 without one
 * and for a negative or non finite blur, in a record of 96 bytes, and a transition of another
 * component encodes the record again with it. */
void Material_diffuse_transmission_blur_pack(void) {
    // materials/pbr
    box_render_material_t record, plain;
    test_int(ECS_SIZEOF(box_render_material_t), 96);
    test_int(offsetof(box_render_material_t, diffuse_blur), 84);
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.diffuse_blur, 0);
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);
    plain = record;

    DiffuseTransmission frosted = {
        .factor = 0.5f, .color = {255, 255, 255}, .thickness = 0.25f, .blur = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &frosted, .fade = 1 });
    test_flt(record.diffuse_blur, 0.125f);
    test_flt(record.thickness, 0.25f);
    test_uint(record.diffuse_transmission >> 24, 128u);
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);

    DiffuseTransmission plain_blur = { .factor = 0.5f, .color = {255, 255, 255} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &plain_blur, .fade = 1 });
    test_flt(record.diffuse_blur, 0);

    DiffuseTransmission negative = { .factor = 0.5f, .color = {255, 255, 255}, .blur = -1 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &negative, .fade = 1 });
    test_flt(record.diffuse_blur, 0);

    DiffuseTransmission nan = { .factor = 0.5f, .color = {255, 255, 255}, .blur = NAN };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &nan, .fade = 1 });
    test_flt(record.diffuse_blur, 0);

    DiffuseTransmission inf = { .factor = 0.5f, .color = {255, 255, 255}, .blur = INFINITY };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &inf, .fade = 1 });
    test_flt(record.diffuse_blur, 0);

    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &frosted, .fade = 1 });
    record.diffuse_transmission = 0;
    record.thickness = 0;
    record.diffuse_blur = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &frosted, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .diffuse_transmission = &frosted, .pbr = &pbr, .fade = 1 },
        BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
    test_flt(encoded.diffuse_blur, 0.125f);
}

void Material_texture_mapping_defaults(void) {
    // materials/texture_mapping
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "texture_mapping",
        "using box.material\n"
        "object { TextureMapping: {mode: MappingObject} }\n"
        "sized { TextureMapping: {mode: MappingObject, size: 2} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, TextureMapping);
    const TextureMapping *m = ecs_get(world, added, TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingUv);
    test_flt(m->size, 1);
    test_bool(ecs_owns(world, added, MaterialId), true);

    m = ecs_get(world, ecs_lookup(world, "object"), TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingObject);
    test_flt(m->size, 1);

    m = ecs_get(world, ecs_lookup(world, "sized"), TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingObject);
    test_flt(m->size, 2);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, TextureMapping, {.mode = MappingObject, .size = 0.5f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, TextureMapping), false);
    test_int(ecs_get(world, instance, TextureMapping)->mode, MappingObject);
    test_flt(ecs_get(world, instance, TextureMapping)->size, 0.5f);

    ecs_fini(world);
}

/** The record holds the mode and size of MappingObject, the record without a mapping for
 * MappingUv, and a transition of another component encodes the record again with them. */
void Material_texture_mapping_pack(void) {
    // materials/texture_mapping
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.mapping, 0);
    test_flt(record.mapping_size, 0);
    plain = record;

    TextureMapping uv = { .mode = MappingUv, .size = 3 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &uv, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureMapping object = { .mode = MappingObject, .size = 2 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &object, .fade = 1 });
    test_uint(record.mapping, MappingObject);
    test_flt(record.mapping_size, 2);
    record.mapping = 0;
    record.mapping_size = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureMapping zero = { .mode = MappingObject, .size = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &zero, .fade = 1 });
    test_uint(record.mapping, MappingObject);
    test_flt(record.mapping_size, 1);

    TextureMapping negative = { .mode = MappingObject, .size = -2 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &negative, .fade = 1 });
    test_flt(record.mapping_size, 1);

    TextureMapping nan = { .mode = MappingObject, .size = NAN };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &nan, .fade = 1 });
    test_flt(record.mapping_size, 1);

    TextureTransform tt = { .scale_x = 2, .scale_y = 3, .offset_x = 0.5f, .offset_y = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .mapping = &object, .uv = &tt, .fade = 1 });
    test_uint(record.mapping, MappingObject);
    test_flt(record.mapping_size, 2);
    test_flt(record.uv[0], 2);
    test_flt(record.uv[1], 3);
    test_flt(record.uv[2], 0.5f);
    test_flt(record.uv[3], 0.25f);

    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .mapping = &object, .uv = &tt, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* mapping_world(
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
 */
static void mapping_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the record of the MaterialId an entity owns or inherits, as the extract packed it.
 *
 * @param world The world
 * @param e The entity
 * @return The record
 */
static const box_render_material_t* mapping_record(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL && s->ctx != NULL);
    box_render_draws_t *d = s->ctx;
    const MaterialId *id = ecs_get(world, e, MaterialId);
    test_assert(id != NULL);
    test_assert(id->value < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id->value);
}

/** Create a box with a color and a texture mapping.
 *
 * @param world The world
 * @param x Its x position
 * @param mapping The mapping, NULL for none
 * @return The box
 */
static ecs_entity_t mapping_box(
    ecs_world_t *world,
    float x,
    const TextureMapping *mapping)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 2, 3});
    ecs_set(world, e, Rgb, {200, 100, 50});
    if (mapping) {
        ecs_set_ptr(world, e, TextureMapping, mapping);
    }

    return e;
}

/** Materials that differ only in the mapping get different slots, an instance shares the slot of
 * its prefab with the mapping, and a change of the mapping reaches the record. */
void Material_texture_mapping_ids(void) {
    // materials/texture_mapping
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    TextureMapping uv = { .mode = MappingUv, .size = 1 };
    TextureMapping object = { .mode = MappingObject, .size = 1 };
    TextureMapping sized = { .mode = MappingObject, .size = 2 };
    ecs_entity_t a = mapping_box(world, 0, NULL);
    ecs_entity_t b = mapping_box(world, 1, &object);
    ecs_entity_t c = mapping_box(world, 2, &sized);
    ecs_entity_t d = mapping_box(world, 3, &uv);
    ecs_entity_t e = mapping_box(world, 4, &object);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, TextureMapping, {.mode = MappingObject, .size = 4});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, instance, Position, {5, 0, 0});
    mapping_frame(world);
    mapping_frame(world);

    uint32_t id_a = ecs_get(world, a, MaterialId)->value;
    uint32_t id_b = ecs_get(world, b, MaterialId)->value;
    uint32_t id_c = ecs_get(world, c, MaterialId)->value;
    uint32_t id_d = ecs_get(world, d, MaterialId)->value;
    uint32_t id_e = ecs_get(world, e, MaterialId)->value;
    test_assert(id_a != id_b);
    test_assert(id_a != id_c);
    test_assert(id_b != id_c);
    test_uint(id_d, id_a);
    test_uint(id_e, id_b);

    test_uint(mapping_record(world, a)->mapping, MappingUv);
    test_flt(mapping_record(world, a)->mapping_size, 0);
    test_uint(mapping_record(world, b)->mapping, MappingObject);
    test_flt(mapping_record(world, b)->mapping_size, 1);
    test_uint(mapping_record(world, c)->mapping, MappingObject);
    test_flt(mapping_record(world, c)->mapping_size, 2);
    test_uint(mapping_record(world, b)->color, 200u | (100u << 8) | (50u << 16));

    test_bool(ecs_owns(world, instance, TextureMapping), false);
    test_uint(ecs_get(world, instance, MaterialId)->value,
        ecs_get(world, prefab, MaterialId)->value);
    test_uint(mapping_record(world, instance)->mapping, MappingObject);
    test_flt(mapping_record(world, instance)->mapping_size, 4);

    ecs_set(world, prefab, TextureMapping, {.mode = MappingObject, .size = 8});
    ecs_set(world, c, TextureMapping, {.mode = MappingObject, .size = 1});
    mapping_frame(world);
    test_flt(mapping_record(world, instance)->mapping_size, 8);
    test_uint(ecs_get(world, c, MaterialId)->value, ecs_get(world, b, MaterialId)->value);

    ecs_remove(world, b, TextureMapping);
    mapping_frame(world);
    test_uint(ecs_get(world, b, MaterialId)->value, ecs_get(world, a, MaterialId)->value);
    test_uint(mapping_record(world, b)->mapping, MappingUv);

    ecs_fini(world);
    box_render_null_free(null);
}

/** MappingWorld and blend parse from a script, blend is 0 by default and instances inherit
 * both. */
void Material_texture_mapping_world_defaults(void) {
    // materials/texture_mapping_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "texture_mapping_world",
        "using box.material\n"
        "world_mapped { TextureMapping: {mode: MappingWorld, size: 2} }\n"
        "blended { TextureMapping: {mode: MappingWorld, blend: 0.25} }\n"
        "object_blended { TextureMapping: {mode: MappingObject, blend: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, TextureMapping);
    const TextureMapping *m = ecs_get(world, added, TextureMapping);
    test_assert(m != NULL);
    test_flt(m->blend, 0);

    m = ecs_get(world, ecs_lookup(world, "world_mapped"), TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingWorld);
    test_flt(m->size, 2);
    test_flt(m->blend, 0);

    m = ecs_get(world, ecs_lookup(world, "blended"), TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingWorld);
    test_flt(m->size, 1);
    test_flt(m->blend, 0.25f);

    m = ecs_get(world, ecs_lookup(world, "object_blended"), TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingObject);
    test_flt(m->blend, 0.5f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, TextureMapping, {.mode = MappingWorld, .size = 3, .blend = 0.5f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, TextureMapping), false);
    test_int(ecs_get(world, instance, TextureMapping)->mode, MappingWorld);
    test_flt(ecs_get(world, instance, TextureMapping)->size, 3);
    test_flt(ecs_get(world, instance, TextureMapping)->blend, 0.5f);

    ecs_fini(world);
}

/** The record holds MappingWorld with its size, and the blend of MappingWorld and MappingObject
 * as a unorm16 above the mode, clamped to 0..1, 0 when not finite; MappingUv drops the blend,
 * and a transition of another component encodes the record again with them. */
void Material_texture_mapping_world_pack(void) {
    // materials/texture_mapping_world
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    plain = record;

    TextureMapping uv = { .mode = MappingUv, .size = 3, .blend = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &uv, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureMapping world = { .mode = MappingWorld, .size = 2 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &world, .fade = 1 });
    test_uint(record.mapping, MappingWorld);
    test_flt(record.mapping_size, 2);
    record.mapping = 0;
    record.mapping_size = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureMapping zero = { .mode = MappingWorld, .size = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &zero, .fade = 1 });
    test_uint(record.mapping, MappingWorld);
    test_flt(record.mapping_size, 1);

    TextureMapping half = { .mode = MappingWorld, .size = 2, .blend = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &half, .fade = 1 });
    test_uint(record.mapping & 0xffffu, MappingWorld);
    test_uint(record.mapping >> 16, 32768);
    test_flt(record.mapping_size, 2);

    TextureMapping object = { .mode = MappingObject, .size = 2, .blend = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &object, .fade = 1 });
    test_uint(record.mapping & 0xffffu, MappingObject);
    test_uint(record.mapping >> 16, 16384);

    TextureMapping full = { .mode = MappingWorld, .blend = 1, .size = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &full, .fade = 1 });
    test_uint(record.mapping, MappingWorld | (65535u << 16));

    TextureMapping above = { .mode = MappingWorld, .blend = 4, .size = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &above, .fade = 1 });
    test_uint(record.mapping, MappingWorld | (65535u << 16));

    TextureMapping below = { .mode = MappingWorld, .blend = -1, .size = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &below, .fade = 1 });
    test_uint(record.mapping, MappingWorld);

    TextureMapping nan = { .mode = MappingWorld, .blend = NAN, .size = 1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &nan, .fade = 1 });
    test_uint(record.mapping, MappingWorld);

    TextureTransform tt = { .scale_x = 2, .scale_y = 3, .offset_x = 0.5f, .offset_y = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .mapping = &half, .uv = &tt, .fade = 1 });
    test_uint(record.mapping & 0xffffu, MappingWorld);
    test_flt(record.uv[0], 2);
    test_flt(record.uv[1], 3);
    test_flt(record.uv[2], 0.5f);
    test_flt(record.uv[3], 0.25f);

    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .mapping = &half, .uv = &tt, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** Materials that differ only in the mapping mode or the blend get different slots, equal ones
 * share a slot, an instance shares the slot of its prefab, and a change of the blend reaches the
 * record. */
void Material_texture_mapping_world_ids(void) {
    // materials/texture_mapping_world
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    TextureMapping object = { .mode = MappingObject, .size = 1 };
    TextureMapping world_mapped = { .mode = MappingWorld, .size = 1 };
    TextureMapping blended = { .mode = MappingWorld, .size = 1, .blend = 0.5f };
    TextureMapping object_blended = { .mode = MappingObject, .size = 1, .blend = 0.5f };
    TextureMapping uv_blended = { .mode = MappingUv, .size = 1, .blend = 0.5f };
    ecs_entity_t a = mapping_box(world, 0, NULL);
    ecs_entity_t b = mapping_box(world, 1, &object);
    ecs_entity_t c = mapping_box(world, 2, &world_mapped);
    ecs_entity_t d = mapping_box(world, 3, &blended);
    ecs_entity_t e = mapping_box(world, 4, &object_blended);
    ecs_entity_t f = mapping_box(world, 5, &uv_blended);
    ecs_entity_t g = mapping_box(world, 6, &blended);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, TextureMapping, {.mode = MappingWorld, .size = 4, .blend = 0.25f});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, instance, Position, {7, 0, 0});
    mapping_frame(world);
    mapping_frame(world);

    uint32_t id_a = ecs_get(world, a, MaterialId)->value;
    uint32_t id_b = ecs_get(world, b, MaterialId)->value;
    uint32_t id_c = ecs_get(world, c, MaterialId)->value;
    uint32_t id_d = ecs_get(world, d, MaterialId)->value;
    uint32_t id_e = ecs_get(world, e, MaterialId)->value;
    uint32_t id_f = ecs_get(world, f, MaterialId)->value;
    uint32_t id_g = ecs_get(world, g, MaterialId)->value;
    test_assert(id_c != id_a);
    test_assert(id_c != id_b);
    test_assert(id_d != id_c);
    test_assert(id_e != id_b);
    test_assert(id_e != id_d);
    test_uint(id_f, id_a);
    test_uint(id_g, id_d);

    test_uint(mapping_record(world, c)->mapping, MappingWorld);
    test_flt(mapping_record(world, c)->mapping_size, 1);
    test_uint(mapping_record(world, d)->mapping, MappingWorld | (32768u << 16));
    test_uint(mapping_record(world, e)->mapping, MappingObject | (32768u << 16));
    test_uint(mapping_record(world, f)->mapping, MappingUv);
    test_uint(mapping_record(world, c)->color, 200u | (100u << 8) | (50u << 16));

    test_uint(ecs_get(world, instance, MaterialId)->value,
        ecs_get(world, prefab, MaterialId)->value);
    test_uint(mapping_record(world, instance)->mapping, MappingWorld | (16384u << 16));
    test_flt(mapping_record(world, instance)->mapping_size, 4);

    ecs_set(world, prefab, TextureMapping, {.mode = MappingWorld, .size = 4, .blend = 1});
    ecs_set(world, d, TextureMapping, {.mode = MappingWorld, .size = 1});
    mapping_frame(world);
    test_uint(mapping_record(world, instance)->mapping, MappingWorld | (65535u << 16));
    test_uint(ecs_get(world, d, MaterialId)->value, ecs_get(world, c, MaterialId)->value);

    ecs_set(world, c, TextureMapping, {.mode = MappingObject, .size = 1});
    mapping_frame(world);
    test_uint(ecs_get(world, c, MaterialId)->value, ecs_get(world, b, MaterialId)->value);
    test_uint(mapping_record(world, c)->mapping, MappingObject);

    ecs_fini(world);
    box_render_null_free(null);
}

/** RGB PNG of 4 by 2 pixels, an image a layer of a TextureBlend samples */
#define BLEND_RGB_PNG "test/assets/textures/rgb.png"

/** Grayscale PNG of 4 by 2 pixels, another image of a layer */
#define BLEND_GRAY_PNG "test/assets/textures/gray.png"

/** RGB mask PNG, the mask of a TextureBlend */
#define BLEND_MASK_PNG "test/assets/textures/mask_rgb.png"

/** Create a headless world with a null backend and a view, whose texture loads complete.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* blend_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
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

/** Create a texture entity that loads an image of the test assets.
 *
 * @param world The world
 * @param path The image, from the repository root
 * @return The texture entity
 */
static ecs_entity_t blend_texture(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexturePath, {.path = ECS_CONST_CAST(char*, path)});

    return result;
}

/** Run frames until no texture of a list is loading, then two more so the backend has them.
 *
 * @param world The world
 * @param textures The texture entities
 * @param count Number of texture entities
 */
static void blend_wait(
    ecs_world_t *world,
    const ecs_entity_t *textures,
    int32_t count)
{
    int32_t frames, i;
    for (frames = 0; frames < 200; frames ++) {
        bool loading = false;
        mapping_frame(world);
        for (i = 0; i < count; i ++) {
            const TextureStatus *status = ecs_get(world, textures[i], TextureStatus);
            loading |= !status || status->state == TextureLoading;
        }

        if (!loading) {
            break;
        }
    }

    mapping_frame(world);
    mapping_frame(world);
}

/** Collect the batches of a mesh in the last packet of a null backend.
 *
 * @param null The backend
 * @param mesh The mesh handle
 * @param out Receives the batches
 * @param max Batches out holds
 * @return The number of batches of the mesh
 */
static int32_t blend_batches(
    const box_render_null_t *null,
    uint32_t mesh,
    const box_render_batch_t **out,
    int32_t max)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->mesh == mesh) {
            if (result < max) {
                out[result] = b;
            }

            result ++;
        }
    }

    return result;
}

/** Get the TextureBlend record of a batch with texture_blend in the last packet.
 *
 * @param null The backend
 * @param b The batch
 * @return The record
 */
static const box_render_texture_blend_t* blend_record(
    const box_render_null_t *null,
    const box_render_batch_t *b)
{
    test_assert(b->texture_blend);

    return box_render_packet_data(&null->last, b->blend_layers);
}

/** Get the handle the backend knows the image of a texture entity by.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The handle, 0 before it was sent
 */
static uint32_t blend_handle(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    return box_textures_handle(box_textures_get(world), tex);
}

/** Shapes with the same TextureBlend share a batch, shapes with and without one do not, and two
 * shapes whose blends differ only in a layer draw in different batches. */
void Material_texture_blend_batches(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = a});
    ecs_entity_t rock = ecs_new(world);
    ecs_set(world, rock, PbrTextures, {.albedo = g, .normal = a});
    ecs_entity_t earth = ecs_new(world);
    ecs_set(world, earth, PbrTextures, {.orm = g});

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, NGon, {.sides = 5, .radius = 0.5f});
    ecs_set(world, prefab, TextureBlend, {.layers = {grass, rock}});
    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, first, Position, {0, 0, 0});
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, second, Position, {1, 0, 0});
    ecs_entity_t plain = cutout_shape(world, 5);

    ecs_entity_t left = cutout_shape(world, 7);
    ecs_set(world, left, TextureBlend, {.layers = {grass, rock}});
    ecs_entity_t right = cutout_shape(world, 7);
    ecs_set(world, right, TextureBlend, {.layers = {grass, earth}});
    ecs_entity_t textures[2] = { a, g };
    blend_wait(world, textures, 2);

    const box_render_batch_t *found[4];
    test_int(blend_batches(null, cutout_mesh(world, first), found, 4), 2);
    const box_render_batch_t *blended = found[0]->texture_blend ? found[0] : found[1];
    const box_render_batch_t *other = found[0]->texture_blend ? found[1] : found[0];
    test_bool(blended->texture_blend, true);
    test_bool(other->texture_blend, false);
    test_uint(blended->instance_count, 2);
    test_uint(other->instance_count, 1);
    test_uint(cutout_mesh(world, plain), cutout_mesh(world, first));

    const box_render_texture_blend_t *r = blend_record(null, blended);
    test_uint(r->count, 2);
    test_uint(r->mask, 0);
    test_assert(blend_handle(world, a) != 0);
    test_assert(blend_handle(world, g) != 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, a));
    test_uint(r->layers[0][BOX_RENDER_BLEND_ORM], 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, g));
    test_uint(r->layers[1][BOX_RENDER_BLEND_NORMAL], blend_handle(world, a));
    test_flt(r->transforms[1][0], 1);
    test_flt(r->transforms[1][1], 1);
    test_flt(r->transforms[1][2], 0);
    test_uint(blended->textures[0], 0);
    test_uint(blended->textures[2], 0);
    test_uint(blended->textures[3], 0);

    test_int(blend_batches(null, cutout_mesh(world, left), found, 4), 2);
    test_bool(found[0]->texture_blend, true);
    test_bool(found[1]->texture_blend, true);
    test_uint(found[0]->instance_count, 1);
    test_uint(found[1]->instance_count, 1);
    const box_render_texture_blend_t *r0 = blend_record(null, found[0]);
    const box_render_texture_blend_t *r1 = blend_record(null, found[1]);
    test_uint(r0->layers[0][BOX_RENDER_BLEND_ALBEDO], r1->layers[0][BOX_RENDER_BLEND_ALBEDO]);
    test_assert(r0->layers[1][BOX_RENDER_BLEND_ORM] != r1->layers[1][BOX_RENDER_BLEND_ORM]);

    ecs_fini(world);
    box_render_null_free(null);
}

/** An instance draws with the TextureBlend of its prefab until it sets its own, and a change of
 * the TextureBlend of the prefab reaches it. */
void Material_texture_blend_prefab(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t mask = blend_texture(world, BLEND_MASK_PNG);
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = a});

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, NGon, {.sides = 6, .radius = 0.5f});
    ecs_set(world, prefab, PbrTextures, {.albedo = a});
    ecs_set(world, prefab, TextureBlend, {.layers = {0, grass}});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {0, 0, 0});
    ecs_entity_t textures[2] = { a, mask };
    blend_wait(world, textures, 2);

    test_bool(ecs_owns(world, inst, TextureBlend), false);
    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, inst), found, 2), 1);
    const box_render_texture_blend_t *r = blend_record(null, found[0]);
    test_uint(r->count, 2);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, a));
    test_uint(r->mask, 0);

    ecs_set(world, prefab, TextureBlend, {.mask = mask, .layers = {grass}});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, inst), found, 2), 1);
    r = blend_record(null, found[0]);
    test_uint(r->count, 1);
    test_uint(r->mask, blend_handle(world, mask));
    test_assert(r->mask != 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, a));

    ecs_remove(world, prefab, TextureBlend);
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, inst), found, 2), 1);
    test_bool(found[0]->texture_blend, false);
    test_uint(found[0]->textures[0], blend_handle(world, a));

    ecs_fini(world);
    box_render_null_free(null);
}

/** A change of the TextureTransform or the PbrTextures of a layer reaches the shapes that blend
 * it, which keep their batch. */
void Material_texture_blend_layer_changes(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = a});
    ecs_entity_t e = cutout_shape(world, 8);
    ecs_set(world, e, TextureBlend, {.layers = {layer}});
    ecs_entity_t textures[2] = { a, g };
    blend_wait(world, textures, 2);

    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    const box_render_texture_blend_t *r = blend_record(null, found[0]);
    test_flt(r->transforms[0][0], 1);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, a));
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;

    ecs_set(world, layer, TextureTransform, {.scale_x = 20, .scale_y = 10, .offset_x = 0.5f});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_flt(r->transforms[0][0], 20);
    test_flt(r->transforms[0][1], 10);
    test_flt(r->transforms[0][2], 0.5f);
    test_flt(r->transforms[0][3], 0);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);

    ecs_set(world, layer, PbrTextures, {.albedo = g, .orm = a});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, g));
    test_uint(r->layers[0][BOX_RENDER_BLEND_ORM], blend_handle(world, a));
    test_uint(r->layers[0][BOX_RENDER_BLEND_NORMAL], 0);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);

    ecs_remove(world, layer, PbrTextures);
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], 0);
    test_uint(r->count, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape with a TextureBlend draws as a plain shape with its own PbrTextures when it is alpha
 * tested, blended or has a triplanar TextureMapping, and blends again without them. */
void Material_texture_blend_plain_fallback(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = a});
    ecs_entity_t cut = cutout_shape(world, 9);
    ecs_set(world, cut, PbrTextures, {.albedo = a});
    ecs_set(world, cut, TextureBlend, {.layers = {layer}});
    ecs_set(world, cut, AlphaCutoff, {0.5f});
    ecs_entity_t faded = cutout_shape(world, 10);
    ecs_set(world, faded, TextureBlend, {.layers = {layer}});
    ecs_set(world, faded, Opacity, {0.5f});
    ecs_set(world, faded, Position, {0, 1, 0});
    ecs_entity_t mapped = cutout_shape(world, 11);
    ecs_set(world, mapped, TextureBlend, {.layers = {layer}});
    ecs_set(world, mapped, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0.5f});
    ecs_entity_t hard = cutout_shape(world, 12);
    ecs_set(world, hard, TextureBlend, {.layers = {layer}});
    ecs_set(world, hard, TextureMapping, {.mode = MappingWorld, .size = 1});
    ecs_entity_t textures[1] = { a };
    blend_wait(world, textures, 1);

    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, cut), found, 2), 1);
    test_bool(found[0]->texture_blend, false);
    test_bool(found[0]->cutout, true);
    test_uint(found[0]->textures[0], blend_handle(world, a));
    test_int(blend_batches(null, cutout_mesh(world, faded), found, 2), 1);
    test_bool(found[0]->texture_blend, false);
    test_int(found[0]->blend, AlphaBlend);
    test_int(blend_batches(null, cutout_mesh(world, mapped), found, 2), 1);
    test_bool(found[0]->texture_blend, false);
    test_int(blend_batches(null, cutout_mesh(world, hard), found, 2), 1);
    test_bool(found[0]->texture_blend, true);

    ecs_remove(world, cut, AlphaCutoff);
    ecs_set(world, mapped, TextureMapping, {.mode = MappingWorld, .size = 1});
    ecs_set(world, hard, TextureMapping, {.mode = MappingObject, .size = 1, .blend = 1});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, cut), found, 2), 1);
    test_bool(found[0]->texture_blend, true);
    test_uint(blend_record(null, found[0])->layers[0][BOX_RENDER_BLEND_ALBEDO],
        blend_handle(world, a));
    test_int(blend_batches(null, cutout_mesh(world, mapped), found, 2), 1);
    test_bool(found[0]->texture_blend, true);
    test_int(blend_batches(null, cutout_mesh(world, hard), found, 2), 1);
    test_bool(found[0]->texture_blend, false);

    ecs_fini(world);
    box_render_null_free(null);
}
