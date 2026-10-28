/**
 * @file Material.c
 * @brief Tests for the material defaults the draws write into the instance data.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

void Material_default_roughness_without_material(void) {
    // materials/pbr
    box_render_instance_t inst = {0};
    box_draw_material_t material = {0};
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0);
    test_flt(inst.material[1], 1);

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.3f };
    material.pbr = &pbr;
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0.25f);
    test_flt(inst.material[1], 0.3f);
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

    box_render_instance_t inst = {0};
    box_draws_material(&inst, &(box_draw_material_t){
        .pbr = ecs_get(world, ecs_lookup(world, "metal"), PbrMaterial)});
    test_flt(inst.material[0], 1);
    test_flt(inst.material[1], 1);

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

    box_render_instance_t inst = {0};
    AlphaCutoff mid = {0.3f}, high = {2}, low = {-1};
    box_draws_material(&inst, &(box_draw_material_t){ .fade = 1 });
    test_flt(inst.material[2], 0);
    box_draws_material(&inst, &(box_draw_material_t){ .cutoff = &mid, .fade = 1 });
    test_flt(inst.material[2], 0.3f);
    box_draws_material(&inst, &(box_draw_material_t){ .cutoff = &high, .fade = 1 });
    test_flt(inst.material[2], 1);
    box_draws_material(&inst, &(box_draw_material_t){ .cutoff = &low, .fade = 1 });
    test_flt(inst.material[2], 0);

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
