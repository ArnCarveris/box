/**
 * @file render/terrain.c
 * @brief Extract of the terrain tiles: the heights each tile that draws needs in the surface
 * assets of its grid mesh and its masks in the mask atlas of the mesh, appended to the packet for
 * the backend (box_gpu_terrain_heights), and the order of the tiles in their draw.
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
    if (count < 2 || !b->key.surface || b->key.mover || !ecs_id(TerrainTile) ||
        !ecs_is_alive(d->world, entities[0]) || !ecs_has(d->world, entities[0], TerrainTile))
    {
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
