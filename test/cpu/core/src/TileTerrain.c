/**
 * @file TileTerrain.c
 * @brief Tests for the TileTerrain shape: the grid image its text makes and its plane mesh.
 */

#include <box.h>
#include <bake_test.h>
#include "texture.h"
#include "terrain.h"
#include "mesh_cache.h"

void TileTerrain_grid_pixels(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}, {'~', 0}),
        .tiles = "\n  .#~\n  ##\n"});

    const TileTerrainGrid *grid = ecs_get(world, e, TileTerrainGrid);
    test_assert(grid != NULL);
    test_int(grid->columns, 3);
    test_int(grid->rows, 2);
    test_assert(ecs_has_pair(world, grid->texture, EcsChildOf, e));
    const TexturePixels *pixels = ecs_get(world, grid->texture, TexturePixels);
    test_assert(pixels != NULL);
    test_int(pixels->width, 3);
    test_int(pixels->height, 2);
    test_bool(pixels->single_channel, true);
    static const uint8_t expect[6] = { 0, 1, 2, 1, 1, 0 };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        test_int(pixels->pixels[i], expect[i]);
    }

    ecs_fini(world);
}

void TileTerrain_whitespace_ignored(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}, {'~', 0}),
        .tiles = " . # \n\n\t~ ~ ~\r\n"});

    const TileTerrainGrid *grid = ecs_get(world, e, TileTerrainGrid);
    test_assert(grid != NULL);
    test_int(grid->columns, 3);
    test_int(grid->rows, 2);
    const TexturePixels *pixels = ecs_get(world, grid->texture, TexturePixels);
    test_assert(pixels != NULL);
    static const uint8_t expect[6] = { 0, 1, 0, 2, 2, 2 };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        test_int(pixels->pixels[i], expect[i]);
    }

    ecs_fini(world);
}

void TileTerrain_unknown_symbol(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_log_set_level(-4);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}),
        .tiles = "#a\n.#"});

    const TileTerrainGrid *grid = ecs_get(world, e, TileTerrainGrid);
    test_assert(grid != NULL);
    test_int(grid->columns, 2);
    test_int(grid->rows, 2);
    const TexturePixels *pixels = ecs_get(world, grid->texture, TexturePixels);
    test_assert(pixels != NULL);
    static const uint8_t expect[4] = { 1, 0, 0, 1 };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_int(pixels->pixels[i], expect[i]);
    }

    ecs_fini(world);
}

void TileTerrain_shape_scale(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {.size = 2,
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}),
        .tiles = "...\n..."});

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 6.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 4.0f);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 4);
    int32_t i;
    for (i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        test_flt(v->position[1], 0.0f);
        test_flt(v->normal[0], 0.0f);
        test_flt(v->normal[1], 1.0f);
        test_flt(v->normal[2], 0.0f);
        test_flt(v->uv[0], v->position[0] + 0.5f);
        test_flt(v->uv[1], v->position[2] + 0.5f);
    }

    for (i = 0; i < mesh->index_count; i += 3) {
        const box_vertex_t *a = &mesh->vertices[mesh->indices[i]];
        const box_vertex_t *b = &mesh->vertices[mesh->indices[i + 1]];
        const box_vertex_t *c = &mesh->vertices[mesh->indices[i + 2]];
        vec3 ab, ac, face;
        glm_vec3_sub((float*)b->position, (float*)a->position, ab);
        glm_vec3_sub((float*)c->position, (float*)a->position, ac);
        glm_vec3_cross(ab, ac, face);
        test_assert(face[1] > 0.0f);
    }

    ecs_fini(world);
}

void TileTerrain_size_default(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}),
        .tiles = "...\n..."});

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 3.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 2.0f);

    ecs_fini(world);
}

