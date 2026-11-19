/**
 * @file render/terrain.c
 * @brief Extract of the terrain tiles: the heights each tile that draws needs in the surface
 * assets of its grid mesh and its masks in the mask atlas of the mesh, appended to the packet for
 * the backend (box_gpu_terrain_heights).
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
}
