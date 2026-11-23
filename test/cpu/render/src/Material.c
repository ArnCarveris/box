/**
 * @file Material.c
 * @brief Tests for the material defaults the draws write into the material table and into the
 * shading of the 2D instances.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <math.h>
#include <stddef.h>
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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

/** Path of this project in the repository, which locates the repository root */
#define MATERIAL_SOURCE "test/cpu/render"

/** MATERIAL_SOURCE as MSVC names it when bake passes the file with backslashes */
#define MATERIAL_SOURCE_WIN "test\\cpu\\render"

/** Read a file of the repository into a string.
 *
 * @param file Path of the file in the repository
 * @return The contents, owned by the caller, NULL when the file cannot be read
 */
static char* material_file_read(
    const char *file)
{
    char *result = NULL;
    const char *self = __FILE__;
    const char *source = strstr(self, MATERIAL_SOURCE);
    if (!source) {
        source = strstr(self, MATERIAL_SOURCE_WIN);
    }

    int32_t root = source ? (int32_t)(source - self) : 0;
    char *path = flecs_asprintf("%.*s%s", root, self, file);
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
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});
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
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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
    ecs_set(world, e, Position, {0, 0, 0});
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
    test_int(ECS_SIZEOF(box_render_material_t), 272);

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
    test_int(ECS_SIZEOF(box_render_material_t), 272);

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
    test_int(ECS_SIZEOF(box_render_material_t), 272);
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
    test_int(ECS_SIZEOF(box_render_material_t), 272);
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
        "object { texture.Mapping: {mode: MappingObject} }\n"
        "sized { texture.Mapping: {mode: MappingObject, size: 2} }\n", NULL) == 0);

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
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
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
        "world_mapped { texture.Mapping: {mode: MappingWorld, size: 2} }\n"
        "blended { texture.Mapping: {mode: MappingWorld, blend: 0.25} }\n"
        "object_blended { texture.Mapping: {mode: MappingObject, blend: 0.5} }\n", NULL) == 0);

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

/** MappingFrame parses from a script, and its record holds the mode with its size and blend as
 * the other projected mappings do. */
void Material_texture_mapping_frame_pack(void) {
    // materials/texture_mapping_frame
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "texture_mapping_frame",
        "using box.material\n"
        "hull { texture.Frame\n texture.Mapping: {mode: MappingFrame, size: 2, blend: 0.5} }\n",
        NULL) == 0);
    ecs_entity_t hull = ecs_lookup(world, "hull");
    test_assert(ecs_has_id(world, hull, TextureFrame));
    const TextureMapping *m = ecs_get(world, hull, TextureMapping);
    test_assert(m != NULL);
    test_int(m->mode, MappingFrame);
    test_flt(m->size, 2);
    test_flt(m->blend, 0.5f);
    ecs_fini(world);

    box_render_material_t record;
    TextureMapping frame = { .mode = MappingFrame, .size = 2, .blend = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &frame, .fade = 1 });
    test_uint(record.mapping & 0xffffu, MappingFrame);
    test_uint(record.mapping >> 16, 32768);
    test_flt(record.mapping_size, 2);

    TextureMapping zero = { .mode = MappingFrame, .size = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .mapping = &zero, .fade = 1 });
    test_uint(record.mapping, MappingFrame);
    test_flt(record.mapping_size, 1);
}

/** The frame of a shape is the shape itself when it has TextureFrame, else its nearest ancestor
 * with it, and 0 without one. */
void Material_texture_mapping_frame_of(void) {
    // materials/texture_mapping_frame
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t loose = ecs_new(world);
    test_uint(box_render_texture_frame(world, loose), 0);

    ecs_entity_t outer = ecs_new(world);
    ecs_add(world, outer, TextureFrame);
    ecs_entity_t middle = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, middle);
    ecs_add(world, inner, TextureFrame);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_entity_t side = ecs_new_w_pair(world, EcsChildOf, middle);

    test_uint(box_render_texture_frame(world, outer), outer);
    test_uint(box_render_texture_frame(world, inner), inner);
    test_uint(box_render_texture_frame(world, middle), outer);
    test_uint(box_render_texture_frame(world, side), outer);
    test_uint(box_render_texture_frame(world, leaf), inner);
    test_uint(box_render_texture_frame(world, loose), 0);

    ecs_remove(world, inner, TextureFrame);
    test_uint(box_render_texture_frame(world, leaf), outer);

    ecs_fini(world);
}

/** The relative transform is the inverse of the frame, its scale included, times the model
 * matrix: the identity frame keeps the model matrix, and a moved, turned and scaled frame divides
 * the position and the scale of the instance by the frame scale. */
void Material_texture_mapping_frame_transform(void) {
    // materials/texture_mapping_frame
    box_render_transform_t model = {{
        {2, 0, 0, 5}, {0, 3, 0, 6}, {0, 0, 4, 7}
    }};
    box_render_transform_t out;
    box_draws_frame(&out, &model, NULL);
    test_assert(!ecs_os_memcmp_t(&out, &model, box_render_transform_t));

    /* A frame at (1, 2, 3), turned by 90 degrees around z (x to y) and scaled by 2 */
    WorldTransform frame = {{
        {0, 2, 0, 0}, {-2, 0, 0, 0}, {0, 0, 2, 0}, {1, 2, 3, 1}
    }};
    box_draws_frame(&out, &model, &frame);

    /* The instance origin (5, 6, 7) is (4, 4, 4) from the frame, (4, -4, 4) in its axes,
     * (2, -2, 2) in its units */
    test_flt(out.rows[0][3], 2);
    test_flt(out.rows[1][3], -2);
    test_flt(out.rows[2][3], 2);

    /* The model x axis (2, 0, 0) is -y of the frame, its y axis (0, 3, 0) +x, each half as
     * long in the units of the frame */
    test_flt(out.rows[0][0], 0);
    test_flt(out.rows[1][0], -1);
    test_flt(out.rows[2][0], 0);
    test_flt(out.rows[0][1], 1.5f);
    test_flt(out.rows[1][1], 0);
    test_flt(out.rows[2][1], 0);
    test_flt(out.rows[2][2], 2);

    /* A flat frame keeps the model matrix */
    WorldTransform flat = {{
        {1, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}, {1, 2, 3, 1}
    }};
    box_draws_frame(&out, &model, &flat);
    test_assert(!ecs_os_memcmp_t(&out, &model, box_render_transform_t));
}

/** Find the batch and slot of a drawn shape.
 *
 * @param world The world
 * @param e The shape
 * @param slot Receives its slot
 * @return Its batch
 */
static box_draw_batch_t* frame_batch_of(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t *slot)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(d != NULL);
    test_assert(ds != NULL && ds->batch >= 0);
    *slot = ds->slot;

    return box_draws_batch(d, ds->batch);
}

/** Check that the frame record of a shape holds an expected relative translation and the
 * diagonal of its scale.
 *
 * @param world The world
 * @param e The shape
 * @param x The expected x of the translation relative to its frame
 * @param y The expected y
 * @param z The expected z
 * @param scale The scale of the frame the diagonal holds on top of the ShapeMesh scale, 1 for
 * none
 */
static void frame_record_check(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y,
    float z,
    float scale)
{
    int32_t slot;
    box_draw_batch_t *b = frame_batch_of(world, e, &slot);
    const box_render_transform_t *rec = box_draws_slot_frame(b, slot);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(rec != NULL);
    test_assert(fabsf(rec->rows[0][3] - x) < 1e-4f);
    test_assert(fabsf(rec->rows[1][3] - y) < 1e-4f);
    test_assert(fabsf(rec->rows[2][3] - z) < 1e-4f);
    for (int32_t k = 0; k < 3; k ++) {
        test_assert(fabsf(rec->rows[k][k] - scale * shape->scale[k]) < 1e-4f);
    }
}

/** A MappingFrame shape gets the frame stream with its transform relative to its frame, its
 * own TextureFrame, the nearest of two frames above it or none (its model matrix); the frame of
 * a moved, turned and scaled parent leaves the transform in the local units of the frame. */
