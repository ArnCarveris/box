/**
 * @file TerrainDraw.c
 * @brief Tests for the draw path of the tiles of a Terrain: the tiles that draw are the instances
 * of one batch of the terrain, keyed and shaded from the terrain entity, without tile entities.
 *
 * Every case renders with the null backend, which computes no image, so the cases mark the
 * heights of the tiles ready by writing the TextureStatus of their textures.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"
#include "terrain.h"

void TerrainDraw_tiles_one_batch(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_add_id(world, camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 3, .lod_distance = 48, .budget = 1000});
    ecs_progress(world, 0);
    int32_t count, i, j;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        ecs_set(world, tiles[i]->texture, TextureStatus, {.state = TextureReady, .revision = 1});
        ecs_set(world, tiles[i]->rough, TextureStatus, {.state = TextureReady, .revision = 1});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status->drawn > 0);
    test_int(ecs_count_id(world, ecs_id(TerrainTile)), 0);

    box_render_draws_t *d = box_draws_state(world);
    const box_draw_batch_t *b = NULL;
    int32_t batches = 0;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        const box_draw_batch_t *cur = box_draws_batch(d, i);
        if (cur->alive && cur->key.terrain == terrain) {
            b = cur;
            batches ++;
        }
    }

    test_int(batches, 1);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_TERRAIN);
    test_bool(b->key.surface, true);
    test_int(ecs_vec_count(&b->entities), status->drawn);

    // Every slot is a tile that draws: its heights slot and its size
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < ecs_vec_count(&b->entities); i ++) {
        const box_render_surface_t *surface = ecs_vec_get_t(&b->surfaces,
            box_render_surface_t, i);
        test_flt(surface->p[0][0], BOX_TERRAIN_SURFACE);
        int32_t found = -1;
        for (j = 0; j < count; j ++) {
            if (tiles[j]->enabled && (float)tiles[j]->slot == surface->p[1][0]) {
                found = j;
            }
        }

        test_assert(found >= 0);
        test_flt(surface->p[4][0], tiles[found]->shape.size);
    }

    // The packet draws the batch with an instance per tile, nearest first
    const box_render_batch_t *draw = NULL;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *cur = ecs_vec_get_t(&null->last.batches,
            box_render_batch_t, i);
        if (cur->mesh == b->key.mesh && cur->view == -1) {
            draw = cur;
        }
    }

    test_assert(draw != NULL);
    test_int(draw->instance_count, status->drawn);
    test_bool(draw->listed, true);
    ecs_fini(world);
    box_render_null_free(null);
}

void TerrainDraw_key_and_material_of_terrain(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_add_id(world, camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2, .lod_distance = 32, .budget = 1000});
    ecs_set(world, terrain, Rgb, {10, 200, 30});
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        ecs_set(world, tiles[i]->texture, TextureStatus, {.state = TextureReady, .revision = 1});
        ecs_set(world, tiles[i]->rough, TextureStatus, {.state = TextureReady, .revision = 1});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_t *d = box_draws_state(world);
    const box_draw_batch_t *b = NULL;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        const box_draw_batch_t *cur = box_draws_batch(d, i);
        b = cur->alive && cur->key.terrain == terrain ? cur : b;
    }

    test_assert(b != NULL);
    test_bool(b->key.shadow, true);

    // The instances render with the material slot the terrain owns
    const MaterialId *id = ecs_get(world, terrain, MaterialId);
    test_assert(id != NULL);
    for (i = 0; i < ecs_vec_count(&b->entities); i ++) {
        const box_render_mesh_shading_t *shading = ecs_vec_get_t(&b->mesh_shadings,
            box_render_mesh_shading_t, i);
        test_uint(shading->material, id->value);
    }

    // NoShadow on the terrain moves its tiles to a batch that casts no shadow
    ecs_add_id(world, terrain, NoShadow);
    ecs_progress(world, 0);
    box_render_sync(world);
    b = NULL;
    int32_t batches = 0;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        const box_draw_batch_t *cur = box_draws_batch(d, i);
        if (cur->alive && cur->key.terrain == terrain) {
            b = cur;
            batches ++;
        }
    }

    test_int(batches, 1);
    test_bool(b->key.shadow, false);
    test_assert(ecs_vec_count(&b->entities) > 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void TerrainDraw_remove_empties_batch(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_add_id(world, camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2, .lod_distance = 32, .budget = 1000});
    ecs_progress(world, 0);
    int32_t count, i, batches = 0;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        ecs_set(world, tiles[i]->texture, TextureStatus, {.state = TextureReady, .revision = 1});
        ecs_set(world, tiles[i]->rough, TextureStatus, {.state = TextureReady, .revision = 1});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_t *d = box_draws_state(world);
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        const box_draw_batch_t *cur = box_draws_batch(d, i);
        batches += cur->alive && cur->key.terrain == terrain;
    }

    test_int(batches, 1);
    ecs_remove(world, terrain, Terrain);
    ecs_progress(world, 0);
    box_render_sync(world);
    batches = 0;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        const box_draw_batch_t *cur = box_draws_batch(d, i);
        batches += cur->alive && cur->key.terrain == terrain &&
            ecs_vec_count(&cur->entities) > 0;
    }

    test_int(batches, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void TerrainDraw_instances_follow_tiles(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_add_id(world, camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 3, .lod_distance = 48, .budget = 1000});
    int32_t count, i, j, step;
    for (step = 0; step < 2; step ++) {
        if (step) {
            ecs_set(world, camera, Position, {1000, 20, -500});
        }

        ecs_progress(world, 0);
        box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
        for (i = 0; i < count; i ++) {
            ecs_set(world, tiles[i]->texture, TextureStatus, {
                .state = TextureReady, .revision = 1});
            ecs_set(world, tiles[i]->rough, TextureStatus, {
                .state = TextureReady, .revision = 1});
        }

        for (i = 0; i < 4; i ++) {
            ecs_progress(world, 0);
        }

        box_render_sync(world);
        box_render_draws_t *d = box_draws_state(world);
        const box_draw_batch_t *b = NULL;
        for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
            const box_draw_batch_t *cur = box_draws_batch(d, i);
            b = cur->alive && cur->key.terrain == terrain ? cur : b;
        }

        test_assert(b != NULL);
        test_int(ecs_vec_count(&b->entities), ecs_get(world, terrain, TerrainStatus)->drawn);

        // Every instance stands at the center of a tile that draws, scaled to its size
        tiles = box_terrain_tiles(world, terrain, &count);
        for (i = 0; i < ecs_vec_count(&b->entities); i ++) {
            const box_render_transform_t *t = ecs_vec_get_t(&b->transforms,
                box_render_transform_t, i);
            const box_render_surface_t *surface = ecs_vec_get_t(&b->surfaces,
                box_render_surface_t, i);
            dvec3 position;
            box_render_transform_position(t, position);
            int32_t found = -1;
            for (j = 0; j < count; j ++) {
                if (tiles[j]->enabled && (float)tiles[j]->slot == surface->p[1][0]) {
                    found = j;
                }
            }

            test_assert(found >= 0);
            test_assert(fabs(position[0] - tiles[found]->position[0]) < 0.01);
            test_assert(fabs(position[2] - tiles[found]->position[2]) < 0.01);
            test_flt(t->rows[0][0], tiles[found]->shape.size);
            test_flt(t->rows[2][2], tiles[found]->shape.size);
        }
    }

    ecs_fini(world);
    box_render_null_free(null);
}