void TileTerrain_set_again(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}),
        .tiles = "..\n.."});
    ecs_entity_t texture = ecs_get(world, e, TileTerrainGrid)->texture;

    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}),
        .tiles = "#.#"});

    const TileTerrainGrid *grid = ecs_get(world, e, TileTerrainGrid);
    test_assert(grid->texture == texture);
    test_int(grid->columns, 3);
    test_int(grid->rows, 1);
    const TexturePixels *pixels = ecs_get(world, texture, TexturePixels);
    test_int(pixels->width, 3);
    test_int(pixels->height, 1);
    test_int(pixels->pixels[0], 1);
    test_int(pixels->pixels[1], 0);
    test_int(pixels->pixels[2], 1);
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_flt(sm->scale[0], 3.0f);
    test_flt(sm->scale[2], 1.0f);

    ecs_fini(world);
}

void TileTerrain_texture_ready(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}, {'#', 0}),
        .tiles = ".#.\n#.#"});
    ecs_entity_t texture = ecs_get(world, e, TileTerrainGrid)->texture;
    ecs_progress(world, 0);

    const Texture *size = ecs_get(world, texture, Texture);
    test_assert(size != NULL);
    test_int(size->width, 3);
    test_int(size->height, 2);
    const TextureStatus *status = ecs_get(world, texture, TextureStatus);
    test_assert(status != NULL);
    test_int(status->state, TextureReady);
    box_textures_t *t = box_textures_get(world);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, texture);
    test_assert(entry != NULL);
    test_bool(entry->single_channel, true);
    test_int(entry->width, 3);
    test_int(entry->height, 2);
    test_int(entry->pixels[1], 1);
    test_int(entry->pixels[3], 1);

    ecs_fini(world);
}

void TileTerrain_remove_deletes_grid(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}),
        .tiles = ".."});
    ecs_entity_t texture = ecs_get(world, e, TileTerrainGrid)->texture;
    test_assert(ecs_is_alive(world, texture));

    ecs_remove(world, e, TileTerrain);
    test_assert(!ecs_is_alive(world, texture));
    test_assert(!ecs_has(world, e, TileTerrainGrid));
    box_textures_t *t = box_textures_get(world);
    test_assert(ecs_map_get(&t->textures, texture) == NULL);

    ecs_fini(world);
}

void TileTerrain_delete_releases_grid(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TileTerrain, {
        .kinds = BOX_VEC_VIEW(TileKind, {'.', 0}),
        .tiles = ".."});
    ecs_entity_t texture = ecs_get(world, e, TileTerrainGrid)->texture;
    box_textures_t *t = box_textures_get(world);
    test_assert(ecs_map_get(&t->textures, texture) != NULL);

    ecs_delete(world, e);
    test_assert(!ecs_is_alive(world, texture));
    test_assert(ecs_map_get(&t->textures, texture) == NULL);

    ecs_fini(world);
}

void TileTerrain_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "test",
        "using box.terrain\n"
        "grass {}\n"
        "road {}\n"
        "terrain {\n"
        "  TileTerrain: {size: 0.5, kinds: [{'.', grass}, {'#', road}], tiles: `\n"
        "    ..#\n"
        "    .##\n"
        "  `}\n"
        "}\n", NULL) == 0);

    ecs_entity_t e = ecs_lookup(world, "terrain");
    test_assert(e != 0);
    const TileTerrain *terrain = ecs_get(world, e, TileTerrain);
    test_assert(terrain != NULL);
    test_flt(terrain->size, 0.5f);
    test_int(ecs_vec_count(&terrain->kinds), 2);
    test_assert(ecs_vec_get_t(&terrain->kinds, TileKind, 1)->symbol == '#');
    test_assert(ecs_vec_get_t(&terrain->kinds, TileKind, 1)->textures ==
        ecs_lookup(world, "road"));
    const TileTerrainGrid *grid = ecs_get(world, e, TileTerrainGrid);
    test_int(grid->columns, 3);
    test_int(grid->rows, 2);
    const TexturePixels *pixels = ecs_get(world, grid->texture, TexturePixels);
    static const uint8_t expect[6] = { 0, 0, 1, 0, 1, 1 };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        test_int(pixels->pixels[i], expect[i]);
    }

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_flt(sm->scale[0], 1.5f);
    test_flt(sm->scale[2], 1.0f);

    ecs_fini(world);
}