void Material_texture_mapping_frame_record(void) {
    // materials/texture_mapping_frame
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    TextureMapping frame_mapping = { .mode = MappingFrame, .size = 1 };

    ecs_entity_t outer = ecs_new(world);
    ecs_add(world, outer, TextureFrame);
    ecs_set(world, outer, Position, {2, 1, -3});
    ecs_set(world, outer, Rotation, {0, 1.5707963f, 0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_add(world, inner, TextureFrame);
    ecs_set(world, inner, Position, {0, 4, 0});
    ecs_set(world, inner, Rotation, {0.3f, 0, 0.2f});
    ecs_set(world, inner, Scale, {2, 2, 2});

    ecs_entity_t in_outer = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, in_outer, Position, {1, 0, 0});
    ecs_set(world, in_outer, Box, {1, 2, 3});
    ecs_set_ptr(world, in_outer, TextureMapping, &frame_mapping);

    ecs_entity_t in_inner = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, in_inner, Position, {1, 0, 0});
    ecs_set(world, in_inner, Box, {1, 2, 3});
    ecs_set_ptr(world, in_inner, TextureMapping, &frame_mapping);

    ecs_entity_t own = ecs_new(world);
    ecs_add(world, own, TextureFrame);
    ecs_set(world, own, Position, {-4, 0, 0});
    ecs_set(world, own, Rotation, {0, 0.5f, 0});
    ecs_set(world, own, Box, {1, 2, 3});
    ecs_set_ptr(world, own, TextureMapping, &frame_mapping);

    ecs_entity_t loose = mapping_box(world, 6, &frame_mapping);
    mapping_frame(world);

    frame_record_check(world, in_outer, 1, 0, 0, 1);
    frame_record_check(world, in_inner, 1, 0, 0, 1);
    frame_record_check(world, own, 0, 0, 0, 1);

    int32_t slot;
    box_draw_batch_t *b = frame_batch_of(world, loose, &slot);
    test_assert(b->key.frame);
    test_assert(!ecs_os_memcmp_t(box_draws_slot_frame(b, slot),
        box_draws_slot_transform(b, slot), box_render_transform_t));

    /* Moving the frame keeps the record */
    ecs_set(world, outer, Position, {-7, 3, 2});
    ecs_set(world, outer, Rotation, {0.4f, -1, 0.1f});
    mapping_frame(world);
    frame_record_check(world, in_outer, 1, 0, 0, 1);
    frame_record_check(world, in_inner, 1, 0, 0, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Materials without MappingFrame stay out of the frame store, as MappingObject without a
 * TextureFrame does: their batches have no frame stream and no page has a frame buffer, until a
 * MappingFrame shape comes. */
void Material_texture_mapping_frame_no_stream(void) {
    // materials/texture_mapping_frame
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    box_render_draws_t *d = box_draws_state(world);
    TextureMapping world_mapping = { .mode = MappingWorld, .size = 1 };
    TextureMapping object_mapping = { .mode = MappingObject, .size = 1 };
    TextureMapping frame_mapping = { .mode = MappingFrame, .size = 1 };

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, TextureFrame);
    ecs_entity_t a = mapping_box(world, 0, NULL);
    ecs_entity_t b = mapping_box(world, 1, &world_mapping);
    ecs_entity_t c = mapping_box(world, 2, &object_mapping);
    ecs_add_pair(world, b, EcsChildOf, parent);
    mapping_frame(world);

    ecs_entity_t shapes[3] = { a, b, c };
    for (int32_t i = 0; i < 3; i ++) {
        int32_t slot;
        box_draw_batch_t *batch = frame_batch_of(world, shapes[i], &slot);
        test_assert(!batch->key.frame);
        test_assert(box_draws_slot_frame(batch, slot) == NULL);
        test_int(ecs_vec_count(&batch->frames), 0);
        test_assert(box_draws_batch_store(d, batch) != &d->stores[BOX_DRAWS_STORE_FRAME]);
        test_uint(box_draws_batch_kinds(batch) & BOX_DRAW_FRAME, 0);
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.page_frame_bytes, 0);
    test_int(stats.frame_bytes, 0);
    test_int(d->stores[BOX_DRAWS_STORE_FRAME].alive, 0);

    ecs_set_ptr(world, c, TextureMapping, &frame_mapping);
    mapping_frame(world);
    int32_t slot;
    box_draw_batch_t *batch = frame_batch_of(world, c, &slot);
    test_assert(batch->key.frame);
    test_assert(box_draws_batch_store(d, batch) == &d->stores[BOX_DRAWS_STORE_FRAME]);
    test_uint(box_draws_batch_kinds(batch), BOX_DRAW_BOTH | BOX_DRAW_FRAME);
    test_assert(box_draws_slot_frame(batch, slot) != NULL);
    test_assert(box_draws_slot_surface(batch, slot) == NULL);
    box_render_draws_stats(world, &stats);
    test_assert(stats.page_frame_bytes > 0);
    test_int(stats.page_surface_bytes, 0);

    /* A parametric surface under the frame takes the store with both streams */
    ecs_entity_t torus = ecs_new(world);
    ecs_add_pair(world, torus, EcsChildOf, parent);
    ecs_set(world, torus, Position, {3, 0, 0});
    ecs_set(world, torus, TorusShell, {
        .radius = 1, .tube = 0.25f,
        .ring_start = 0, .ring_end = 2 * (float)GLM_PI,
        .tube_start = 0, .tube_end = 2 * (float)GLM_PI });
    ecs_set_ptr(world, torus, TextureMapping, &frame_mapping);
    mapping_frame(world);
    batch = frame_batch_of(world, torus, &slot);
    test_assert(batch->key.frame && batch->key.surface);
    test_assert(box_draws_batch_store(d, batch) == &d->stores[BOX_DRAWS_STORE_FRAME_SURFACE]);
    test_uint(box_draws_batch_kinds(batch), BOX_DRAW_ALL);
    test_assert(box_draws_slot_surface(batch, slot) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Adding or removing TextureFrame writes the frame record of the shapes below it, and so does
 * reparenting a shape under another frame. */
void Material_texture_mapping_frame_updates(void) {
    // materials/texture_mapping_frame
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    TextureMapping frame_mapping = { .mode = MappingFrame, .size = 1 };

    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {3, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {0, 0, -5});
    ecs_add(world, second, TextureFrame);
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, first);
    ecs_set(world, shape, Position, {1, 2, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set_ptr(world, shape, TextureMapping, &frame_mapping);
    mapping_frame(world);

    /* No frame: the world position */
    frame_record_check(world, shape, 4, 2, 0, 1);

    ecs_add(world, first, TextureFrame);
    mapping_frame(world);
    frame_record_check(world, shape, 1, 2, 0, 1);

    ecs_remove(world, first, TextureFrame);
    mapping_frame(world);
    frame_record_check(world, shape, 4, 2, 0, 1);

    ecs_add(world, first, TextureFrame);
    mapping_frame(world);
    frame_record_check(world, shape, 1, 2, 0, 1);

    /* Reparented under the second frame, its Position is relative to it */
    ecs_add_pair(world, shape, EcsChildOf, second);
    mapping_frame(world);
    frame_record_check(world, shape, 1, 2, 0, 1);
    ecs_set(world, shape, Position, {0, 1, 2});
    mapping_frame(world);
    frame_record_check(world, shape, 0, 1, 2, 1);

    /* The shape becomes its own frame */
    ecs_add(world, shape, TextureFrame);
    mapping_frame(world);
    frame_record_check(world, shape, 0, 0, 0, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A frame scaled uniformly by 10 divides the relative transform by 10: the texture of a shape
 * of 10 units covers what one unit covered without the scale. */
void Material_texture_mapping_frame_scaled(void) {
    // materials/texture_mapping_frame
    box_render_transform_t model = {{
        {10, 0, 0, 20}, {0, 20, 0, 30}, {0, 0, 30, 40}
    }};
    box_render_transform_t out;

    /* A frame at (10, 10, 10) scaled by 10 */
    WorldTransform frame = {{
        {10, 0, 0, 0}, {0, 10, 0, 0}, {0, 0, 10, 0}, {10, 10, 10, 1}
    }};
    box_draws_frame(&out, &model, &frame);
    test_flt(out.rows[0][3], 1);
    test_flt(out.rows[1][3], 2);
    test_flt(out.rows[2][3], 3);
    test_flt(out.rows[0][0], 1);
    test_flt(out.rows[1][1], 2);
    test_flt(out.rows[2][2], 3);
    test_flt(out.rows[0][1], 0);
    test_flt(out.rows[1][0], 0);

    /* A frame of scale 1 keeps world units */
    WorldTransform unit = {{
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {10, 10, 10, 1}
    }};
    box_draws_frame(&out, &model, &unit);
    test_flt(out.rows[0][3], 10);
    test_flt(out.rows[1][3], 20);
    test_flt(out.rows[2][3], 30);
    test_flt(out.rows[0][0], 10);
    test_flt(out.rows[1][1], 20);
    test_flt(out.rows[2][2], 30);
}

/** Under a frame scaled by 10 a MappingFrame shape gets the record it has under a frame of
 * scale 1, and a scale between the frame and the shape stays in the record, so a stretched plate
 * keeps its texel density. */
void Material_texture_mapping_frame_scaled_record(void) {
    // materials/texture_mapping_frame
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    TextureMapping frame_mapping = { .mode = MappingFrame, .size = 1 };

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, TextureFrame);
    ecs_set(world, root, Position, {5, 0, 0});
    ecs_set(world, root, Rotation, {0, 0.7f, 0});
    ecs_set(world, root, Scale, {10, 10, 10});

    ecs_entity_t plate = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, plate, Position, {1, 2, 0});
    ecs_set(world, plate, Box, {1, 2, 3});
    ecs_set_ptr(world, plate, TextureMapping, &frame_mapping);

    ecs_entity_t stretch = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, stretch, Position, {0, 0, 1});
    ecs_set(world, stretch, Scale, {2, 1, 3});
    ecs_entity_t stretched = ecs_new_w_pair(world, EcsChildOf, stretch);
    ecs_set(world, stretched, Position, {1, 1, 1});
    ecs_set(world, stretched, Box, {1, 2, 3});
    ecs_set_ptr(world, stretched, TextureMapping, &frame_mapping);
    mapping_frame(world);

    /* In the local units of the root, as with a root of scale 1 */
    frame_record_check(world, plate, 1, 2, 0, 1);

    /* The scale between the frame and the shape counts */
    int32_t slot;
    box_draw_batch_t *b = frame_batch_of(world, stretched, &slot);
    const box_render_transform_t *rec = box_draws_slot_frame(b, slot);
    const ShapeMesh *shape = ecs_get(world, stretched, ShapeMesh);
    test_assert(rec != NULL);
    test_assert(fabsf(rec->rows[0][3] - 2) < 1e-4f);
    test_assert(fabsf(rec->rows[1][3] - 1) < 1e-4f);
    test_assert(fabsf(rec->rows[2][3] - 4) < 1e-4f);
    test_assert(fabsf(rec->rows[0][0] - 2 * shape->scale[0]) < 1e-4f);
    test_assert(fabsf(rec->rows[1][1] - 1 * shape->scale[1]) < 1e-4f);
    test_assert(fabsf(rec->rows[2][2] - 3 * shape->scale[2]) < 1e-4f);

    /* Scaling the root again keeps the records */
    ecs_set(world, root, Scale, {4, 4, 4});
    mapping_frame(world);
    frame_record_check(world, plate, 1, 2, 0, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A MappingObject shape under a TextureFrame gets the frame stream with its model matrix over
 * the uniform world scale of the frame, so the shader measures its scale relative to the frame;
 * one without a frame stays out of the frame store and keeps its world scale. */
void Material_texture_mapping_object_frame(void) {
    // materials/texture_mapping_frame
    box_render_null_t *null;
    ecs_world_t *world = mapping_world(&null);
    box_render_draws_t *d = box_draws_state(world);
    TextureMapping object_mapping = { .mode = MappingObject, .size = 1 };

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, TextureFrame);
    ecs_set(world, root, Position, {3, 0, 0});
    ecs_set(world, root, Scale, {10, 10, 10});

    ecs_entity_t ring = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ring, Position, {1, 0, 0});
    ecs_set(world, ring, Rotation, {0.5f, 0, 0});
    ecs_set(world, ring, Scale, {1, 2, 1});
    ecs_set(world, ring, Box, {1, 2, 3});
    ecs_set_ptr(world, ring, TextureMapping, &object_mapping);

    ecs_entity_t loose = mapping_box(world, 0, &object_mapping);
    ecs_set(world, loose, Scale, {10, 10, 10});
    mapping_frame(world);

    /* Under the frame: the model matrix over 10, its column lengths the scale relative to the
     * frame */
    int32_t slot;
    box_draw_batch_t *b = frame_batch_of(world, ring, &slot);
    test_assert(b->key.frame);
    test_assert(box_draws_batch_store(d, b) == &d->stores[BOX_DRAWS_STORE_FRAME]);
    const box_render_transform_t *rec = box_draws_slot_frame(b, slot);
    const box_render_transform_t *model = box_draws_slot_transform(b, slot);
    const ShapeMesh *shape = ecs_get(world, ring, ShapeMesh);
    test_assert(rec != NULL);
    float rel[3] = { 1, 2, 1 };
    for (int32_t c = 0; c < 3; c ++) {
        float length = 0;
        for (int32_t r = 0; r < 3; r ++) {
            test_assert(fabsf(rec->rows[r][c] - model->rows[r][c] / 10) < 1e-4f);
            length += rec->rows[r][c] * rec->rows[r][c];
        }

        test_assert(fabsf(sqrtf(length) - rel[c] * shape->scale[c]) < 1e-3f);
    }

    /* Without a frame: no frame stream, the shader takes the world scale */
    b = frame_batch_of(world, loose, &slot);
    test_assert(!b->key.frame);
    test_assert(box_draws_slot_frame(b, slot) == NULL);
    test_assert(box_draws_batch_store(d, b) != &d->stores[BOX_DRAWS_STORE_FRAME]);

    /* A non uniform frame divides by the cube root of its volume scale */
    ecs_set(world, root, Scale, {2, 4, 8});
    mapping_frame(world);
    b = frame_batch_of(world, ring, &slot);
    rec = box_draws_slot_frame(b, slot);
    model = box_draws_slot_transform(b, slot);
    test_assert(rec != NULL);
    for (int32_t r = 0; r < 3; r ++) {
        for (int32_t c = 0; c < 4; c ++) {
            float expect = c == 3 ? model->rows[r][c] : model->rows[r][c] / 4;
            test_assert(fabsf(rec->rows[r][c] - expect) < 1e-3f);
        }
    }

    /* Losing the frame leaves the frame store, getting it back joins it again */
    ecs_remove(world, root, TextureFrame);
    mapping_frame(world);
    b = frame_batch_of(world, ring, &slot);
    test_assert(!b->key.frame);
    test_assert(box_draws_slot_frame(b, slot) == NULL);

    ecs_add(world, root, TextureFrame);
    mapping_frame(world);
    b = frame_batch_of(world, ring, &slot);
    test_assert(b->key.frame);
    test_assert(box_draws_slot_frame(b, slot) != NULL);

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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
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

/** A deleted layer entity, whose id the TextureBlend still holds, contributes the defaults like
 * a layer without PbrTextures, and the shape keeps blending. */
void Material_texture_blend_layer_deleted(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = a, .normal = g});
    ecs_set(world, grass, TextureTransform, {.scale_x = 4, .scale_y = 4});
    ecs_entity_t rock = ecs_new(world);
    ecs_set(world, rock, PbrTextures, {.albedo = g});
    ecs_entity_t e = cutout_shape(world, 9);
    ecs_set(world, e, TextureBlend, {.layers = {grass, rock}});
    ecs_entity_t textures[2] = { a, g };
    blend_wait(world, textures, 2);

    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    const box_render_texture_blend_t *r = blend_record(null, found[0]);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, a));
    test_flt(r->transforms[0][0], 4);

    ecs_delete(world, grass);
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    test_bool(found[0]->texture_blend, true);
    r = blend_record(null, found[0]);
    test_uint(r->count, 2);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ALBEDO], 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_ORM], 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_NORMAL], 0);
    test_flt(r->transforms[0][0], 1);
    test_flt(r->transforms[0][1], 1);
    test_uint(r->layers[1][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, g));

    ecs_fini(world);
    box_render_null_free(null);
}

