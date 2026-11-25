/**
 * @file render/terrain.c
 * @brief Draw path of the terrain tiles: the tiles that draw of a terrain are the instances of a
 * batch of its own (box_render_terrain_draws), written from the tiles box.terrain keeps, which are
 * not entities. The extract appends the heights each tile that draws needs in the surface assets
 * of its grid mesh and its masks in the mask atlas of the mesh to the packet for the backend
 * (box_gpu_terrain_heights), the grid of the tiles that draw of each terrain with TerrainSampled
 * (box_gpu_terrain_samples), the bakes of the tiles with masks and the order of the tiles in
 * their draw.
 */

#include "render.h"
#include "../texture/texture.h"
#include "../terrain/terrain.h"
#include "../geometry/mesh_cache.h"
#include "draw.h"
#include <math.h>

/** A terrain with TerrainSampled as it was last appended to a packet */
typedef struct box_render_terrain_cache_t {
    box_render_terrain_sample_t sample; /**< The terrain, its grid not set */
    uint32_t *grid;         /**< The grid of the windows of its levels, NULL without one */
    uint32_t size;          /**< Bytes of grid */
    int32_t updates;        /**< Updates of the tiles of the terrain the grid was built at */
    int32_t version;        /**< Version of the heights the grid was built at */
    int64_t frame;          /**< Frame it was last appended in */
    bool complete;          /**< Every tile that draws had its heights and its mesh */
    bool valid;             /**< The other members are set */
} box_render_terrain_cache_t;

/** The instances of the tiles of a terrain in its batch */
typedef struct box_render_terrain_draw_t {
    ecs_entity_t terrain;   /**< The terrain entity */
    int32_t batch;          /**< Index of its batch, -1 for none */
    uint32_t mesh;          /**< Grid mesh of its tiles, a reference taken from the mesh cache,
                             *   0 for none */
    int32_t vertices;       /**< Vertices per edge of the grid mesh */
    int32_t version;        /**< Version of the tiles its instances were written for
                             *   (box_terrain_tiles_version), -1 to write them again */
    box_render_mesh_shading_t shading; /**< Shading of every instance */
    bool masked;            /**< Its material takes the world mapping of a terrain with masks and
                             *   a TextureBlend without a TextureMapping */
    ecs_vec_t tiles;        /**< TerrainTile per slot of its batch */
    int64_t tick;           /**< Extract of the draws that last saw the terrain */
} box_render_terrain_draw_t;

/** State of the extract of the terrain tiles */
typedef struct box_render_terrain_t {
    ecs_query_t *terrains;  /**< The terrains, whose tiles draw */
    ecs_map_t draws;        /**< Terrain entity to its box_render_terrain_draw_t */
    bool dirty;             /**< The instances of a terrain were written since heights was
                             *   built */
    ecs_vec_t heights;      /**< box_render_terrain_height_t, the heights last built */
    ecs_map_t samples;      /**< Terrain entity to its box_render_terrain_cache_t */
    int32_t version;        /**< Bumped when heights is built */
    int32_t builds;         /**< Times heights was built */
    int32_t grids;          /**< Times the grid of a sampled terrain was built */
    bool complete;          /**< Every tile that draws had its heights and its mesh, so heights
                             *   holds until the instances of a terrain are written */
} box_render_terrain_t;

/** Singleton that holds the state of the extract of the terrain tiles */
typedef struct RenderTerrain {
    box_render_terrain_t *state; /**< The state */
} RenderTerrain;

static ECS_COMPONENT_DECLARE(RenderTerrain);

void box_draws_terrain_key(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t terrain,
    const ShapeMesh *shape,
    box_draw_key_t *key,
    box_draw_material_t *material)
{
    DrawSlot slot = { .batch = -1, .slot = -1, .fade = 1.0f, .tint = { .keep = 1.0f } };
    box_draws_shape_material(world, terrain, &slot, material);
    material->inherited = NULL;
    d->scene_table = NULL;
    box_draws_key(world, d, terrain, shape, box_draws_alpha(material), key);
    d->scene_table = NULL;
    key->mover = false;
    key->dynamic_transform = false;
    key->frame = false;
    key->terrain = terrain;
}

/** Get the state of the extract of the terrain tiles.
 *
 * @param world The world
 * @return The state, NULL before the extract is imported
 */
