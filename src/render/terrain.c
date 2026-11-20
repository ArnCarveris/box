/**
 * @file render/terrain.c
 * @brief Extract of the terrain tiles: the heights each tile that draws needs in the surface
 * assets of its grid mesh and its masks in the mask atlas of the mesh, appended to the packet for
 * the backend (box_gpu_terrain_heights), the grid of the tiles that draw of each terrain with
 * TerrainSampled (box_gpu_terrain_samples), the bakes of the tiles with masks and the order of
 * the tiles in their draw.
 */

#include "render.h"
#include "../texture/texture.h"
#include "../terrain/terrain.h"
#include "../geometry/mesh_cache.h"
#include "draw.h"
#include <math.h>

/** Append the heights of every terrain tile that draws and whose heights the backend has.
 * Disabled tiles do not match, so a tile is copied from the frame it draws.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTerrain(
    ecs_iter_t *it)
{
    const TerrainTile *tiles = ecs_field(it, TerrainTile, 0);
    const ShapeMesh *shapes = ecs_field(it, ShapeMesh, 1);
    box_textures_t *t = box_textures_get(it->world);
    box_render_packet_t *p = NULL;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        uint32_t texture = box_textures_handle(t, tiles[i].texture);
        uint32_t rough = box_textures_handle(t, tiles[i].rough);
        if (!texture || !rough || !shapes[i].mesh) {
            continue;
        }

        p = p ? p : box_render_packet(it->world);
        *ecs_vec_append_t(NULL, &p->terrain_heights, box_render_terrain_height_t) =
            (box_render_terrain_height_t){
                .mesh = shapes[i].mesh,
                .slot = (uint32_t)tiles[i].slot,
                .texture = texture,
                .rough = rough,
                .masks = tiles[i].masks ? box_textures_handle(t, tiles[i].masks) : 0,
                .coarse = tiles[i].coarse ? box_textures_handle(t, tiles[i].coarse) : 0
            };
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
    const ecs_entity_t *entities = ecs_vec_first(&b->entities);
    int32_t count = ecs_vec_count(&b->entities), i;
    for (i = 0; i < count; i ++) {
        const TerrainTile *tile = ecs_get(d->world, entities[i], TerrainTile);
        if (!tile || !tile->masked) {
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
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    const box_render_view_t *view = NULL;
    const box_render_transform_t *t = ecs_vec_first(&b->transforms);
    const ecs_entity_t *entities = ecs_vec_first(&b->entities);
    int32_t count = ecs_vec_count(&b->transforms), i;
    uint32_t offset = 0;
    if (!count || !b->key.surface || b->key.mover || !ecs_id(TerrainTile) ||
        !ecs_is_alive(d->world, entities[0]) || !ecs_has(d->world, entities[0], TerrainTile))
    {
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
    ecs_vec_clear(&d->sorted);
    box_draw_sorted_t *sorted = ecs_vec_grow_t(NULL, &d->sorted, box_draw_sorted_t, count);
    for (i = 0; i < count; i ++) {
        float dx = fabsf(cx - t[i].rows[0][3]) - 0.5f * fabsf(t[i].rows[0][0]);
        float dz = fabsf(cz - t[i].rows[2][3]) - 0.5f * fabsf(t[i].rows[2][2]);
        dx = dx > 0.0f ? dx : 0.0f;
        dz = dz > 0.0f ? dz : 0.0f;
        sorted[i] = (box_draw_sorted_t){ .depth = dx * dx + dz * dz, .slot = i };
    }

    qsort(sorted, (size_t)count, sizeof(box_draw_sorted_t), box_draws_nearest_cmp);
    uint32_t *slots = box_render_packet_reserve(p,
        (uint32_t)count * (uint32_t)sizeof(uint32_t), &offset);
    for (i = 0; i < count; i ++) {
        slots[i] = b->first + (uint32_t)sorted[i].slot;
    }

    out->first_instance = 0;
    out->listed = true;
    out->slots = offset;
done:
    return;
}

/** Append a terrain with TerrainSampled to the packet: the windows of its levels with the slot
 * of the tile that draws at each node, from the tiles that draw this frame, whose heights the
 * extract of the tiles appends.
 *
 * @param world The world
 * @param t The textures
 * @param e The terrain
 */
static void box_render_terrain_sample(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e)
{
    box_terrain_window_t w;
    box_render_packet_t *p = box_render_packet(world);
    box_render_terrain_sample_t *s = ecs_vec_append_t(NULL, &p->terrain_samples,
        box_render_terrain_sample_t);
    double local[3];
    int32_t count = 0, i, level;
    ecs_os_memset_t(s, 0, box_render_terrain_sample_t);
    s->terrain = e;
    box_terrain_local(world, e, local);
    const WorldOrigin *o = ecs_id(WorldOrigin) ? ecs_singleton_get(world, WorldOrigin) : NULL;
    s->origin[0] = local[0] + (o ? o->x : 0.0);
    s->origin[1] = local[1] + (o ? o->y : 0.0);
    s->origin[2] = local[2] + (o ? o->z : 0.0);
    if (!box_terrain_window(world, e, &w)) {
        goto done;
    }

    const Terrain *value = ecs_get(world, e, Terrain);
    uint32_t cells = (uint32_t)(w.cells * w.cells), offset;
    uint32_t size = (uint32_t)w.levels * cells * (uint32_t)sizeof(uint32_t);
    uint32_t *grid = box_render_packet_reserve(p, size, &offset);
    s = ecs_vec_last_t(&p->terrain_samples, box_render_terrain_sample_t);
    ecs_os_memset(grid, 0, (ecs_size_t)size);
    s->grid = offset;
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
            z >= w.cells || !box_textures_handle(t, tile->texture))
        {
            continue;
        }

        const ShapeMesh *shape = ecs_get(world, tile->entity, ShapeMesh);
        if (!shape || !shape->mesh) {
            continue;
        }

        s->mesh = s->mesh ? s->mesh : shape->mesh;
        if (shape->mesh == s->mesh) {
            grid[(uint32_t)tile->level * cells + (uint32_t)(z * w.cells + x)] =
                (uint32_t)tile->slot + 1u;
        }
    }

done:
    return;
}

/** Append every terrain with TerrainSampled to the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTerrainSamples(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_terrain_sample(it->world, t, it->entities[i]);
    }
}

void box_render_terrain_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TerrainHeights" }),
        .phase = RenderExtract,
        .query.terms = {
            { .id = ecs_id(TerrainTile), .inout = EcsIn },
            { .id = ecs_id(ShapeMesh), .inout = EcsIn }
        },
        .callback = BoxRenderExtractTerrain
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TerrainSamples" }),
        .phase = RenderExtract,
        .query.terms = {
            { .id = ecs_id(Terrain), .inout = EcsIn },
            { .id = TerrainSampled }
        },
        .callback = BoxRenderExtractTerrainSamples
    });
}