/** The contrast and the heights of a TextureBlend reach the record of its batch, the bind group
 * key of the backend, as the layers do: with a contrast of 0 the heights stay 0 so they split no
 * bind group, with a contrast above 0 each layer carries the handle of its height.
 *
 * A change of the contrast, of a height or of the image of a height updates the record, and the
 * shape keeps its batch. A contrast that is negative or not finite is 0. */
void Material_texture_blend_height_record(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t h = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = a});
    ecs_entity_t cobble = ecs_new(world);
    ecs_set(world, cobble, PbrTextures, {.albedo = g});
    ecs_set(world, cobble, TextureTransform, {.scale_x = 4, .scale_y = 4});
    ecs_entity_t e = cutout_shape(world, 8);
    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {0, h}});
    ecs_entity_t textures[3] = { a, g, h };
    blend_wait(world, textures, 3);

    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    const box_render_texture_blend_t *r = blend_record(null, found[0]);
    test_flt(r->contrast, 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_HEIGHT], 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], 0);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;

    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {0, h},
        .contrast = 16});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_flt(r->contrast, 16);
    test_assert(blend_handle(world, h) != 0);
    test_uint(r->layers[0][BOX_RENDER_BLEND_HEIGHT], 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, h));
    test_uint(r->layers[1][BOX_RENDER_BLEND_ALBEDO], blend_handle(world, g));
    test_flt(r->transforms[1][0], 4);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);

    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {a, g},
        .contrast = 16});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_uint(r->layers[0][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, a));
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, g));
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);

    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {0, h},
        .contrast = 16});
    ecs_set(world, h, TexturePath, {.path = ECS_CONST_CAST(char*, BLEND_RGB_PNG)});
    blend_wait(world, &h, 1);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_assert(blend_handle(world, h) != 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, h));
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);

    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {0, h},
        .contrast = -2});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_flt(r->contrast, 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], 0);

    ecs_set(world, e, TextureBlend, {.layers = {grass, cobble}, .heights = {0, h},
        .contrast = NAN});
    mapping_frame(world);
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    r = blend_record(null, found[0]);
    test_flt(r->contrast, 0);
    test_uint(r->layers[1][BOX_RENDER_BLEND_HEIGHT], 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Two shapes whose blends have the same layers but differ in their contrast, or only in a
 * height with the same contrast, draw with records that differ, so they bind different groups;
 * instances of a prefab with a contrast share its batch and record. */
void Material_texture_blend_height_key(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = a});
    ecs_entity_t cobble = ecs_new(world);
    ecs_set(world, cobble, PbrTextures, {.albedo = g});

    ecs_entity_t linear = cutout_shape(world, 7);
    ecs_set(world, linear, TextureBlend, {.layers = {grass, cobble}, .heights = {0, g}});
    ecs_entity_t sharp = cutout_shape(world, 7);
    ecs_set(world, sharp, TextureBlend, {.layers = {grass, cobble}, .heights = {0, g},
        .contrast = 24});

    ecs_entity_t left = cutout_shape(world, 9);
    ecs_set(world, left, TextureBlend, {.layers = {grass, cobble}, .heights = {0, g},
        .contrast = 24});
    ecs_entity_t right = cutout_shape(world, 9);
    ecs_set(world, right, TextureBlend, {.layers = {grass, cobble}, .heights = {0, a},
        .contrast = 24});

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, NGon, {.sides = 5, .radius = 0.5f});
    ecs_set(world, prefab, TextureBlend, {.layers = {grass, cobble}, .heights = {a, g},
        .contrast = 8});
    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, first, Position, {0, 0, 0});
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, second, Position, {1, 0, 0});

    ecs_entity_t textures[2] = { a, g };
    blend_wait(world, textures, 2);

    const box_render_batch_t *found[4];
    test_int(blend_batches(null, cutout_mesh(world, linear), found, 4), 2);
    const box_render_texture_blend_t *r0 = blend_record(null, found[0]);
    const box_render_texture_blend_t *r1 = blend_record(null, found[1]);
    test_assert(r0->contrast != r1->contrast);
    test_assert(ecs_os_memcmp(r0, r1, ECS_SIZEOF(box_render_texture_blend_t)) != 0);
    const box_render_texture_blend_t *flat = r0->contrast == 0 ? r0 : r1;
    test_uint(flat->layers[1][BOX_RENDER_BLEND_HEIGHT], 0);

    test_int(blend_batches(null, cutout_mesh(world, left), found, 4), 2);
    r0 = blend_record(null, found[0]);
    r1 = blend_record(null, found[1]);
    test_flt(r0->contrast, 24);
    test_flt(r1->contrast, 24);
    test_uint(r0->layers[1][BOX_RENDER_BLEND_ALBEDO], r1->layers[1][BOX_RENDER_BLEND_ALBEDO]);
    test_assert(r0->layers[1][BOX_RENDER_BLEND_HEIGHT] != r1->layers[1][BOX_RENDER_BLEND_HEIGHT]);
    test_assert(ecs_os_memcmp(r0, r1, ECS_SIZEOF(box_render_texture_blend_t)) != 0);

    test_int(blend_batches(null, cutout_mesh(world, first), found, 4), 1);
    test_uint(found[0]->instance_count, 2);
    r0 = blend_record(null, found[0]);
    test_flt(r0->contrast, 8);
    test_uint(r0->layers[0][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, a));
    test_uint(r0->layers[1][BOX_RENDER_BLEND_HEIGHT], blend_handle(world, g));

    ecs_fini(world);
    box_render_null_free(null);
}

/** Check that a mesh draws in one batch with the TextureBlend whose layer 0 samples an image,
 * and get that batch.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape
 * @param tex The texture entity of the albedo of layer 0
 * @return The batch
 */
static const box_render_batch_t* blend_only_batch(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    ecs_entity_t tex)
{
    const box_render_batch_t *found[2];
    test_int(blend_batches(null, cutout_mesh(world, e), found, 2), 1);
    test_bool(found[0]->texture_blend, true);
    test_uint(blend_record(null, found[0])->layers[0][BOX_RENDER_BLEND_ALBEDO],
        blend_handle(world, tex));

    return found[0];
}

/** A shape with a TextureBlend blends whatever draws it: alpha tested in the opaque pass and
 * the shadow pass, with AlphaBlend or an Opacity under 1 in the sorted blended pass, with
 * Transmission in the transmissive pass, and with a triplanar TextureMapping.
 *
 * Its own albedo, which the blend replaces, is not bound, and without the alpha test and the
 * blend mode it still blends, now opaque. */