static box_render_terrain_t* box_render_terrain_state(
    const ecs_world_t *world)
{
    const RenderTerrain *m = ecs_id(RenderTerrain)
        ? ecs_singleton_get(world, RenderTerrain) : NULL;

    return m ? m->state : NULL;
}

/** Get the instances of the tiles of a terrain.
 *
 * @param state The state of the extract
 * @param terrain The terrain entity
 * @return The instances, NULL when the terrain draws no tiles
 */
static box_render_terrain_draw_t* box_render_terrain_draw_get(
    const box_render_terrain_t *state,
    ecs_entity_t terrain)
{
    return state ? ecs_map_get_deref(&state->draws, box_render_terrain_draw_t, terrain) : NULL;
}

/** Take the instances of the tiles of a terrain out of its batch, which is freed when empty.
 *
 * @param world The world
 * @param d The state of the draws
 * @param state The state of the extract
 * @param td The instances
 */
static void box_render_terrain_draw_empty(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_terrain_t *state,
    box_render_terrain_draw_t *td)
{
    box_draw_batch_t *b = td->batch >= 0 ? box_draws_batch(d, td->batch) : NULL;
    if (b && b->alive && b->key.terrain == td->terrain) {
        while (ecs_vec_count(&b->entities)) {
            box_draws_slot_remove(world, d, td->batch, ecs_vec_count(&b->entities) - 1);
        }
    }

    state->dirty = state->dirty || ecs_vec_count(&td->tiles) > 0;
    td->batch = -1;
    td->version = -1;
    ecs_vec_clear(&td->tiles);
}

/** Take the instances of the tiles of a terrain out of its batch and release its grid mesh.
 *
 * @param world The world
 * @param d The state of the draws
 * @param state The state of the extract
 * @param cache The mesh cache
 * @param td The instances
 */
static void box_render_terrain_draw_clear(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_terrain_t *state,
    box_mesh_cache_t *cache,
    box_render_terrain_draw_t *td)
{
    box_render_terrain_draw_empty(world, d, state, td);
    box_mesh_cache_release(cache, td->mesh);
    td->mesh = 0;
    td->vertices = 0;
}

/** Check whether the material of a terrain takes the world mapping of a terrain with masks and a
 * TextureBlend that has no TextureMapping (box_draws_material_of).
 *
 * @param world The world
 * @param e The terrain
 * @return Whether it does
 */
static bool box_render_terrain_masked(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const Terrain *t = ecs_get(world, e, Terrain);

    return t && t->masks && ecs_has(world, e, TextureBlend) && !ecs_has(world, e, TextureMapping);
}

/** Write the instance of a tile into a slot of the batch of its terrain: the tile at its world
 * position scaled to its size, with the surface that tells the vertex stage where its heights are
 * and how it morphs, recording the regions of a caster slot whose instance changed.
 *
 * @param world The world
 * @param d The state of the draws
 * @param b The batch
 * @param slot The slot
 * @param fresh Whether the slot was just added
 * @param tile The tile
 * @param shape The grid mesh of the tiles
 * @return Whether the instance changed
 */
static bool box_render_terrain_draw_tile(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot,
    bool fresh,
    const box_terrain_tile_t *tile,
    ShapeMesh *shape)
{
    WorldTransform world_transform = {
        .position = { tile->position[0], tile->position[1], tile->position[2] },
        .basis = {{ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }}
    };
    box_render_transform_t model, was_transform;
    box_render_surface_t surface, was_surface;
    float size = isfinite(tile->shape.size) && tile->shape.size > 0.0f ? tile->shape.size : 1.0f;
    bool result = fresh;
    glm_vec3_copy((vec3){ size, 1.0f, size }, shape->scale);
    box_terrain_tile_surface(&tile->shape, &shape->surface, shape->min, shape->max);
    box_draws_transform(&model, &world_transform, shape);
    ecs_os_memset_t(&surface, 0, box_render_surface_t);
    box_draws_surface(&surface, shape);
    box_render_transform_t *transform = box_draws_slot_transform(b, slot);
    box_render_surface_t *own = box_draws_slot_surface(b, slot);
    result = result || ecs_os_memcmp_t(transform, &model, box_render_transform_t) ||
        ecs_os_memcmp_t(own, &surface, box_render_surface_t);
    if (!result) {
        goto done;
    }

    was_transform = *transform;
    was_surface = *own;
    *transform = model;
    *own = surface;
    if (!d->shadow_record || !box_draws_caster(b)) {
        goto done;
    }

    if (fresh) {
        box_draws_shadow_slot(world, d, b, slot);
    } else {
        box_draws_shadow_region(world, d, b, &was_transform, &was_surface);
        box_draws_shadow_region(world, d, b, transform, own);
    }

done:
    return result;
}

