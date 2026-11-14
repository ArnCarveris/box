/**
 * @file terrain/terrain.c
 * @brief The TileTerrain shape: a unit plane scaled to its grid, and the grid as an image.
 *
 * Setting a TileTerrain writes the kind index of every tile into the TexturePixels of a child
 * texture entity, which the render extract binds as the mask of the texture_blend variant.
 */

#define BOX_TERRAIN_IMPL
#include "terrain.h"
#include "../texture/texture.h"
#include <math.h>

ECS_COMPONENT_DECLARE(TileTerrainGrid);

/** Check whether a character of the grid is whitespace.
 *
 * @param ch The character
 * @return Whether it separates symbols
 */
static bool box_terrain_space(
    char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
}

void box_terrain_dims(
    const char *tiles,
    int32_t *columns,
    int32_t *rows)
{
    int32_t width = 0, count = 0, longest = 0;
    const char *ch;
    for (ch = tiles; ch && *ch; ch ++) {
        if (*ch == '\n') {
            count += width > 0;
            longest = width > longest ? width : longest;
            width = 0;
        } else if (!box_terrain_space(*ch)) {
            width ++;
        }
    }

    count += width > 0;
    longest = width > longest ? width : longest;
    *columns = longest > 0 ? longest : 1;
    *rows = count > 0 ? count : 1;
}

/** Get the edge of a tile of a TileTerrain.
 *
 * @param t The terrain
 * @return Its size, 1 when it is not positive and finite
 */
static float box_terrain_size(
    const TileTerrain *t)
{
    return isfinite(t->size) && t->size > 0.0f ? t->size : 1.0f;
}

/** Key of the TileTerrain mesh: every terrain shares the unit plane.
 *
 * @param value The terrain
 * @param key The key
 */
static void box_terrain_key(
    const void *value,
    box_shape_key_t *key)
{
    (void)value;
    (void)key;
}

/** Generate the unit plane of a TileTerrain: y up, u along +x and v along +z.
 *
 * @param value The terrain
 * @param key The key
 * @param mesh The mesh
 */
static void box_terrain_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    static const float corners[4][2] = {
        { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f }
    };
    int32_t i;
    (void)value;
    (void)key;
    for (i = 0; i < 4; i ++) {
        box_mesh_vertex(mesh, (vec3){ corners[i][0], 0.0f, corners[i][1] },
            (vec3){ 0.0f, 1.0f, 0.0f }, corners[i][0] + 0.5f, corners[i][1] + 0.5f);
    }

    box_mesh_triangle(mesh, 0, 2, 1);
    box_mesh_triangle(mesh, 0, 3, 2);
}

/** Scale of a TileTerrain: its tiles along x and z times their size.
 *
 * @param value The terrain
 * @param scale The scale
 */
static void box_terrain_scale(
    const void *value,
    vec3 scale)
{
    const TileTerrain *t = value;
    int32_t columns, rows;
    float size = box_terrain_size(t);
    box_terrain_dims(t->tiles, &columns, &rows);
    glm_vec3_copy((vec3){ (float)columns * size, 1.0f, (float)rows * size }, scale);
}

/** Write the kind index of every tile of a TileTerrain, reporting the first unknown symbol.
 *
 * @param world The world
 * @param e The terrain entity
 * @param t The terrain
 * @param columns Tiles along x
 * @param rows Tiles along z
 * @return The R8 rows of the grid, owned by the caller
 */