void Material_texture_blend_every_shape(void) {
    // materials/texture_blend
    box_render_null_t *null;
    ecs_world_t *world = blend_world(&null);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t a = blend_texture(world, BLEND_RGB_PNG);
    ecs_entity_t g = blend_texture(world, BLEND_GRAY_PNG);
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = a});
    ecs_entity_t cut = cutout_shape(world, 9);
    ecs_set(world, cut, Position, {-2, 0, 0});
    ecs_set(world, cut, PbrTextures, {.albedo = g});
    ecs_set(world, cut, TextureBlend, {.layers = {layer}});
    ecs_set(world, cut, AlphaCutoff, {0.5f});
    ecs_entity_t blended = cutout_shape(world, 10);
    ecs_set(world, blended, TextureBlend, {.layers = {layer}});
    ecs_set(world, blended, BlendMode, {AlphaBlend});
    ecs_entity_t faded = cutout_shape(world, 11);
    ecs_set(world, faded, Position, {0, 1, 0});
    ecs_set(world, faded, TextureBlend, {.layers = {layer}});
    ecs_set(world, faded, Opacity, {0.5f});
    ecs_entity_t clear = cutout_shape(world, 12);
    ecs_set(world, clear, Position, {2, 0, 0});
    ecs_set(world, clear, TextureBlend, {.layers = {layer}});
    ecs_set(world, clear, Transmission, {.transmission_factor = 1, .ior = 1.5f});
    ecs_entity_t mapped = cutout_shape(world, 13);
    ecs_set(world, mapped, Position, {0, -1, 0});
    ecs_set(world, mapped, TextureBlend, {.layers = {layer}});
    ecs_set(world, mapped, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0.5f});
    ecs_entity_t textures[2] = { a, g };
    blend_wait(world, textures, 2);

    const box_render_batch_t *b = blend_only_batch(world, null, cut, a);
    test_bool(b->cutout, true);
    test_int(b->blend, Opaque);
    test_int(b->view, -1);
    test_bool(b->shadow, true);
    test_bool(b->transmissive, false);
    test_uint(b->textures[0], 0);

    b = blend_only_batch(world, null, blended, a);
    test_int(b->blend, AlphaBlend);
    test_bool(b->listed, true);
    test_assert(b->view >= 0);
    test_bool(b->cutout, false);
    test_bool(b->transmissive, false);

    b = blend_only_batch(world, null, faded, a);
    test_int(b->blend, AlphaBlend);
    test_bool(b->listed, true);
    test_assert(b->view >= 0);

    b = blend_only_batch(world, null, clear, a);
    test_bool(b->transmissive, true);
    test_int(b->blend, AlphaBlend);
    test_bool(b->listed, true);
    test_assert(b->view >= 0);
    test_bool(b->cutout, false);
    test_bool(b->shadow, false);

    b = blend_only_batch(world, null, mapped, a);
    test_int(b->blend, Opaque);
    test_bool(b->cutout, false);
    test_int(b->view, -1);

    ecs_remove(world, cut, AlphaCutoff);
    ecs_remove(world, blended, BlendMode);
    mapping_frame(world);
    b = blend_only_batch(world, null, cut, a);
    test_bool(b->cutout, false);
    test_int(b->blend, Opaque);
    b = blend_only_batch(world, null, blended, a);
    test_int(b->blend, Opaque);
    test_int(b->view, -1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_texture_scroll_defaults(void) {
    // materials/texture_scroll
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "scroll",
        "using box.material\n"
        "river { texture.Scroll: {velocity: {0.5, -0.25}} }\n"
        "ripples { texture.Scroll: {normal_velocity: {0, 0.1}} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, TextureScroll);
    const TextureScroll *s = ecs_get(world, added, TextureScroll);
    test_assert(s != NULL);
    test_flt(s->velocity[0], BOX_MATERIAL_SCROLL_VELOCITY);
    test_flt(s->velocity[0], 0);
    test_flt(s->velocity[1], 0);
    test_flt(s->normal_velocity[0], 0);
    test_flt(s->normal_velocity[1], 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    s = ecs_get(world, ecs_lookup(world, "river"), TextureScroll);
    test_assert(s != NULL);
    test_flt(s->velocity[0], 0.5f);
    test_flt(s->velocity[1], -0.25f);
    test_flt(s->normal_velocity[0], 0);
    test_flt(s->normal_velocity[1], 0);

    s = ecs_get(world, ecs_lookup(world, "ripples"), TextureScroll);
    test_assert(s != NULL);
    test_flt(s->velocity[0], 0);
    test_flt(s->normal_velocity[1], 0.1f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, TextureScroll, {.velocity = {0.25f, 0.75f}});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, TextureScroll), false);
    test_flt(ecs_get(world, instance, TextureScroll)->velocity[0], 0.25f);
    test_flt(ecs_get(world, instance, TextureScroll)->velocity[1], 0.75f);

    ecs_fini(world);
}

/** The record adds the wrapped velocity offset to the TextureTransform offset and holds the
 * wrapped normal_velocity offset on its own; a material without TextureScroll packs the bytes it
 * did before, and a transition of another component encodes the scroll again. */