/** Bring the tiles that draw of a terrain into its batch.
 *
 * The key and the shading follow the components of the terrain, computed in every extract; the
 * instances are written again when the version of the tiles, the key or the shading changed.
 *
 * @param world The world
 * @param d The state of the draws
 * @param state The state of the extract
 * @param cache The mesh cache
 * @param e The terrain
 */
static void box_render_terrain_draw(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_terrain_t *state,
    box_mesh_cache_t *cache,
    ecs_entity_t e)
{
    box_render_terrain_draw_t *td = box_render_terrain_draw_get(state, e);
    box_draw_material_t material;
    box_render_mesh_shading_t shading;
    box_draw_key_t key;
    ShapeMesh shape;
    int32_t count = 0, drawn = 0, first = -1, i, slot = 0;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, e, &count);
    if (!td) {
        td = ecs_map_insert_alloc_t(&state->draws, box_render_terrain_draw_t, e);
        ecs_os_memset_t(td, 0, box_render_terrain_draw_t);
        td->terrain = e;
        td->batch = -1;
        td->version = -1;
        ecs_vec_init_t(NULL, &td->tiles, TerrainTile, 0);
    }

    td->tick = d->tick;
    for (i = 0; i < count; i ++) {
        if (tiles[i]->enabled) {
            first = first < 0 ? i : first;
            drawn ++;
        }
    }

    int32_t vertices = drawn ? box_terrain_vertices(tiles[first]->shape.resolution) : 0;
    if (!drawn || vertices != td->vertices) {
        box_render_terrain_draw_clear(world, d, state, cache, td);
    }

    if (!drawn) {
        goto done;
    }

    if (!td->mesh) {
        const box_shape_t *grid = box_mesh_cache_shape(cache, ecs_id(TerrainTile));
        td->mesh = grid ? box_mesh_cache_acquire(cache, grid, &tiles[first]->shape) : 0;
        td->vertices = vertices;
    }

    if (!td->mesh) {
        goto done;
    }

    ecs_os_memset_t(&shape, 0, ShapeMesh);
    shape.mesh = td->mesh;
    glm_vec3_one(shape.scale);
    box_terrain_tile_surface(&tiles[first]->shape, &shape.surface, shape.min, shape.max);
    box_draws_terrain_key(world, d, e, &shape, &key, &material);
    bool masked = box_render_terrain_masked(world, e);
    if (masked != td->masked) {
        box_draws_material_changed(d, e);
        td->masked = masked;
    }

    box_draw_batch_t *b = td->batch >= 0 ? box_draws_batch(d, td->batch) : NULL;
    if (!b || !b->alive || ecs_os_memcmp_t(&b->key, &key, box_draw_key_t)) {
        box_render_terrain_draw_empty(world, d, state, td);
        td->batch = box_draws_batch_ensure(d, &key);
    }

    ecs_os_memset_t(&shading, 0, box_render_mesh_shading_t);
    box_draws_mesh_shading(&shading, box_draws_planet_material(
        box_draws_material_id(world, d, e, true, key.dynamic_material), key.planet), &material);
    int32_t version = box_terrain_tiles_version(world, e);
    bool shaded = td->version == -1 ||
        ecs_os_memcmp_t(&shading, &td->shading, box_render_mesh_shading_t);
    if (version == td->version && !shaded) {
        goto done;
    }

    b = box_draws_batch(d, td->batch);
    int32_t had = ecs_vec_count(&b->entities);
    while (ecs_vec_count(&b->entities) > drawn) {
        box_draws_slot_remove(world, d, td->batch, ecs_vec_count(&b->entities) - 1);
    }

    while (ecs_vec_count(&b->entities) < drawn) {
        box_draws_slot_add(d, td->batch, e);
    }

    bool written = version != td->version;
    ecs_vec_set_count_t(NULL, &td->tiles, TerrainTile, drawn);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *tile = tiles[i];
        uint8_t kinds = 0;
        if (!tile->enabled) {
            continue;
        }

        bool fresh = slot >= had;
        if ((fresh || written) &&
            box_render_terrain_draw_tile(world, d, b, slot, fresh, tile, &shape))
        {
            kinds |= BOX_DRAW_TRANSFORM | BOX_DRAW_SURFACE;
        }

        if (fresh || shaded) {
            *box_draws_slot_mesh_shading(b, slot) = shading;
            kinds |= BOX_DRAW_SHADING;
        }

        *ecs_vec_get_t(&td->tiles, TerrainTile, slot) = tile->shape;
        if (kinds) {
            box_draws_slot_changed(b, slot, kinds);
        }

        slot ++;
    }

    state->dirty = state->dirty || written;
    td->version = version;
    td->shading = shading;
