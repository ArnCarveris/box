/**
 * @file render/terrain.c
 * @brief Extract of the terrain tiles: the heights each tile that draws needs in the surface
 * assets of its grid mesh and its masks in the mask atlas of the mesh, appended to the packet for
 * the backend (box_gpu_terrain_heights), and the grid of the tiles that draw of each terrain with
 * TerrainSampled (box_gpu_terrain_samples).
 */

#include "render.h"
#include "../texture/texture.h"
#include "../terrain/terrain.h"
#include "../geometry/mesh_cache.h"

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
        if (!texture || !shapes[i].mesh) {
            continue;
        }

        p = p ? p : box_render_packet(it->world);
        *ecs_vec_append_t(NULL, &p->terrain_heights, box_render_terrain_height_t) =
            (box_render_terrain_height_t){
                .mesh = shapes[i].mesh,
                .slot = (uint32_t)tiles[i].slot,
                .texture = texture,
                .masks = tiles[i].masks ? box_textures_handle(t, tiles[i].masks) : 0,
                .coarse = tiles[i].coarse ? box_textures_handle(t, tiles[i].coarse) : 0
            };
    }
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