void Material_texture_scroll_pack(void) {
    // materials/texture_scroll
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    TextureTransform uv = { .scale_x = 2, .scale_y = 3, .offset_x = 0.25f, .offset_y = -0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .uv = &uv, .fade = 1 });
    test_flt(record.uv[2], 0.25f);
    test_flt(record.uv[3], -0.5f);
    test_flt(record.normal_offset[0], 0);
    test_flt(record.normal_offset[1], 0);
    test_uint(record.fresnel_color[0], 0);
    test_uint(record.fresnel_color[1], 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 272);

    box_draws_material_pack(&record, &(box_draw_material_t){
        .uv = &uv, .fade = 1, .time = 1000 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureScroll still = {0};
    box_draws_material_pack(&record, &(box_draw_material_t){
        .uv = &uv, .scroll = &still, .time = 1000, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    TextureScroll scroll = { .velocity = {0.25f, -0.5f}, .normal_velocity = {0.125f, 0.75f} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .uv = &uv, .scroll = &scroll, .time = 3, .fade = 1 });
    test_flt(record.uv[0], 2);
    test_flt(record.uv[1], 3);
    test_flt(record.uv[2], 0.25f + 0.75f);
    test_flt(record.uv[3], -0.5f + 0.5f);
    test_flt(record.normal_offset[0], 0.375f);
    test_flt(record.normal_offset[1], 0.25f);
    test_uint(record.fresnel_color[0], 0);
    test_uint(record.fresnel_color[1], 0);

    box_draws_material_pack(&record, &(box_draw_material_t){
        .scroll = &scroll, .time = 3, .fade = 1 });
    test_flt(record.uv[0], 1);
    test_flt(record.uv[1], 1);
    test_flt(record.uv[2], 0.75f);
    test_flt(record.uv[3], 0.5f);

    record.uv[2] = plain.uv[2];
    record.uv[3] = plain.uv[3];
    record.uv[0] = plain.uv[0];
    record.uv[1] = plain.uv[1];
    record.normal_offset[0] = 0;
    record.normal_offset[1] = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .uv = &uv, .scroll = &scroll, .time = 12345.5, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .uv = &uv, .scroll = &scroll, .time = 12345.5, .pbr = &pbr, .fade = 1 },
        BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t a, b;
    box_draws_material(&a, &(box_draw_material_t){ .uv = &uv, .fade = 1 });
    box_draws_material(&b, &(box_draw_material_t){
        .uv = &uv, .scroll = &scroll, .time = 3, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_shading_t));
}

/** The offsets wrap to 0..2, stay positive and keep their precision after hours: two frames
 * 1/60 s apart at 10^6 s differ by velocity / 60, and the wrap does not jump. */
void Material_texture_scroll_wrap(void) {
    // materials/texture_scroll
    float out[4], next[4];
    box_draws_texture_scroll(NULL, 1000, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    test_flt(out[3], 0);

    TextureScroll scroll = { .velocity = {0.37f, -0.23f}, .normal_velocity = {-1.3f, 0.051f} };
    box_draws_texture_scroll(&scroll, 0, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);

    box_draws_texture_scroll(&scroll, 1, out);
    test_assert(fabsf(out[0] - 0.37f) < 1e-6f);
    test_assert(fabsf(out[1] - (2.0f - 0.23f)) < 1e-6f);
    test_assert(fabsf(out[2] - (2.0f - 1.3f)) < 1e-6f);
    test_assert(fabsf(out[3] - 0.051f) < 1e-6f);

    const float *v = scroll.velocity;
    const float *n = scroll.normal_velocity;
    float velocity[4] = { v[0], v[1], n[0], n[1] };
    double starts[3] = { 1e6, 4.0 * 3600.0, 1e6 + 12345.678 };
    int32_t s, f, k;
    for (s = 0; s < 3; s ++) {
        for (f = 0; f < 600; f ++) {
            double t = starts[s] + (double)f / 60.0;
            box_draws_texture_scroll(&scroll, t, out);
            box_draws_texture_scroll(&scroll, t + 1.0 / 60.0, next);
            for (k = 0; k < 4; k ++) {
                test_assert(out[k] >= 0.0f && out[k] < 2.0f);
                float d = next[k] - out[k];
                if (d > 1.0f) {
                    d -= 2.0f;
                } else if (d < -1.0f) {
                    d += 2.0f;
                }

                test_assert(fabsf(d - velocity[k] / 60.0f) < 1e-5f);
            }
        }
    }

    TextureScroll broken = { .velocity = {NAN, INFINITY}, .normal_velocity = {-INFINITY, 1} };
    box_draws_texture_scroll(&broken, 10.5, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    test_flt(out[3], 0.5f);
}

void Material_fresnel_defaults(void) {
    // materials/fresnel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fresnel",
        "using box.material\n"
        "thin { Fresnel: {power: 8} }\n"
        "field { Fresnel: {color: {0, 128, 255}, opacity: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Fresnel);
    const Fresnel *f = ecs_get(world, added, Fresnel);
    test_assert(f != NULL);
    test_uint(f->color.r, 255);
    test_uint(f->color.g, 255);
    test_uint(f->color.b, 255);
    test_flt(f->strength, BOX_MATERIAL_FRESNEL_STRENGTH);
    test_flt(f->strength, 1);
    test_flt(f->power, BOX_MATERIAL_FRESNEL_POWER);
    test_flt(f->power, 5);
    test_flt(f->bias, BOX_MATERIAL_FRESNEL_BIAS);
    test_flt(f->bias, 0);
    test_flt(f->opacity, BOX_MATERIAL_FRESNEL_OPACITY);
    test_flt(f->opacity, 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    f = ecs_get(world, ecs_lookup(world, "thin"), Fresnel);
    test_assert(f != NULL);
    test_uint(f->color.g, 255);
    test_flt(f->strength, 1);
    test_flt(f->power, 8);
    test_flt(f->bias, 0);
    test_flt(f->opacity, 0);
    test_bool(ecs_owns(world, ecs_lookup(world, "thin"), MaterialId), true);

    f = ecs_get(world, ecs_lookup(world, "field"), Fresnel);
    test_assert(f != NULL);
    test_uint(f->color.r, 0);
    test_uint(f->color.g, 128);
    test_uint(f->color.b, 255);
    test_flt(f->strength, 1);
    test_flt(f->power, 5);
    test_flt(f->opacity, 0.5f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Fresnel, {.color = {10, 20, 30}, .strength = 2, .power = 3});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Fresnel), false);
    test_uint(ecs_get(world, instance, Fresnel)->color.g, 20);
    test_flt(ecs_get(world, instance, Fresnel)->power, 3);

    ecs_fini(world);
}

/** The record holds the Fresnel color in linear light as unorm16, the strength, power, bias and
 * the opacity clamped to 0..1; a material without one packs zeros and the bytes it did before,
 * and a transition of another component encodes the record again with it. */
void Material_fresnel_pack(void) {
    // materials/fresnel
    box_render_material_t record, plain;
    box_draws_tables_init();
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_uint(record.fresnel_color[0], 0);
    test_uint(record.fresnel_color[1], 0);
    test_flt(record.fresnel_strength, 0);
    test_flt(record.fresnel_power, 0);
    test_flt(record.fresnel_bias, 0);
    test_flt(record.fresnel_opacity, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 272);
    test_int(ECS_SIZEOF(box_render_material_t) % 16, 0);
    test_int(offsetof(box_render_material_t, fresnel_color), 104);
    test_int(offsetof(box_render_material_t, fresnel_strength), 112);
    test_int(offsetof(box_render_material_t, fresnel_opacity), 124);

    Fresnel red = { .color = {255, 0, 128}, .strength = 2.5f, .power = 3, .bias = 0.25f,
        .opacity = 0.75f };
    box_draws_material_pack(&record, &(box_draw_material_t){ .fresnel = &red, .fade = 1 });
    test_uint(record.fresnel_color[0] & 0xFFFFu, 0xFFFFu);
    test_uint(record.fresnel_color[0] >> 16, 0);
    uint32_t b = record.fresnel_color[1] & 0xFFFFu;
    uint32_t expect = (uint32_t)(0.2158605f * 65535.0f + 0.5f);
    test_assert(b + 2 >= expect && b <= expect + 2);
    test_uint(record.fresnel_color[1] >> 16, 0);
    test_flt(record.fresnel_strength, 2.5f);
    test_flt(record.fresnel_power, 3);
    test_flt(record.fresnel_bias, 0.25f);
    test_flt(record.fresnel_opacity, 0.75f);

    /* Fresnel leaves the emission of the record alone: the shader adds it */
    test_flt(record.emissive[0], 0);
    test_flt(record.emissive[1], 0);
    test_flt(record.emissive[2], 0);

    Fresnel broken = { .color = {255, 255, 255}, .strength = -1, .power = NAN, .bias = INFINITY,
        .opacity = 3 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .fresnel = &broken, .fade = 1 });
    test_flt(record.fresnel_strength, 0);
    test_flt(record.fresnel_power, 0);
    test_flt(record.fresnel_bias, 0);
    test_flt(record.fresnel_opacity, 1);
    test_uint(record.fresnel_color[0], 0xFFFFFFFFu);
    test_uint(record.fresnel_color[1], 0xFFFFu);

    Fresnel low = { .color = {0, 0, 0}, .strength = 1, .power = 5, .bias = -0.5f,
        .opacity = -1 };
    box_draws_material_pack(&record, &(box_draw_material_t){ .fresnel = &low, .fade = 1 });
    test_flt(record.fresnel_bias, -0.5f);
    test_flt(record.fresnel_opacity, 0);

    box_draws_material_pack(&record, &(box_draw_material_t){ .fresnel = &red, .fade = 1 });
    record.fresnel_color[0] = 0;
    record.fresnel_color[1] = 0;
    record.fresnel_strength = 0;
    record.fresnel_power = 0;
    record.fresnel_bias = 0;
    record.fresnel_opacity = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .fresnel = &red, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .fresnel = &red, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t sa, sb;
    box_draws_material(&sa, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&sb, &(box_draw_material_t){
        .fresnel = &red, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&sa, &sb, box_render_shading_t));
}

/** The C twin of fresnel_factor of material.wgsl: at 0 degrees (facing the viewer) only the
 * bias, at 60 degrees 0.5^power plus the bias, at 90 degrees (grazing) 1, all clamped to 0..1. */
void Material_fresnel_factor(void) {
    // materials/fresnel
    float c0 = 1.0f;
    float c60 = cosf(glm_rad(60.0f));
    float c90 = cosf(glm_rad(90.0f));

    test_assert(fabsf(box_draws_fresnel_factor(c0, 5, 0)) < 1e-6f);
    test_assert(fabsf(box_draws_fresnel_factor(c60, 5, 0) - 0.03125f) < 1e-5f);
    test_assert(fabsf(box_draws_fresnel_factor(c90, 5, 0) - 1.0f) < 1e-5f);

    test_assert(fabsf(box_draws_fresnel_factor(c0, 2, 0)) < 1e-6f);
    test_assert(fabsf(box_draws_fresnel_factor(c60, 2, 0) - 0.25f) < 1e-5f);
    test_assert(fabsf(box_draws_fresnel_factor(c90, 2, 0) - 1.0f) < 1e-5f);

    test_assert(fabsf(box_draws_fresnel_factor(c0, 5, 0.25f) - 0.25f) < 1e-5f);
    test_assert(fabsf(box_draws_fresnel_factor(c60, 5, 0.25f) - 0.28125f) < 1e-5f);
    test_flt(box_draws_fresnel_factor(c90, 5, 0.25f), 1);

    test_flt(box_draws_fresnel_factor(c0, 5, -0.5f), 0);
    test_assert(fabsf(box_draws_fresnel_factor(c60, 1, -0.25f) - 0.25f) < 1e-5f);
    test_flt(box_draws_fresnel_factor(c0, 5, 2), 1);

    /* Power 0 glows everywhere, a back facing normal counts as grazing */
    test_flt(box_draws_fresnel_factor(c0, 0, 0), 1);
    test_flt(box_draws_fresnel_factor(-0.5f, 5, 0), 1);

    /* A higher power gives a thinner rim */
    test_assert(box_draws_fresnel_factor(c60, 8, 0) < box_draws_fresnel_factor(c60, 2, 0));
}

void Material_water_defaults(void) {
    // materials/water
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "water",
        "using box.material\n"
        "pond { Water: {depth: 5, foam_width: 0} }\n"
        "sea { Water: {shallow_color: {10, 20, 30}, ripple_scale: 2.5,"
        " ripple_velocity: {0.25, -0.5}} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Water);
    const Water *w = ecs_get(world, added, Water);
    test_assert(w != NULL);
    test_uint(w->shallow_color.r, 60);
    test_uint(w->shallow_color.g, 170);
    test_uint(w->shallow_color.b, 160);
    test_uint(w->deep_color.r, 8);
    test_uint(w->deep_color.g, 40);
    test_uint(w->deep_color.b, 64);
    test_flt(w->depth, BOX_MATERIAL_WATER_DEPTH);
    test_flt(w->depth, 3);
    test_flt(w->visibility, 2);
    test_uint(w->absorption.r, 255);
    test_uint(w->absorption.g, 255);
    test_uint(w->absorption.b, 255);
    test_uint(w->absorption.r, BOX_MATERIAL_WATER_ABSORPTION_R);
    test_uint(w->absorption.g, BOX_MATERIAL_WATER_ABSORPTION_G);
    test_uint(w->absorption.b, BOX_MATERIAL_WATER_ABSORPTION_B);
    test_flt(w->refraction, 0.02f);
    test_uint(w->foam_color.r, 240);
    test_uint(w->foam_color.g, 245);
    test_uint(w->foam_color.b, 245);
    test_flt(w->foam_width, 0.3f);
    test_uint(w->foam_texture, 0);
    test_flt(w->foam_scale, 1);
    test_flt(w->ripple_scale, 0);
    test_flt(w->ripple_velocity[0], 0);
    test_flt(w->ripple_velocity[1], 0);
    test_bool(ecs_owns(world, added, MaterialId), true);

    w = ecs_get(world, ecs_lookup(world, "pond"), Water);
    test_assert(w != NULL);
    test_flt(w->depth, 5);
    test_flt(w->foam_width, 0);
    test_flt(w->visibility, 2);
    test_uint(w->absorption.r, 255);
    test_uint(w->absorption.b, 255);
    test_uint(w->shallow_color.g, 170);
    test_bool(ecs_owns(world, ecs_lookup(world, "pond"), MaterialId), true);

    w = ecs_get(world, ecs_lookup(world, "sea"), Water);
    test_assert(w != NULL);
    test_uint(w->shallow_color.r, 10);
    test_uint(w->shallow_color.g, 20);
    test_uint(w->shallow_color.b, 30);
    test_uint(w->deep_color.b, 64);
    test_flt(w->ripple_scale, 2.5f);
    test_flt(w->ripple_velocity[0], 0.25f);
    test_flt(w->ripple_velocity[1], -0.5f);
    test_flt(w->depth, 3);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, Water);
    ecs_get_mut(world, prefab, Water)->depth = 7;
    ecs_modified(world, prefab, Water);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Water), false);
    test_flt(ecs_get(world, instance, Water)->depth, 7);
    test_flt(ecs_get(world, instance, Water)->visibility, 2);

    ecs_fini(world);
}

/** The record holds the Water colors display encoded, its floats sanitized and the ripple offset
 * wrapped at the time; a material without one packs zeros and the bytes it did before, and a
 * transition of another component encodes the record again with it. */