done:
    return;
}

void box_render_terrain_draws(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_render_terrain_t *state = box_render_terrain_state(world);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t i;
    if (!state || !cache) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->terrains);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_render_terrain_draw(world, d, state, cache, it.entities[i]);
        }
    }

    ecs_entity_t stale;
    do {
        ecs_map_iter_t mit = ecs_map_iter(&state->draws);
        box_render_terrain_draw_t *td = NULL;
        stale = 0;
        while (!stale && ecs_map_next(&mit)) {
            td = ecs_map_ptr(&mit);
            stale = td->tick != d->tick ? ecs_map_key(&mit) : 0;
        }

        if (stale) {
            box_render_terrain_draw_clear(world, d, state, cache, td);
            if (ecs_is_alive(world, stale)) {
                box_draws_material_release(world, d, stale);
            }

            ecs_vec_fini_t(NULL, &td->tiles, TerrainTile);
            ecs_map_remove_free(&state->draws, stale);
        }
    } while (stale);

done:
    return;
}

/** Append the heights of every terrain tile that draws and whose heights the backend has.
 *
 * The list is built again only when the instances of a terrain were written, or while a tile
 * that draws waits for a texture or its mesh; else the list built before is appended.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTerrain(
    ecs_iter_t *it)
{
    box_render_terrain_t *state = it->ctx;
    ecs_world_t *world = it->world;
    int32_t i, count;
    if (state->dirty || !state->complete) {
        box_textures_t *t = box_textures_get(world);
        ecs_map_iter_t mit = ecs_map_iter(&state->draws);
        ecs_vec_clear(&state->heights);
        state->complete = true;
        state->dirty = false;
        state->version ++;
        state->builds ++;
        while (ecs_map_next(&mit)) {
            const box_render_terrain_draw_t *td = ecs_map_ptr(&mit);
            const TerrainTile *tiles = ecs_vec_first(&td->tiles);
            for (i = 0; i < ecs_vec_count(&td->tiles); i ++) {
                uint32_t texture = box_textures_handle(t, tiles[i].texture);
                uint32_t rough = box_textures_handle(t, tiles[i].rough);
                uint32_t masks = tiles[i].masks ? box_textures_handle(t, tiles[i].masks) : 0;
                uint32_t coarse = tiles[i].coarse
                    ? box_textures_handle(t, tiles[i].coarse) : 0;
                state->complete = state->complete && texture && rough && td->mesh &&
                    (masks || !tiles[i].masks) && (coarse || !tiles[i].coarse);
                if (!texture || !rough || !td->mesh) {
                    continue;
                }

                *ecs_vec_append_t(NULL, &state->heights, box_render_terrain_height_t) =
                    (box_render_terrain_height_t){
                        .mesh = td->mesh,
                        .slot = (uint32_t)tiles[i].slot,
                        .texture = texture,
                        .rough = rough,
                        .masks = masks,
                        .coarse = coarse
                    };
            }
        }
    }

    count = ecs_vec_count(&state->heights);
    if (count) {
        box_render_packet_t *p = box_render_packet(world);
        ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &p->terrain_heights, box_render_terrain_height_t,
            count), ecs_vec_first(&state->heights), box_render_terrain_height_t, count);
    }
}

/** Get the index of the tiles of a terrain in a packet, appended on first use: the tiles that
 * draw by level (box_render_terrain_index_t).
 *
 * @param p The packet
 * @param world The world
 * @param terrain The terrain entity
 * @return Index of the terrain index in terrain_indices, -1 without tiles
 */
