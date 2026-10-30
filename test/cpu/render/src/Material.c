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
    test_uint(record.reserved[0], 0);
    test_uint(record.reserved[1], 0);
    test_uint(record.reserved[2], 0);
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