void Material_water_pack(void) {
    // materials/water
    box_render_material_t record, plain;
    box_draws_tables_init();
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.water_ripple_offset[0], 0);
    test_flt(record.water_ripple_offset[1], 0);
    test_uint(record.water_shallow, 0);
    test_uint(record.water_deep, 0);
    test_uint(record.water_foam, 0);
    test_flt(record.water_depth, 0);
    test_flt(record.water_visibility, 0);
    test_flt(record.water_refraction, 0);
    test_flt(record.water_foam_width, 0);
    test_flt(record.water_foam_scale, 0);
    test_flt(record.water_ripple_scale, 0);
    test_uint(record.water_flags, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 272);
    test_int(offsetof(box_render_material_t, water_ripple_offset), 128);
    test_int(offsetof(box_render_material_t, water_shallow), 136);
    test_int(offsetof(box_render_material_t, water_depth), 148);
    test_int(offsetof(box_render_material_t, water_flags), 172);

    Water pond = {
        .shallow_color = {60, 170, 160}, .deep_color = {8, 40, 64},
        .depth = 3, .visibility = 2, .refraction = 0.02f, .foam_color = {240, 245, 245},
        .foam_width = 0.3f, .foam_texture = 0, .foam_scale = 1, .ripple_scale = 0,
        .ripple_velocity = {0.5f, 0.25f}
    };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &pond, .time = 3, .fade = 1 });
    test_uint(record.water_shallow, 60u | (170u << 8) | (160u << 16));
    test_uint(record.water_deep, 8u | (40u << 8) | (64u << 16));
    test_uint(record.water_foam, 240u | (245u << 8) | (245u << 16));
    test_flt(record.water_depth, 3);
    test_flt(record.water_visibility, 2);
    test_flt(record.water_refraction, 0.02f);
    test_flt(record.water_foam_width, 0.3f);
    test_flt(record.water_foam_scale, 1);
    test_flt(record.water_ripple_scale, 0);
    test_uint(record.water_flags, 0);

    /* Without ripples the velocity does not move anything */
    test_flt(record.water_ripple_offset[0], 0);
    test_flt(record.water_ripple_offset[1], 0);

    /* Water leaves the transmission of the record alone */
    test_flt(record.transmission_factor, 0);

    Water rippled = pond;
    rippled.ripple_scale = 4;
    rippled.ripple_velocity[0] = 0.25f;
    rippled.ripple_velocity[1] = -0.5f;
    rippled.foam_texture = 1234;
    rippled.foam_scale = 8;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &rippled, .time = 3, .fade = 1 });
    test_flt(record.water_ripple_scale, 4);
    test_flt(record.water_ripple_offset[0], 0.75f);
    test_flt(record.water_ripple_offset[1], 0.5f);
    test_flt(record.water_foam_scale, 8);
    test_uint(record.water_flags, BOX_RENDER_WATER_FOAM_TEXTURE);

    /* The absorption fills the bytes above the flags, display encoded */
    Water tinted = rippled;
    tinted.absorption = (Rgb){20, 140, 230};
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &tinted, .time = 3, .fade = 1 });
    test_uint(record.water_flags, BOX_RENDER_WATER_FOAM_TEXTURE | (20u << 8) | (140u << 16) |
        (230u << 24));
    test_uint(record.water_shallow, 60u | (170u << 8) | (160u << 16));
    test_uint(record.water_deep, 8u | (40u << 8) | (64u << 16));
    test_uint(record.water_foam, 240u | (245u << 8) | (245u << 16));
    tinted.foam_texture = 0;
    tinted.absorption = (Rgb){255, 255, 255};
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &tinted, .time = 3, .fade = 1 });
    test_uint(record.water_flags, 0xFFFFFFu << BOX_RENDER_WATER_ABSORPTION_SHIFT);
    test_uint(record.water_flags & 0xFFu, 0);

    /* The ripple offset wraps like TextureScroll, in double precision */
    float scroll[4];
    TextureScroll same = { .velocity = {0.25f, -0.5f} };
    box_draws_texture_scroll(&same, 12345.5, scroll);
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &rippled, .time = 12345.5, .fade = 1 });
    test_flt(record.water_ripple_offset[0], scroll[0]);
    test_flt(record.water_ripple_offset[1], scroll[1]);

    Water broken = pond;
    broken.depth = -1;
    broken.visibility = NAN;
    broken.refraction = INFINITY;
    broken.foam_width = -2;
    broken.foam_scale = NAN;
    broken.ripple_scale = -1;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 3, .fade = 1 });
    test_flt(record.water_depth, BOX_RENDER_WATER_MIN);
    test_flt(record.water_visibility, BOX_RENDER_WATER_MIN);
    test_flt(record.water_refraction, 0);
    test_flt(record.water_foam_width, 0);
    test_flt(record.water_foam_scale, 0);
    test_flt(record.water_ripple_scale, 0);
    test_flt(record.water_ripple_offset[0], 0);

    Water zero = pond;
    zero.depth = 0;
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &zero, .fade = 1 });
    test_assert(record.water_depth > 0);

    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    PbrMaterial pbr = { .metallic = 0, .roughness = 0.05f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &rippled, .pbr = &pbr, .time = 7.25, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .water = &rippled, .pbr = &pbr, .time = 7.25, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));

    box_render_shading_t sa, sb;
    box_draws_material(&sa, &(box_draw_material_t){ .pbr = &pbr, .fade = 1 });
    box_draws_material(&sb, &(box_draw_material_t){
        .water = &rippled, .pbr = &pbr, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&sa, &sb, box_render_shading_t));
}

/** The C twin of water_depth_mix of material.wgsl: 0 at the surface, about 95% of the deep
 * color at the depth, 1 - exp(-3) in general, monotonic, and 1 where nothing is behind. */
void Material_water_depth_mix(void) {
    // materials/water
    test_flt(box_draws_water_depth_mix(0, 3), 0);
    test_assert(fabsf(box_draws_water_depth_mix(3, 3) - (1.0f - expf(-3.0f))) < 1e-6f);
    test_assert(box_draws_water_depth_mix(3, 3) > 0.94f);
    test_assert(box_draws_water_depth_mix(3, 3) < 0.96f);
    test_assert(fabsf(box_draws_water_depth_mix(1, 2) - (1.0f - expf(-1.5f))) < 1e-6f);
    test_assert(box_draws_water_depth_mix(0.5f, 3) < box_draws_water_depth_mix(1, 3));
    test_assert(box_draws_water_depth_mix(1, 3) < box_draws_water_depth_mix(2, 3));

    /* A deeper Water.depth keeps the color shallow longer */
    test_assert(box_draws_water_depth_mix(1, 6) < box_draws_water_depth_mix(1, 3));

    /* Negative depths (above the surface) are shallow, the sky is deep */
    test_flt(box_draws_water_depth_mix(-1, 3), 0);
    test_flt(box_draws_water_depth_mix(INFINITY, 3), 1);
    test_flt(box_draws_water_depth_mix(1e9f, 3), 1);

    /* A depth of 0 is the smallest one */
    test_assert(box_draws_water_depth_mix(0.01f, 0) > 0.99f);
}

/** The C twin of water_transmittance of material.wgsl: 1 at the surface, exp(-3) at the
 * visibility, falling with the ray length, and 0 where nothing is behind. */
void Material_water_transmittance(void) {
    // materials/water
    test_flt(box_draws_water_transmittance(0, 2), 1);
    test_assert(fabsf(box_draws_water_transmittance(2, 2) - expf(-3.0f)) < 1e-6f);
    test_assert(fabsf(box_draws_water_transmittance(1, 2) - expf(-1.5f)) < 1e-6f);
    test_assert(box_draws_water_transmittance(0.5f, 2) > box_draws_water_transmittance(1, 2));

    /* A larger visibility shows more of the scene behind */
    test_assert(box_draws_water_transmittance(1, 4) > box_draws_water_transmittance(1, 2));

    test_flt(box_draws_water_transmittance(-1, 2), 1);
    test_flt(box_draws_water_transmittance(INFINITY, 2), 0);
    test_assert(box_draws_water_transmittance(0.01f, 0) < 0.01f);
}

/** The C twin of water_absorbed of material.wgsl: the absorption to the power of the ray length
 * over the visibility, 1 at the surface, the absorption itself at the visibility, falling with
 * the ray length; white keeps the scene behind as it is and nothing behind is not tinted. */
void Material_water_absorption(void) {
    // materials/water
    test_flt(box_draws_water_absorption(0.25f, 0, 2), 1);
    test_assert(fabsf(box_draws_water_absorption(0.25f, 2, 2) - 0.25f) < 1e-5f);
    test_assert(fabsf(box_draws_water_absorption(0.25f, 1, 2) - 0.5f) < 1e-5f);
    test_assert(fabsf(box_draws_water_absorption(0.25f, 4, 2) - 0.0625f) < 1e-5f);
    test_assert(fabsf(box_draws_water_absorption(0.5f, 3, 1) - 0.125f) < 1e-5f);
    test_assert(box_draws_water_absorption(0.25f, 0.5f, 2) >
        box_draws_water_absorption(0.25f, 1, 2));

    /* A larger visibility tints less over the same ray */
    test_assert(box_draws_water_absorption(0.25f, 1, 4) >
        box_draws_water_absorption(0.25f, 1, 2));

    /* White keeps every channel at any ray length */
    test_flt(box_draws_water_absorption(1, 0, 2), 1);
    test_flt(box_draws_water_absorption(1, 3, 2), 1);
    test_flt(box_draws_water_absorption(1, 1e6f, 0.001f), 1);

    /* Black removes the channel past the surface, keeps it at the surface */
    test_flt(box_draws_water_absorption(0, 0, 2), 1);
    test_assert(box_draws_water_absorption(0, 1, 2) < 1e-2f);

    /* Above the surface it is not tinted, the sky behind is not tinted */
    test_flt(box_draws_water_absorption(0.25f, -1, 2), 1);
    test_flt(box_draws_water_absorption(0.25f, INFINITY, 2), 1);
    test_flt(box_draws_water_absorption(0.25f, NAN, 2), 1);
    test_assert(box_draws_water_absorption(0.25f, 0.01f, 0) < 0.01f);
}

/** The C twin of water_foam of material.wgsl: a band from 1 at the shore to 0 at foam_width,
 * none at a width of 0 or over the sky; a texture cuts the band at its red. */
void Material_water_foam(void) {
    // materials/water
    test_flt(box_draws_water_foam(0, 0.3f, false, 0), 1);
    test_assert(fabsf(box_draws_water_foam(0.15f, 0.3f, false, 0) - 0.5f) < 1e-5f);
    test_flt(box_draws_water_foam(0.3f, 0.3f, false, 0), 0);
    test_flt(box_draws_water_foam(1, 0.3f, false, 0), 0);
    test_flt(box_draws_water_foam(-1, 0.3f, false, 0), 1);

    /* No foam at a width of 0 or where nothing is behind */
    test_flt(box_draws_water_foam(0, 0, false, 0), 0);
    test_flt(box_draws_water_foam(0, -1, false, 0), 0);
    test_flt(box_draws_water_foam(INFINITY, 0.3f, false, 0), 0);
    test_flt(box_draws_water_foam(INFINITY, 0.3f, true, 1), 0);

    /* A texture keeps the band where its red is above 1 - band, softened by 0.1 */
    test_flt(box_draws_water_foam(0, 0.3f, true, 0.5f), 1);
    test_flt(box_draws_water_foam(0.15f, 0.3f, true, 0.9f), 1);
    test_flt(box_draws_water_foam(0.15f, 0.3f, true, 0.4f), 0);
    test_assert(fabsf(box_draws_water_foam(0.15f, 0.3f, true, 0.55f) - 0.5f) < 1e-4f);
    test_flt(box_draws_water_foam(0.3f, 0.3f, true, 1), 0);
    test_flt(box_draws_water_foam(0, 0, true, 1), 0);

    /* Less of the texture shows farther from the shore */
    float near_ = box_draws_water_foam(0.03f, 0.3f, true, 0.2f);
    float far_ = box_draws_water_foam(0.27f, 0.3f, true, 0.2f);
    test_assert(near_ > far_);
}