static int32_t box_render_terrain_index(
    box_render_packet_t *p,
    const ecs_world_t *world,
    ecs_entity_t terrain)
{
    const box_render_terrain_index_t *indices = ecs_vec_first(&p->terrain_indices);
    int32_t i, count = ecs_vec_count(&p->terrain_indices), tiles = 0, levels = 0, result = -1;
    int32_t lo[BOX_TERRAIN_LEVELS][2], hi[BOX_TERRAIN_LEVELS][2];
    uint32_t words, offset = 0, cells = 0;
    for (i = 0; i < count; i ++) {
        if (indices[i].terrain == terrain) {
            result = i;
            goto done;
        }
    }

    box_terrain_tile_t* const* list = box_terrain_tiles(world, terrain, &tiles);
    const Terrain *t = ecs_get(world, terrain, Terrain);
    if (!list || !t) {
        goto done;
    }

    for (i = 0; i < tiles; i ++) {
        const box_terrain_tile_t *tile = list[i];
        int32_t l = tile->level;
        if (!tile->drawn || l < 0 || l >= BOX_TERRAIN_LEVELS) {
            continue;
        }

        if (l >= levels) {
            int32_t k;
            for (k = levels; k <= l; k ++) {
                lo[k][0] = lo[k][1] = INT32_MAX;
                hi[k][0] = hi[k][1] = INT32_MIN;
            }

            levels = l + 1;
        }

        lo[l][0] = tile->ix < lo[l][0] ? tile->ix : lo[l][0];
        lo[l][1] = tile->iz < lo[l][1] ? tile->iz : lo[l][1];
        hi[l][0] = tile->ix > hi[l][0] ? tile->ix : hi[l][0];
        hi[l][1] = tile->iz > hi[l][1] ? tile->iz : hi[l][1];
    }

    if (!levels) {
        goto done;
    }

    for (i = 0; i < levels; i ++) {
        if (lo[i][0] > hi[i][0]) {
            lo[i][0] = lo[i][1] = 0;
            hi[i][0] = hi[i][1] = -1;
        }

        cells += (uint32_t)((hi[i][0] - lo[i][0] + 1) * (hi[i][1] - lo[i][1] + 1));
    }

    words = 4u + 4u * (uint32_t)levels + cells;
    uint32_t *r = box_render_packet_reserve(p, words * (uint32_t)sizeof(uint32_t), &offset);
    float size = isfinite(t->tile_size) && t->tile_size > 0.0f ? t->tile_size : 32.0f;
    ecs_os_memset(r, 0, (ecs_size_t)(words * sizeof(uint32_t)));
    r[0] = (uint32_t)levels;
    ecs_os_memcpy(&r[1], &size, ECS_SIZEOF(float));
    uint32_t at = 4u + 4u * (uint32_t)levels;
    for (i = 0; i < levels; i ++) {
        int32_t nx = hi[i][0] - lo[i][0] + 1, nz = hi[i][1] - lo[i][1] + 1;
        r[4 + 4 * i] = (uint32_t)lo[i][0];
        r[5 + 4 * i] = (uint32_t)lo[i][1];
        r[6 + 4 * i] = (uint32_t)nx | ((uint32_t)nz << 16);
        r[7 + 4 * i] = at;
        at += (uint32_t)(nx * nz);
    }

    for (i = 0; i < tiles; i ++) {
        const box_terrain_tile_t *tile = list[i];
        int32_t l = tile->level;
        if (!tile->drawn || l < 0 || l >= levels) {
            continue;
        }

        int32_t nx = hi[l][0] - lo[l][0] + 1;
        uint32_t cell = r[7 + 4 * l] +
            (uint32_t)((tile->iz - lo[l][1]) * nx + (tile->ix - lo[l][0]));
        r[cell] = (uint32_t)tile->slot + 1u;
    }

    result = count;
    *ecs_vec_append_t(NULL, &p->terrain_indices, box_render_terrain_index_t) =
        (box_render_terrain_index_t){ .terrain = terrain, .data = offset, .count = words };
done:
    return result;
}

