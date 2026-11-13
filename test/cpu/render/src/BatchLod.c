/**
 * @file BatchLod.c
 * @brief Tests for the meshes of the levels of detail the extract sends with an opaque batch
 * (box_render_batch_t.lods) when a view of the packet has LodSettings.
 *
 * Every case renders with the null backend and reads the packets it received. The batches
 * stay keyed by the mesh of level 0; the GPU picks the level of each instance.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include "mesh_cache.h"

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @param levels Levels of detail of the view, 0 for none
 * @return The world
 */
static ecs_world_t* lod_world_new(
    box_render_null_t **null,
    int32_t levels)
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
    ecs_set(world, view, View, {.camera = camera,
        .lod = {.levels = levels, .distances = {0, 10, 20, 40}}});

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void lod_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Find the draw of the batch of a shape in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape
 * @return The draw
 */
static const box_render_batch_t* lod_draw_of(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);
    const box_draw_batch_t *b = box_draws_batch(box_draws_state(world), slot->batch);
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->mesh == b->key.mesh && draw->view == -1 &&
            draw->first_instance == b->first)
        {
            result = draw;
        }
    }

    test_assert(result != NULL);

    return result;
}

void BatchLod_none_without_view_lod(void) {
    box_render_null_t *null;
    ecs_world_t *world = lod_world_new(&null, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    lod_frame(world);

    const box_render_batch_t *draw = lod_draw_of(world, null, e);
    test_uint(draw->mesh, ecs_get(world, e, ShapeMesh)->mesh);
    test_uint(draw->lods[0], draw->mesh);
    for (int32_t l = 1; l < BOX_LOD_LEVELS_MAX; l ++) {
        test_uint(draw->lods[l], 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchLod_levels_of_view(void) {
    box_render_null_t *null;
    ecs_world_t *world = lod_world_new(&null, 3);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    lod_frame(world);

    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    const box_render_batch_t *draw = lod_draw_of(world, null, e);
    test_uint(draw->mesh, mesh);
    test_uint(draw->lods[0], mesh);
    test_uint(draw->lods[1], box_mesh_cache_lod(cache, mesh, 1));
    test_uint(draw->lods[2], box_mesh_cache_lod(cache, mesh, 2));
    test_uint(draw->lods[3], 0);
    test_assert(draw->lods[1] != mesh);
    test_assert(draw->lods[2] != draw->lods[1]);
    test_assert(box_mesh_cache_get(cache, draw->lods[2])->index_count <
        box_mesh_cache_get(cache, mesh)->index_count);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchLod_box_not_reduced(void) {
    box_render_null_t *null;
    ecs_world_t *world = lod_world_new(&null, 4);
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, {3, 0, 0});
    ecs_set(world, cylinder, Cylinder, {.segments = 32, .smooth = false, .length = 1,
        .radius = 0.5f});
    lod_frame(world);

    const box_render_batch_t *draw = lod_draw_of(world, null, box);
    for (int32_t l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        test_uint(draw->lods[l], draw->mesh);
    }

    draw = lod_draw_of(world, null, cylinder);
    for (int32_t l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        test_uint(draw->lods[l], draw->mesh);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchLod_blended_level_0(void) {
    box_render_null_t *null;
    ecs_world_t *world = lod_world_new(&null, 3);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    ecs_set(world, e, Opacity, {0.5f});
    lod_frame(world);

    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    int32_t i, found = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->mesh == mesh) {
            test_assert(draw->view != -1);
            for (int32_t l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
                test_uint(draw->lods[l], 0);
            }

            found ++;
        }
    }

    test_assert(found > 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchLod_released_with_shape(void) {
    box_render_null_t *null;
    ecs_world_t *world = lod_world_new(&null, 4);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    lod_frame(world);
    int32_t before = box_mesh_cache_count(cache);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    lod_frame(world);
    test_assert(box_mesh_cache_count(cache) > before + 1);

    ecs_delete(world, e);
    lod_frame(world);
    lod_frame(world);
    test_int(box_mesh_cache_count(cache), before);

    ecs_fini(world);
    box_render_null_free(null);
}