void Material_distortion_defaults(void) {
    // materials/distortion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "distortion",
        "using box.material\n"
        "haze { Distortion: {strength: 0.01, scale: 0.5, speed: 2} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Distortion);
    const Distortion *d = ecs_get(world, added, Distortion);
    test_assert(d != NULL);
    test_flt(d->strength, BOX_MATERIAL_DISTORTION_STRENGTH);
    test_flt(d->strength, 0.004f);
    test_flt(d->scale, 1);
    test_flt(d->speed, 0.5f);
    test_bool(ecs_owns(world, added, MaterialId), true);

    ecs_entity_t haze = ecs_lookup(world, "haze");
    d = ecs_get(world, haze, Distortion);
    test_assert(d != NULL);
    test_flt(d->strength, 0.01f);
    test_flt(d->scale, 0.5f);
    test_flt(d->speed, 2);
    test_bool(ecs_owns(world, haze, MaterialId), true);

    /* Reflection knows its members */
    const EcsStruct *s = ecs_get(world, ecs_id(Distortion), EcsStruct);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->members), 3);
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 0)->name, "strength");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 1)->name, "scale");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 2)->name, "speed");

    /* Instances inherit it like Rgb */
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Distortion, {.strength = 0.02f, .scale = 4, .speed = 0});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Distortion), false);
    test_flt(ecs_get(world, instance, Distortion)->strength, 0.02f);
    test_flt(ecs_get(world, instance, Distortion)->scale, 4);
    test_bool(ecs_owns(world, instance, MaterialId), false);

    /* distortion is a member of the shader features of Scene, off by default */
    ecs_singleton_add(world, Scene);
    test_bool(ecs_singleton_get(world, Scene)->shader_features.Extended.distortion, false);
    test_assert(ecs_script_run(world, "scene",
        "using box.view\n"
        "world_scene { Scene: {shader_features: {Extended: {distortion: true}}} }\n", NULL) == 0);
    test_bool(ecs_get(world, ecs_lookup(world, "world_scene"), Scene)->
        shader_features.Extended.distortion, true);

    ecs_fini(world);
}

/** The record holds the strength, the inverse scale and the wrapped phase of a Distortion; alone
 * it packs the transmission of a factor of 1 without refraction or absorption and the sharp
 * flag, with Transmission it keeps that, and Water drops it. */
void Material_distortion_pack(void) {
    // materials/distortion
    box_render_material_t record, plain;
    box_draws_tables_init();
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.distortion_strength, 0);
    test_flt(record.distortion_frequency, 0);
    test_flt(record.distortion_phase, 0);
    test_uint(record.distortion_flags, 0);
    test_flt(record.transmission_factor, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 272);
    test_int(offsetof(box_render_material_t, distortion_strength), 176);
    test_int(offsetof(box_render_material_t, distortion_flags), 188);

    Distortion haze = { .strength = 0.01f, .scale = 0.5f, .speed = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &haze, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0.01f);
    test_flt(record.distortion_frequency, 2);
    test_flt(record.distortion_phase, 0.75f);
    test_uint(record.distortion_flags,
        BOX_RENDER_DISTORTION_PRESENT | BOX_RENDER_DISTORTION_SHARP);
    test_flt(record.transmission_factor, 1);
    test_flt(record.ior, 1);
    test_flt(record.thickness_factor, 0);
    test_flt(record.attenuation_distance, BOX_MATERIAL_ATTENUATION_DISTANCE);
    test_uint(record.attenuation_color, 0xFFFFFFu);

    /* A strength of 0 keeps the shape transmissive, with no offset */
    Distortion still = { .strength = 0, .scale = 1, .speed = 0 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &still, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0);
    test_flt(record.distortion_frequency, 1);
    test_flt(record.distortion_phase, 0);
    test_uint(record.distortion_flags,
        BOX_RENDER_DISTORTION_PRESENT | BOX_RENDER_DISTORTION_SHARP);
    test_flt(record.transmission_factor, 1);

    /* With Transmission its words stay those of the Transmission */
    Transmission glass = {
        .transmission_factor = 0.5f, .ior = 1.5f, .thickness_factor = 0.25f,
        .attenuation_distance = 2, .attenuation_color = {200, 100, 50}
    };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &haze, .transmission = &glass, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0.01f);
    test_uint(record.distortion_flags, BOX_RENDER_DISTORTION_PRESENT);
    test_flt(record.transmission_factor, 0.5f);
    test_flt(record.ior, 1.5f);
    test_flt(record.thickness_factor, 0.25f);
    test_flt(record.attenuation_distance, 2);
    test_uint(record.attenuation_color, 200u | (100u << 8) | (50u << 16));

    /* With Transmission a strength of 0 packs as no Distortion */
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &still, .transmission = &glass, .time = 3, .fade = 1 });
    test_flt(record.distortion_frequency, 0);
    test_uint(record.distortion_flags, 0);
    test_flt(record.transmission_factor, 0.5f);

    /* Water ignores it */
    Water pond = { .depth = 3, .visibility = 2 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &haze, .water = &pond, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0);
    test_uint(record.distortion_flags, 0);
    test_flt(record.transmission_factor, 0);

    /* Values that are not finite or not positive are sanitized */
    Distortion broken = { .strength = NAN, .scale = -1, .speed = INFINITY };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &broken, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0);
    test_flt(record.distortion_frequency, 1.0f / BOX_RENDER_DISTORTION_MIN);
    test_flt(record.distortion_phase, 0);
    broken.strength = -0.5f;
    broken.scale = NAN;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .distortion = &broken, .time = 3, .fade = 1 });
    test_flt(record.distortion_strength, 0);
    test_flt(record.distortion_frequency, 1.0f / BOX_RENDER_DISTORTION_MIN);

    /* Without one the record is that of a material without one */
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));
}

/** The phase wraps to 0..1 in double precision, so it stays exact after hours of uptime. */
void Material_distortion_phase(void) {
    // materials/distortion
    test_flt(box_draws_distortion_phase(0.5f, 0), 0);
    test_flt(box_draws_distortion_phase(0.5f, 1.5), 0.75f);
    test_flt(box_draws_distortion_phase(-0.5f, 1.5), 0.25f);
    test_flt(box_draws_distortion_phase(0, 12345.0), 0);
    test_flt(box_draws_distortion_phase(NAN, 2), 0);
    test_flt(box_draws_distortion_phase(0.5f, INFINITY), 0);

    /* Ten hours in, a frame still moves the phase by speed times the frame time */
    double t = 36000.0 + 0.125;
    float a = box_draws_distortion_phase(0.5f, t);
    float b = box_draws_distortion_phase(0.5f, t + 1.0 / 60.0);
    test_assert(fabsf(a - 0.0625f) < 1e-5f);
    test_assert(fabsf(b - a - 0.5f / 60.0f) < 1e-5f);
    int32_t i;
    for (i = 0; i < 100; i ++) {
        float p = box_draws_distortion_phase(0.37f, 1e6 + (double)i * 0.01);
        test_assert(p >= 0.0f && p < 1.0f);
    }
}

/** A constructed Water has a turquoise scatter color and no scatter; a script sets both, and an
 * instance inherits them. */
void Material_water_scatter_defaults(void) {
    // materials/water
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "water_scatter",
        "using box.material\n"
        "sea { Water: {scatter_color: {30, 180, 150}, scatter: 1.5} }\n"
        "lake { Water: {scatter: 0.5} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Water);
    const Water *w = ecs_get(world, added, Water);
    test_assert(w != NULL);
    test_uint(w->scatter_color.r, BOX_MATERIAL_WATER_SCATTER_R);
    test_uint(w->scatter_color.g, BOX_MATERIAL_WATER_SCATTER_G);
    test_uint(w->scatter_color.b, BOX_MATERIAL_WATER_SCATTER_B);
    test_uint(w->scatter_color.r, 40);
    test_uint(w->scatter_color.g, 200);
    test_uint(w->scatter_color.b, 170);
    test_flt(w->scatter, BOX_MATERIAL_WATER_SCATTER);
    test_flt(w->scatter, 0);

    w = ecs_get(world, ecs_lookup(world, "sea"), Water);
    test_assert(w != NULL);
    test_uint(w->scatter_color.r, 30);
    test_uint(w->scatter_color.g, 180);
    test_uint(w->scatter_color.b, 150);
    test_flt(w->scatter, 1.5f);
    test_flt(w->depth, 3);

    w = ecs_get(world, ecs_lookup(world, "lake"), Water);
    test_assert(w != NULL);
    test_uint(w->scatter_color.g, 200);
    test_flt(w->scatter, 0.5f);

    /* Script access through the reflection data */
    ecs_entity_t sea = ecs_lookup(world, "sea");
    Water *mw = ecs_get_mut(world, sea, Water);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, ecs_id(Water), mw);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "scatter"), 0);
    test_int(ecs_meta_set_float(&cur, 2.0), 0);
    test_int(ecs_meta_member(&cur, "scatter_color"), 0);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "g"), 0);
    test_int(ecs_meta_set_uint(&cur, 99), 0);
    test_int(ecs_meta_pop(&cur), 0);
    test_int(ecs_meta_pop(&cur), 0);
    ecs_modified(world, sea, Water);
    w = ecs_get(world, sea, Water);
    test_flt(w->scatter, 2.0f);
    test_uint(w->scatter_color.g, 99);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, Water);
    ecs_get_mut(world, prefab, Water)->scatter = 0.75f;
    ecs_modified(world, prefab, Water);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Water), false);
    test_flt(ecs_get(world, instance, Water)->scatter, 0.75f);
    test_uint(ecs_get(world, instance, Water)->scatter_color.b, 170);

    ecs_fini(world);
}

/** The scatter of a Water takes the Sheen and DiffuseTransmission words of its record: its color
 * display encoded and its strength when positive and finite, both 0 without one, so a Water
 * without scatter packs the same words whatever its scatter color; a record without Water keeps
 * its Sheen and DiffuseTransmission. */