/** Append a bake of every tile of a terrain batch that draws with masks, with the index of the
 * tiles of its terrain.
 *
 * @param p The packet
 * @param d The draw state
 * @param b The batch of the tiles
 */
static void box_render_terrain_bakes(
    box_render_packet_t *p,
    box_render_draws_t *d,
    const box_draw_batch_t *b)
{
    const box_render_terrain_draw_t *td = box_render_terrain_draw_get(
        box_render_terrain_state(d->world), b->key.terrain);
    int32_t count = td ? ecs_vec_count(&td->tiles) : 0, i;
    count = count < ecs_vec_count(&b->entities) ? count : ecs_vec_count(&b->entities);
    for (i = 0; i < count; i ++) {
        const TerrainTile *tile = ecs_vec_get_t(&td->tiles, TerrainTile, i);
        if (!tile->masked) {
            continue;
        }

        int32_t index = box_render_terrain_index(p, d->world, tile->terrain);
        if (index < 0) {
            continue;
        }

        *ecs_vec_append_t(NULL, &p->terrain_bakes, box_render_terrain_bake_t) =
            (box_render_terrain_bake_t){
                .mesh = b->key.mesh,
                .slot = (uint32_t)tile->slot,
                .batch = b->record,
                .instance = b->first + (uint32_t)i,
                .index = index,
                .level = tile->level,
                .ix = tile->ix,
                .iz = tile->iz
            };
    }
}

/** Order instances by their distance to the camera, nearest first, then by slot.
 *
 * @param a The first instance
 * @param b The second instance
 * @return Negative when a draws first
 */
static int box_draws_nearest_cmp(
    const void *a,
    const void *b)
{
    const box_draw_sorted_t *sa = a, *sb = b;
    int result = (sa->depth > sb->depth) - (sa->depth < sb->depth);
    if (!result) {
        result = (sa->slot > sb->slot) - (sa->slot < sb->slot);
    }

    return result;
}

void box_draws_send_nearest(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    box_render_batch_t *out)
{
    const box_render_view_t *view = NULL;
    const box_render_transform_t *t = ecs_vec_first(&b->transforms);
    int32_t count = ecs_vec_count(&b->transforms), i;
    uint32_t offset = 0;
    if (!count || !b->key.terrain) {
        goto done;
    }

    if (out->texture_blend) {
        box_render_terrain_bakes(p, d, b);
    }

    if (count < 2) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&p->views) && !view; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        view = v->scene == b->key.scene ? v : NULL;
    }

    if (!view) {
        goto done;
    }

    float cx = view->camera_transform[3][0], cz = view->camera_transform[3][2];
    if (b->nearest_dirty || b->key.dynamic_transform || ecs_vec_count(&b->nearest) != count ||
        b->nearest_at[0] != cx || b->nearest_at[1] != cz)
    {
        ecs_vec_clear(&d->sorted);
        box_draw_sorted_t *sorted = ecs_vec_grow_t(NULL, &d->sorted, box_draw_sorted_t, count);
        for (i = 0; i < count; i ++) {
            vec3 offset;
            box_render_transform_offset(&t[i], d->origin, offset);
            float dx = fabsf(cx - offset[0]) - 0.5f * fabsf(t[i].rows[0][0]);
            float dz = fabsf(cz - offset[2]) - 0.5f * fabsf(t[i].rows[2][2]);
            dx = dx > 0.0f ? dx : 0.0f;
            dz = dz > 0.0f ? dz : 0.0f;
            sorted[i] = (box_draw_sorted_t){ .depth = dx * dx + dz * dz, .slot = i };
        }

        qsort(sorted, (size_t)count, sizeof(box_draw_sorted_t), box_draws_nearest_cmp);
        ecs_vec_set_count_t(NULL, &b->nearest, uint32_t, count);
        uint32_t *made = ecs_vec_first_t(&b->nearest, uint32_t);
        for (i = 0; i < count; i ++) {
            made[i] = (uint32_t)sorted[i].slot;
        }

        b->nearest_dirty = false;
        b->nearest_at[0] = cx;
        b->nearest_at[1] = cz;
    }

    const uint32_t *order = ecs_vec_first_t(&b->nearest, uint32_t);
    uint32_t *slots = box_render_packet_reserve(p,
        (uint32_t)count * (uint32_t)sizeof(uint32_t), &offset);
    for (i = 0; i < count; i ++) {
        slots[i] = b->first + order[i];
    }

    out->first_instance = 0;
    out->listed = true;
    out->slots = offset;