static uint8_t* box_terrain_pixels(
    const ecs_world_t *world,
    ecs_entity_t e,
    const TileTerrain *t,
    int32_t columns,
    int32_t rows)
{
    int16_t index[256];
    const TileKind *kinds = ecs_vec_first(&t->kinds);
    int32_t i, count = ecs_vec_count(&t->kinds), x = 0, z = 0;
    uint8_t *result = ecs_os_calloc((ecs_size_t)columns * rows);
    char unknown = 0;
    const char *ch;
    for (i = 0; i < 256; i ++) {
        index[i] = -1;
    }

    for (i = count < BOX_TERRAIN_KINDS ? count : BOX_TERRAIN_KINDS; i > 0; i --) {
        index[(uint8_t)kinds[i - 1].symbol] = (int16_t)(i - 1);
    }

    for (ch = t->tiles; ch && *ch; ch ++) {
        if (*ch == '\n') {
            z += x > 0;
            x = 0;
        } else if (!box_terrain_space(*ch) && z < rows && x < columns) {
            int16_t k = index[(uint8_t)*ch];
            unknown = k < 0 && !unknown ? *ch : unknown;
            result[z * columns + x] = (uint8_t)(k < 0 ? 0 : k);
            x ++;
        }
    }

    if (unknown) {
        char *name = ecs_get_path(world, e);
        ecs_err("TileTerrain '%s': unknown tile symbol '%c'", name, unknown);
        ecs_os_free(name);
    }

    if (count > BOX_TERRAIN_KINDS) {
        char *name = ecs_get_path(world, e);
        ecs_err("TileTerrain '%s': %d kinds, at most %d draw", name, count,
            BOX_TERRAIN_KINDS);
        ecs_os_free(name);
    }

    return result;
}

/** Write the grid image of every set TileTerrain into its child texture entity, made on the
 * first set.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnSet(
    ecs_iter_t *it)
{
    const TileTerrain *terrains = ecs_field(it, TileTerrain, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const TileTerrainGrid *grid = ecs_get(it->world, e, TileTerrainGrid);
        ecs_entity_t texture = grid && ecs_is_alive(it->world, grid->texture)
            ? grid->texture : 0;
        int32_t columns, rows;
        box_terrain_dims(terrains[i].tiles, &columns, &rows);
        if (!texture) {
            texture = ecs_new_w_pair(it->stage, EcsChildOf, e);
        }

        ecs_set(it->stage, e, TileTerrainGrid, {
            .texture = texture, .columns = columns, .rows = rows });
        TexturePixels *pixels = ecs_ensure(it->stage, texture, TexturePixels);
        ecs_os_free(pixels->pixels);
        pixels->width = columns;
        pixels->height = rows;
        pixels->single_channel = true;
        pixels->pixels = box_terrain_pixels(it->world, e, &terrains[i], columns, rows);
        ecs_modified(it->stage, texture, TexturePixels);
    }
}

/** Delete the grid texture of a terrain that loses its TileTerrain.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnRemove(
    ecs_iter_t *it)
{
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        const TileTerrainGrid *grid = ecs_get(it->world, it->entities[i], TileTerrainGrid);
        if (grid && ecs_is_alive(it->world, grid->texture)) {
            ecs_delete(it->stage, grid->texture);
        }

        ecs_remove(it->stage, it->entities[i], TileTerrainGrid);
    }

done:
    return;
}

void BoxTerrainImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTerrain);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxGeometry);

    ECS_META_COMPONENT(world, TileKind);
    ECS_META_COMPONENT(world, TileTerrain);
    box_owned_hooks(world, TileTerrain, BOX_OWNED_VEC(TileTerrain, kinds, TileKind),
        BOX_OWNED_STR(TileTerrain, tiles));
    ECS_COMPONENT_DEFINE(world, TileTerrainGrid);
    ecs_add_pair(world, ecs_id(TileTerrainGrid), EcsOnInstantiate, EcsDontInherit);

    box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(TileTerrain),
        .key = box_terrain_key,
        .generate = box_terrain_generate,
        .scale = box_terrain_scale
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnSet" }),
        .query.terms = {{ .id = ecs_id(TileTerrain) }},
        .events = { EcsOnSet },
        .callback = BoxTerrainOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnRemove" }),
        .query.terms = {{ .id = ecs_id(TileTerrain) }},
        .events = { EcsOnRemove },
        .callback = BoxTerrainOnRemove
    });
}