void Material_water_scatter_pack(void) {
    // materials/water
    box_render_material_t record, base;
    box_draws_tables_init();
    test_int(offsetof(box_render_material_t, water_scatter_color),
        offsetof(box_render_material_t, sheen));
    test_int(offsetof(box_render_material_t, water_scatter),
        offsetof(box_render_material_t, diffuse_transmission));
    test_int(ECS_SIZEOF(box_render_material_t), 272);

    Water sea = {
        .shallow_color = {60, 170, 160}, .deep_color = {8, 40, 64},
        .depth = 3, .visibility = 2, .refraction = 0.02f, .foam_color = {240, 245, 245},
        .foam_width = 0.3f, .foam_scale = 1, .scatter_color = {40, 200, 170}, .scatter = 0
    };
    box_draws_material_pack(&base, &(box_draw_material_t){ .water = &sea, .fade = 1 });
    test_uint(base.water_scatter_color, 0);
    test_flt(base.water_scatter, 0);

    /* Without scatter the color does not change the record */
    Water other = sea;
    other.scatter_color = (Rgb){1, 2, 3};
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &other, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &base, box_render_material_t));

    Water lit = sea;
    lit.scatter = 1;
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &lit, .fade = 1 });
    test_uint(record.water_scatter_color, 40u | (200u << 8) | (170u << 16));
    test_flt(record.water_scatter, 1);

    /* The scatter words differ, the rest is the record without scatter */
    record.water_scatter_color = 0;
    record.water_scatter = 0;
    test_assert(!ecs_os_memcmp_t(&record, &base, box_render_material_t));

    /* A Water ignores Sheen and DiffuseTransmission: their words hold the scatter */
    Sheen sheen = { .color = {255, 255, 255}, .roughness = 1 };
    DiffuseTransmission dt = { .factor = 1, .color = {255, 255, 255} };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &lit, .sheen = &sheen, .diffuse_transmission = &dt, .fade = 1 });
    test_uint(record.water_scatter_color, 40u | (200u << 8) | (170u << 16));
    test_flt(record.water_scatter, 1);
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &sea, .sheen = &sheen, .diffuse_transmission = &dt, .fade = 1 });
    test_uint(record.water_scatter_color, 0);
    test_flt(record.water_scatter, 0);

    /* Without Water they pack as before */
    box_draws_material_pack(&record, &(box_draw_material_t){
        .sheen = &sheen, .diffuse_transmission = &dt, .fade = 1 });
    test_assert(record.sheen != 0);
    test_assert(record.diffuse_transmission != 0);

    /* Negative or non finite strengths are none */
    Water broken = sea;
    broken.scatter = -1;
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &broken, .fade = 1 });
    test_flt(record.water_scatter, 0);
    test_uint(record.water_scatter_color, 0);
    broken.scatter = NAN;
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &broken, .fade = 1 });
    test_flt(record.water_scatter, 0);
    broken.scatter = INFINITY;
    box_draws_material_pack(&record, &(box_draw_material_t){ .water = &broken, .fade = 1 });
    test_flt(record.water_scatter, 0);

    /* A transition encodes the scatter like the rest of the Water */
    PbrMaterial pbr = { .metallic = 0, .roughness = 0.05f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &lit, .pbr = &pbr, .time = 2, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .water = &lit, .pbr = &pbr, .time = 2, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** A constructed Water has no shore waves and a shore reach of 0.3; a script sets both, they
 * are reachable through the reflection data, and an instance inherits them. */
void Material_water_shore_defaults(void) {
    // materials/water
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "water_shore",
        "using box.material\n"
        "beach { Water: {shore_period: 6, shore_reach: 0.8} }\n"
        "lagoon { Water: {shore_period: 4} }\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, Water);
    const Water *w = ecs_get(world, added, Water);
    test_assert(w != NULL);
    test_flt(w->shore_period, BOX_MATERIAL_WATER_SHORE_PERIOD);
    test_flt(w->shore_period, 0);
    test_flt(w->shore_reach, BOX_MATERIAL_WATER_SHORE_REACH);
    test_flt(w->shore_reach, 0.3f);
    test_flt(w->foam_width, BOX_MATERIAL_WATER_FOAM_WIDTH);

    w = ecs_get(world, ecs_lookup(world, "beach"), Water);
    test_assert(w != NULL);
    test_flt(w->shore_period, 6);
    test_flt(w->shore_reach, 0.8f);
    test_flt(w->depth, 3);
    test_flt(w->scatter, 0);

    w = ecs_get(world, ecs_lookup(world, "lagoon"), Water);
    test_assert(w != NULL);
    test_flt(w->shore_period, 4);
    test_flt(w->shore_reach, 0.3f);

    /* Script access through the reflection data */
    ecs_entity_t beach = ecs_lookup(world, "beach");
    Water *mw = ecs_get_mut(world, beach, Water);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, ecs_id(Water), mw);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "shore_period"), 0);
    test_int(ecs_meta_set_float(&cur, 3.5), 0);
    test_int(ecs_meta_member(&cur, "shore_reach"), 0);
    test_int(ecs_meta_set_float(&cur, 0.45), 0);
    test_int(ecs_meta_pop(&cur), 0);
    ecs_modified(world, beach, Water);
    w = ecs_get(world, beach, Water);
    test_flt(w->shore_period, 3.5f);
    test_flt(w->shore_reach, 0.45f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, Water);
    ecs_get_mut(world, prefab, Water)->shore_period = 5;
    ecs_modified(world, prefab, Water);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, Water), false);
    test_flt(ecs_get(world, instance, Water)->shore_period, 5);
    test_flt(ecs_get(world, instance, Water)->shore_reach, 0.3f);

    ecs_fini(world);
}

/** The shore waves of a Water take the DiffuseTransmission thickness and blur words of its
 * record: the phase at the time and the reach with a shore_period above 0, both 0 without one,
 * so a Water without shore waves packs the same words whatever its reach and time; a record
 * without Water keeps its DiffuseTransmission. */
void Material_water_shore_pack(void) {
    // materials/water
    box_render_material_t record, base;
    box_draws_tables_init();
    test_int(offsetof(box_render_material_t, water_shore_phase),
        offsetof(box_render_material_t, thickness));
    test_int(offsetof(box_render_material_t, water_shore_reach),
        offsetof(box_render_material_t, diffuse_blur));
    test_int(ECS_SIZEOF(box_render_material_t), 272);

    Water sea = {
        .shallow_color = {60, 170, 160}, .deep_color = {8, 40, 64},
        .depth = 3, .visibility = 2, .refraction = 0.02f, .foam_color = {240, 245, 245},
        .foam_width = 0.3f, .foam_scale = 1, .shore_period = 0, .shore_reach = 0.3f
    };
    box_draws_material_pack(&base, &(box_draw_material_t){
        .water = &sea, .time = 1.25, .fade = 1 });
    test_flt(base.water_shore_phase, 0);
    test_flt(base.water_shore_reach, 0);

    /* Without a period neither the reach nor the time change the record */
    Water other = sea;
    other.shore_reach = 2;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &other, .time = 7.5, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &base, box_render_material_t));

    /* With a period: the reach and the time over the period wrapped to 0..1 */
    Water beach = sea;
    beach.shore_period = 4;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &beach, .time = 9, .fade = 1 });
    test_assert(fabsf(record.water_shore_phase - 0.25f) < 1e-6f);
    test_flt(record.water_shore_reach, 0.3f);

    /* The shore words differ, the rest is the record without shore waves */
    record.water_shore_phase = 0;
    record.water_shore_reach = 0;
    test_assert(!ecs_os_memcmp_t(&record, &base, box_render_material_t));

    /* A whole period later the record is the same */
    box_render_material_t later;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &beach, .time = 9, .fade = 1 });
    box_draws_material_pack(&later, &(box_draw_material_t){
        .water = &beach, .time = 13, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &later, box_render_material_t));

    /* A Water ignores DiffuseTransmission: its words hold the shore waves */
    DiffuseTransmission dt = { .factor = 1, .color = {255, 255, 255}, .thickness = 0.5f,
        .blur = 0.25f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &sea, .diffuse_transmission = &dt, .time = 9, .fade = 1 });
    test_flt(record.water_shore_phase, 0);
    test_flt(record.water_shore_reach, 0);
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &beach, .diffuse_transmission = &dt, .time = 9, .fade = 1 });
    test_assert(fabsf(record.water_shore_phase - 0.25f) < 1e-6f);
    test_flt(record.water_shore_reach, 0.3f);

    /* Without Water they pack as before */
    box_draws_material_pack(&record, &(box_draw_material_t){
        .diffuse_transmission = &dt, .fade = 1 });
    test_flt(record.thickness, 0.5f);
    test_flt(record.diffuse_blur, 0.25f);

    /* Negative or non finite periods and reaches are none */
    Water broken = beach;
    broken.shore_period = -1;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 9, .fade = 1 });
    test_flt(record.water_shore_phase, 0);
    test_flt(record.water_shore_reach, 0);
    broken.shore_period = NAN;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 9, .fade = 1 });
    test_flt(record.water_shore_reach, 0);
    broken.shore_period = INFINITY;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 9, .fade = 1 });
    test_flt(record.water_shore_reach, 0);
    broken = beach;
    broken.shore_reach = -0.5f;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 9, .fade = 1 });
    test_flt(record.water_shore_phase, 0);
    test_flt(record.water_shore_reach, 0);
    broken.shore_reach = NAN;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &broken, .time = 9, .fade = 1 });
    test_flt(record.water_shore_reach, 0);

    /* A transition encodes the shore waves like the rest of the Water */
    PbrMaterial pbr = { .metallic = 0, .roughness = 0.05f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &beach, .pbr = &pbr, .time = 2, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .water = &beach, .pbr = &pbr, .time = 2, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** The phase of the shore waves is the time over the period wrapped to [0, 1) in double
 * precision: it moves on smoothly after hours of render time and is 0 for a period or a time
 * that is not usable. */
void Material_water_shore_wrap(void) {
    // materials/water
    test_flt(box_draws_water_shore_phase(0, 10), 0);
    test_flt(box_draws_water_shore_phase(-2, 10), 0);
    test_flt(box_draws_water_shore_phase(NAN, 10), 0);
    test_flt(box_draws_water_shore_phase(INFINITY, 10), 0);
    test_flt(box_draws_water_shore_phase(4, NAN), 0);
    test_flt(box_draws_water_shore_phase(4, INFINITY), 0);
    test_flt(box_draws_water_shore_phase(4, 0), 0);
    test_assert(fabsf(box_draws_water_shore_phase(4, 1) - 0.25f) < 1e-6f);
    test_assert(fabsf(box_draws_water_shore_phase(4, 6) - 0.5f) < 1e-6f);
    test_flt(box_draws_water_shore_phase(4, 8), 0);

    /* A negative time wraps to a positive phase */
    test_assert(fabsf(box_draws_water_shore_phase(4, -1) - 0.75f) < 1e-6f);

    /* After hours the phase still advances by a frame over the period every frame */
    float period = 5.5f;
    double starts[3] = { 1e6, 4.0 * 3600.0, 1e6 + 12345.678 };
    int32_t s, f;
    for (s = 0; s < 3; s ++) {
        for (f = 0; f < 600; f ++) {
            double t = starts[s] + (double)f / 60.0;
            float a = box_draws_water_shore_phase(period, t);
            float b = box_draws_water_shore_phase(period, t + 1.0 / 60.0);
            test_assert(a >= 0.0f && a < 1.0f);
            float d = b - a;
            if (d < -0.5f) {
                d += 1.0f;
            }

            test_assert(fabsf(d - 1.0f / (60.0f * period)) < 1e-4f);
        }
    }
}