done:
    return;
}

/** Build a terrain with TerrainSampled: the windows of its levels with the slot of the tile
 * that draws at each node, from the tiles that draw this frame, whose heights the extract of the
 * tiles appends.
 *
 * @param world The world
 * @param t The textures
 * @param e The terrain
 * @param mesh Grid mesh of the tiles of the terrain, 0 for none
 * @param c Receives the terrain and its grid
 */
static void box_render_terrain_sample_build(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    uint32_t mesh,
    box_render_terrain_cache_t *c)
{
    box_terrain_window_t w;
    box_render_terrain_sample_t *s = &c->sample;
    int32_t count = 0, i, level;
    ecs_os_memset_t(s, 0, box_render_terrain_sample_t);
    s->terrain = e;
    c->complete = true;
    c->size = 0;
    if (!box_terrain_window(world, e, &w)) {
        goto done;
    }

    const Terrain *value = ecs_get(world, e, Terrain);
    uint32_t cells = (uint32_t)(w.cells * w.cells);
    c->size = (uint32_t)w.levels * cells * (uint32_t)sizeof(uint32_t);
    c->grid = ecs_os_realloc(c->grid, (ecs_size_t)(c->size ? c->size : 1));
    uint32_t *grid = c->grid;
    ecs_os_memset(grid, 0, (ecs_size_t)c->size);
    s->levels = w.levels;
    s->cells = w.cells;
    s->resolution = w.resolution;
    s->focus_count = w.focus_count;
    s->masks = value && value->masks;
    s->tile_size = w.tile_size;
    s->extent = w.extent;
    ecs_os_memcpy_n(s->foci, w.foci, double, BOX_RENDER_TERRAIN_FOCI * 2);
    for (level = 0; level < w.levels; level ++) {
        s->first[level][0] = w.first[level][0];
        s->first[level][1] = w.first[level][1];
        s->morph[level][0] = w.morph[level][0];
        s->morph[level][1] = w.morph[level][1];
    }

    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, e, &count);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *tile = tiles[i];
        int32_t x = tile->ix - w.first[tile->level][0], z = tile->iz - w.first[tile->level][1];
        if (!tile->enabled || tile->level >= w.levels || x < 0 || z < 0 || x >= w.cells ||
            z >= w.cells)
        {
            continue;
        }

        if (!box_textures_handle(t, tile->texture) || !mesh) {
            c->complete = false;
            continue;
        }

        s->mesh = mesh;
        grid[(uint32_t)tile->level * cells + (uint32_t)(z * w.cells + x)] =
            (uint32_t)tile->slot + 1u;
    }

done:
    return;
}

/** Append a terrain with TerrainSampled to the packet, at the position it has now.
 *
 * It is built again only when the tiles of the terrain were updated, the heights of the tiles
 * that draw were built again or a tile that draws waits for a texture or its mesh.
 *
 * @param world The world
 * @param state The state of the extract
 * @param t The textures
 * @param e The terrain
 */
static void box_render_terrain_sample(
    ecs_world_t *world,
    box_render_terrain_t *state,
    box_textures_t *t,
    ecs_entity_t e)
{
    box_render_packet_t *p = box_render_packet(world);
    box_render_terrain_cache_t *c = ecs_map_get_deref(&state->samples,
        box_render_terrain_cache_t, e);
    int32_t updates, selects;
    if (!c) {
        c = ecs_os_calloc_t(box_render_terrain_cache_t);
        ecs_map_insert_ptr(&state->samples, e, c);
    }

    box_terrain_counters(world, e, &updates, &selects);
    if (!c->valid || !c->complete || c->updates != updates || c->version != state->version) {
        const box_render_terrain_draw_t *td = box_render_terrain_draw_get(state, e);
        box_render_terrain_sample_build(world, t, e, td ? td->mesh : 0, c);
        c->valid = true;
        c->updates = updates;
        c->version = state->version;
        state->grids ++;
    }

    c->frame = ecs_get_world_info(world)->frame_count_total;
    box_terrain_position(world, e, c->sample.origin);
    box_render_terrain_sample_t *s = ecs_vec_append_t(NULL, &p->terrain_samples,
        box_render_terrain_sample_t);
    *s = c->sample;
    if (c->size) {
        uint32_t offset;
        uint32_t *grid = box_render_packet_reserve(p, c->size, &offset);
        ecs_os_memcpy(grid, c->grid, (ecs_size_t)c->size);
        s = ecs_vec_last_t(&p->terrain_samples, box_render_terrain_sample_t);
        s->grid = offset;
    }
}

/** Free a sampled terrain as it was last appended.
 *
 * @param c The terrain
 */
static void box_render_terrain_cache_free(
    box_render_terrain_cache_t *c)
{
    ecs_os_free(c->grid);
    ecs_os_free(c);
}

/** Append every terrain with TerrainSampled to the packet, and forget those that no longer
 * have the tag.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTerrainSamples(
    ecs_iter_t *it)
{
    box_render_terrain_t *state = it->ctx;
    box_textures_t *t = box_textures_get(it->world);
    int64_t frame = ecs_get_world_info(it->world)->frame_count_total;
    int32_t i, count = 0;
    while (ecs_iter_next(it)) {
        for (i = 0; i < it->count; i ++) {
            box_render_terrain_sample(it->world, state, t, it->entities[i]);
        }

        count += it->count;
    }

    while (ecs_map_count(&state->samples) > count) {
        ecs_map_iter_t mit = ecs_map_iter(&state->samples);
        ecs_entity_t stale = 0;
        while (!stale && ecs_map_next(&mit)) {
            const box_render_terrain_cache_t *c = ecs_map_ptr(&mit);
            stale = c->frame != frame ? ecs_map_key(&mit) : 0;
        }

        if (!stale) {
            break;
        }

        box_render_terrain_cache_free(ecs_map_get_deref(&state->samples,
            box_render_terrain_cache_t, stale));
        ecs_map_remove(&state->samples, stale);
    }
}

/** Free the state of the extract of the terrain tiles.
 *
 * @param ptr The state
 */
static void box_render_terrain_free(
    void *ptr)
{
    box_render_terrain_t *state = ptr;
    ecs_map_iter_t it = ecs_map_iter(&state->samples);
    while (ecs_map_next(&it)) {
        box_render_terrain_cache_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&state->samples);
    it = ecs_map_iter(&state->draws);
    while (ecs_map_next(&it)) {
        box_render_terrain_draw_t *td = ecs_map_ptr(&it);
        ecs_vec_fini_t(NULL, &td->tiles, TerrainTile);
        ecs_os_free(td);
    }

    ecs_map_fini(&state->draws);
    ecs_vec_fini_t(NULL, &state->heights, box_render_terrain_height_t);
    ecs_os_free(state);
}

void box_render_terrain_counters(
    const ecs_world_t *world,
    int32_t *heights,
    int32_t *grids)
{
    const RenderTerrain *m = ecs_id(RenderTerrain)
        ? ecs_singleton_get(world, RenderTerrain) : NULL;
    *heights = m && m->state ? m->state->builds : 0;
    *grids = m && m->state ? m->state->grids : 0;
}

void box_render_terrain_init(
    ecs_world_t *world)
{
    box_render_terrain_t *state = ecs_os_calloc_t(box_render_terrain_t);
    ecs_vec_init_t(NULL, &state->heights, box_render_terrain_height_t, 0);
    ecs_map_init(&state->samples, NULL);
    ECS_COMPONENT_DEFINE(world, RenderTerrain);
    ecs_singleton_set(world, RenderTerrain, { .state = state });
    ecs_map_init(&state->draws, NULL);
    state->terrains = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "extract.Terrains" }),
        .terms = {{ .id = ecs_id(Terrain), .src.id = EcsSelf, .inout = EcsInOutFilter }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TerrainHeights" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTerrain,
        .ctx = state,
        .ctx_free = box_render_terrain_free
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TerrainSamples" }),
        .phase = RenderExtract,
        .query.terms = {
            { .id = ecs_id(Terrain), .inout = EcsIn },
            { .id = TerrainSampled }
        },
        .run = BoxRenderExtractTerrainSamples,
        .ctx = state
    });
}
