/**
 * @file Terrain.c
 * @brief Tests for the tiles of a Terrain without a GPU: the tiles the focus wants, made nearest
 * first within the budget, the TextureGraph and the shape of a tile, the coarse tiles kept until
 * the finer ones are ready, tiles dropped within the budget, origin shifts and materials.
 *
 * The null backend computes no image, so the tests mark the heights of a tile ready by writing
 * the TextureStatus of its texture.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "texture.h"
#include "terrain.h"
#include "mesh_cache.h"

/** Edge of a tile of level 0 in the tests */
#define TT_TILE (16.0f)

/** Range of level 0 in the tests */
#define TT_RANGE (48.0f)

/** Create a world with a focus at the origin and a terrain of a constant height node.
 *
 * @param terrain Receives the terrain
 * @param focus Receives the focus
 * @param budget Tiles made per frame
 * @return The world
 */
static ecs_world_t* tt_world(
    ecs_entity_t *terrain,
    ecs_entity_t *focus,
    int32_t budget)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *focus = ecs_new(world);
    ecs_set(world, *focus, Position, {0, 10, 0});
    ecs_add_id(world, *focus, WorldFocus);
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    *terrain = ecs_new(world);
    ecs_set(world, *terrain, Terrain, {.height = node, .tile_size = TT_TILE, .resolution = 17,
        .levels = 4, .lod_distance = TT_RANGE, .budget = budget});

    return world;
}

/** Mark the heights of every tile of a terrain ready.
 *
 * @param world The world
 * @param terrain The terrain
 */
static void tt_ready(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        ecs_set(world, tiles[i]->texture, TextureStatus, {
            .state = TextureReady, .revision = 1 });
    }
}

/** Mark the masks and the coarse masks of every tile of a terrain ready, or loading as before
 * their first image (without a render backend a graph is ready at once).
 *
 * @param world The world
 * @param terrain The terrain
 * @param ready Whether they are ready
 */
static void tt_masks_ready(
    ecs_world_t *world,
    ecs_entity_t terrain,
    bool ready)
{
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        test_assert(tiles[i]->masks != 0);
        test_assert(tiles[i]->coarse != 0);
        ecs_set(world, tiles[i]->masks, TextureStatus, {
            .state = ready ? TextureReady : TextureLoading, .revision = ready });
        ecs_set(world, tiles[i]->coarse, TextureStatus, {
            .state = ready ? TextureReady : TextureLoading, .revision = ready });
    }
}

/** Give a terrain a masks node of a constant vector.
 *
 * @param world The world
 * @param terrain The terrain
 * @return The masks node
 */
static ecs_entity_t tt_masks(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {1, 0, 0, 0}});
    Terrain *t = ecs_get_mut(world, terrain, Terrain);
    t->masks = node;
    ecs_modified(world, terrain, Terrain);

    return node;
}

/** Get the distance from a point to the rectangle of a tile.
 *
 * @param t The tile
 * @param x X of the point
 * @param z Z of the point
 * @return Meters
 */
static double tt_distance(
    const box_terrain_tile_t *t,
    double x,
    double z)
{
    double size = TT_TILE * (double)(1 << t->level);
    double x0 = t->ix * size, z0 = t->iz * size;
    double dx = fmax(fmax(x0 - x, x - (x0 + size)), 0.0);
    double dz = fmax(fmax(z0 - z, z - (z0 + size)), 0.0);

    return sqrt(dx * dx + dz * dz);
}

/** Check that the drawn tiles of a terrain do not overlap.
 *
 * @param tiles The tiles
 * @param count Number of tiles
 */
static void tt_expect_disjoint(
    box_terrain_tile_t *const *tiles,
    int32_t count)
{
    int32_t i, j;
    for (i = 0; i < count; i ++) {
        for (j = 0; j < count; j ++) {
            const box_terrain_tile_t *a = tiles[i], *b = tiles[j];
            if (i == j || !a->drawn || !b->drawn || a->level < b->level) {
                continue;
            }

            int32_t shift = a->level - b->level;
            int32_t bx = b->ix >> shift, bz = b->iz >> shift;
            test_assert(!(bx == a->ix && bz == a->iz));
        }
    }
}

void Terrain_budget_nearest_first(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 6);
    ecs_progress(world, 0);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status != NULL);
    test_int(status->count, 6);
    test_int(status->created, 6);
    test_int(status->drawn, 0);
    test_assert(status->pending >= 6);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 6);
    for (i = 0; i < count; i ++) {
        test_int(tiles[i]->level, 0);
        test_assert(tt_distance(tiles[i], 0, 0) <= TT_TILE);
        test_bool(tiles[i]->drawn, false);
        test_assert(ecs_has_id(world, tiles[i]->entity, EcsDisabled));
    }

    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->created, 12);
    ecs_fini(world);
}

void Terrain_levels_follow_ranges(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    int32_t count, i, levels[4] = {0};
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    test_int(ecs_get(world, terrain, TerrainStatus)->pending, count);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        double d = tt_distance(t, 0, 0);
        levels[t->level] ++;
        if (t->level > 0) {
            test_assert(d >= TT_RANGE * (double)(1 << (t->level - 1)));
        }

        test_assert(d < TT_RANGE * (double)(1 << t->level) +
            TT_TILE * (double)(1 << t->level) * 1.5);
    }

    for (i = 0; i < 4; i ++) {
        test_assert(levels[i] > 0);
    }

    ecs_fini(world);
}

void Terrain_tile_graph_window(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1);
    ecs_progress(world, 0);
    int32_t count;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 1);
    const box_terrain_tile_t *t = tiles[0];
    const TextureGraph *graph = ecs_get(world, t->texture, TextureGraph);
    test_assert(graph != NULL);
    test_assert(ecs_has_pair(world, t->texture, EcsChildOf, t->entity));
    test_int(graph->output, ecs_get(world, terrain, Terrain)->height);
    test_int(graph->domain, DomainWorld);
    test_int(graph->format, GraphFloat);
    test_int(graph->width, 17 + 2 * BOX_TERRAIN_BORDER);
    test_int(graph->height, 17 + 2 * BOX_TERRAIN_BORDER);
    double cell = TT_TILE / 16.0;
    test_flt(graph->window.x, t->ix * TT_TILE - 2 * cell);
    test_flt(graph->window.z, t->iz * TT_TILE - 2 * cell);
    test_flt(graph->window.width, (float)(cell * 20));
    test_flt(graph->window.depth, (float)(cell * 20));
    ecs_fini(world);
}

void Terrain_tile_shape(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1);
    ecs_progress(world, 0);
    int32_t count, n = 17;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 1);
    const box_terrain_tile_t *t = tiles[0];
    const ShapeMesh *sm = ecs_get(world, t->entity, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], TT_TILE);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], TT_TILE);
    test_flt(sm->surface.p[0][0], BOX_TERRAIN_SURFACE);
    test_flt(sm->surface.p[1][0], (float)t->slot);
    test_flt(sm->surface.p[1][1], (float)(box_terrain_row_bytes(n) / 4));
    test_flt(sm->surface.p[1][2], (float)(n + 2 * BOX_TERRAIN_BORDER));
    test_flt(sm->surface.p[1][3], (float)n);
    test_flt(sm->surface.p[2][3], TT_RANGE);
    test_flt(sm->surface.p[4][0], TT_TILE);
    test_flt(sm->surface.p[4][3], 0.0f);

    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, n * n + 4 * n);
    test_int(mesh->index_count, (n - 1) * (n - 1) * 6 + 4 * (n - 1) * 6);
    test_int(mesh->surface_asset_count, 1);
    const Position *p = ecs_get(world, t->entity, Position);
    test_flt(p->x, (t->ix + 0.5f) * TT_TILE);
    test_flt(p->z, (t->iz + 0.5f) * TT_TILE);
    ecs_fini(world);
}

void Terrain_ready_tiles_draw(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    tt_ready(world, terrain);
    ecs_progress(world, 0);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    int32_t count, i, sum = 0;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(status->pending, 0);
    test_int(status->drawn, count);
    for (i = 0; i < BOX_TERRAIN_LEVELS; i ++) {
        sum += status->tiles[i];
    }

    test_int(sum, count);
    for (i = 0; i < count; i ++) {
        test_bool(tiles[i]->drawn, true);
        test_assert(!ecs_has_id(world, tiles[i]->entity, EcsDisabled));
    }

    tt_expect_disjoint(tiles, count);
    ecs_fini(world);
}

void Terrain_coarse_kept_until_ready(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    tt_ready(world, terrain);
    ecs_progress(world, 0);

    // The focus moves 200 m: the finest tiles there are pending, the coarser tiles that cover
    // them keep drawing and nothing overlaps
    ecs_set(world, focus, Position, {200, 10, 0});
    ecs_progress(world, 0);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status->pending > 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    bool covered = false;
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        covered |= t->drawn && t->level > 0 && tt_distance(t, 200, 0) == 0.0;
        test_bool(t->drawn && !t->ready, false);
    }

    test_bool(covered, true);
    tt_expect_disjoint(tiles, count);

    tt_ready(world, terrain);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->pending, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    bool fine = false;
    for (i = 0; i < count; i ++) {
        fine |= tiles[i]->drawn && tiles[i]->level == 0 && tt_distance(tiles[i], 200, 0) == 0.0;
        test_bool(tiles[i]->wanted || tiles[i]->keep, true);
    }

    test_bool(fine, true);
    tt_expect_disjoint(tiles, count);
    ecs_fini(world);
}

void Terrain_drops_within_budget(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    tt_ready(world, terrain);
    ecs_progress(world, 0);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    int32_t before = status->count;
    test_assert(before > 16);
    test_int(status->dropped, 0);

    // A smaller budget keeps the tiles; far away 2 tiles are made and 8 dropped per frame
    Terrain *t = ecs_get_mut(world, terrain, Terrain);
    t->budget = 2;
    ecs_modified(world, terrain, Terrain);
    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->count, before);
    test_int(status->dropped, 0);

    ecs_set(world, focus, Position, {100000, 10, 0});
    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->dropped, 8);
    test_int(status->count, before - 8 + 2);
    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->dropped, 16);
    test_int(status->count, before - 16 + 4);
    ecs_fini(world);
}

void Terrain_origin_shift_keeps_tiles(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_singleton_add(world, WorldOrigin);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->enabled = false;
    ecs_singleton_modified(world, WorldOrigin);
    ecs_progress(world, 0);
    tt_ready(world, terrain);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    int32_t before = count;
    int32_t created = ecs_get(world, terrain, TerrainStatus)->created;

    box_world_origin_shift(world, 0, (double[3]){ 512, 0, -256 });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, before);
    test_int(ecs_get(world, terrain, TerrainStatus)->created, created);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        double size = TT_TILE * (double)(1 << t->level);
        const Position *p = ecs_get(world, t->entity, Position);
        double abs[3];
        box_world_to_absolute(world, (vec3){ p->x, p->y, p->z }, abs);
        test_flt(abs[0], (t->ix + 0.5) * size);
        test_flt(abs[2], (t->iz + 0.5) * size);
        test_bool(t->drawn, true);
    }

    ecs_fini(world);
}

void Terrain_materials_copied(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 4);
    ecs_set(world, terrain, Rgb, {10, 200, 30});
    ecs_set(world, terrain, TextureMapping, {.mode = MappingWorld, .size = 8, .blend = 0.5f});
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 4);
    for (i = 0; i < count; i ++) {
        const Rgb *c = ecs_get(world, tiles[i]->entity, Rgb);
        test_assert(c != NULL);
        test_int(c->g, 200);
        const TextureMapping *m = ecs_get(world, tiles[i]->entity, TextureMapping);
        test_assert(m != NULL);
        test_int(m->mode, MappingWorld);
    }

    ecs_set(world, terrain, Rgb, {50, 60, 70});
    ecs_remove(world, terrain, TextureMapping);
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        test_int(ecs_get(world, tiles[i]->entity, Rgb)->g, 60);
        test_assert(!ecs_has(world, tiles[i]->entity, TextureMapping));
    }

    ecs_fini(world);
}

void Terrain_bounded_extent(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    Terrain *t = ecs_get_mut(world, terrain, Terrain);
    t->extent = 40;
    ecs_modified(world, terrain, Terrain);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        double size = TT_TILE * (double)(1 << tiles[i]->level);
        test_assert(tiles[i]->ix * size < 40.0);
        test_assert((tiles[i]->ix + 1) * size > -40.0);
        test_assert(tiles[i]->iz * size < 40.0);
        test_assert((tiles[i]->iz + 1) * size > -40.0);
        const ShapeMesh *sm = ecs_get(world, tiles[i]->entity, ShapeMesh);
        test_flt(sm->surface.p[4][3], 1.0f);
    }

    ecs_fini(world);
}

void Terrain_remove_deletes_tiles(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 4);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 4);
    ecs_entity_t entities[4];
    for (i = 0; i < count; i ++) {
        entities[i] = tiles[i]->entity;
    }

    ecs_remove(world, terrain, Terrain);
    ecs_progress(world, 0);
    for (i = 0; i < 4; i ++) {
        test_bool(ecs_is_alive(world, entities[i]), false);
    }

    test_assert(box_terrain_tiles(world, terrain, &count) == NULL);
    test_int(count, 0);
    test_assert(!ecs_has(world, terrain, TerrainStatus));
    ecs_fini(world);
}

void Terrain_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "terrain_script",
        "using box.transform\n"
        "using box.texture\n"
        "using box.terrain\n"
        "using box.world\n"
        "focus { Position: {0, 5, 0}\n WorldFocus }\n"
        "ground {\n"
        "  flat { nodes.Constant: {value: {3, 3, 3, 3}} }\n"
        "  Terrain: {height: flat, tile_size: 8, levels: 2, budget: 3}\n"
        "}\n", NULL) == 0);
    ecs_entity_t ground = ecs_lookup(world, "ground");
    test_assert(ground != 0);
    const Terrain *t = ecs_get(world, ground, Terrain);
    test_assert(t != NULL);
    test_int(t->height, ecs_lookup(world, "ground.flat"));
    test_flt(t->tile_size, 8);
    ecs_progress(world, 0);
    test_int(ecs_get(world, ground, TerrainStatus)->count, 3);
    ecs_fini(world);
}

void Terrain_masks_graph_windows(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_entity_t masks = tt_masks(world, terrain);
    ecs_progress(world, 0);
    int32_t count, i, n = 17, texels = n + 2 * BOX_TERRAIN_BORDER;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    test_int(box_terrain_coarse_texels(n), (texels + 1) / 2);
    bool coarse_level = false;
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        const TextureGraph *heights = ecs_get(world, t->texture, TextureGraph);
        const TextureGraph *fine = ecs_get(world, t->masks, TextureGraph);
        const TextureGraph *coarse = ecs_get(world, t->coarse, TextureGraph);
        test_assert(fine != NULL);
        test_assert(coarse != NULL);
        test_assert(ecs_has_pair(world, t->masks, EcsChildOf, t->entity));
        test_assert(ecs_has_pair(world, t->coarse, EcsChildOf, t->entity));
        test_int(fine->format, GraphLinear);
        test_int(coarse->format, GraphLinear);
        test_int(fine->domain, DomainWorld);
        test_int(coarse->domain, DomainWorld);
        test_int(fine->width, texels);
        test_int(fine->height, texels);
        test_int(coarse->width, (texels + 1) / 2);
        test_int(coarse->height, (texels + 1) / 2);

        // The same window as the heights: the coarse texels are every second one
        test_assert(fine->window.x == heights->window.x);
        test_assert(fine->window.z == heights->window.z);
        test_flt(fine->window.width, heights->window.width);
        test_assert(coarse->window.x == heights->window.x);
        test_assert(coarse->window.z == heights->window.z);
        test_flt(coarse->window.width, heights->window.width);
        test_flt(coarse->window.depth, heights->window.depth);

        // Level 0 reads the masks node, the levels above through a TexBlur of a level
        test_int(fine->output, box_terrain_mask_node(world, terrain, t->level));
        test_int(coarse->output, box_terrain_mask_node(world, terrain, t->level + 1));
        if (t->level == 0) {
            test_int(fine->output, masks);
        } else {
            const TexBlur *blur = ecs_get(world, fine->output, TexBlur);
            double cell = TT_TILE * (double)(1 << t->level) / (double)(n - 1);
            test_assert(blur != NULL);
            test_int(blur->input, masks);
            test_flt(blur->radius, (float)(1.5 * cell));
            coarse_level = true;
        }

        const TexBlur *up = ecs_get(world, coarse->output, TexBlur);
        test_assert(up != NULL);
        test_int(up->input, masks);
    }

    test_bool(coarse_level, true);
    ecs_fini(world);
}

void Terrain_masks_wait_ready(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    tt_masks(world, terrain);
    ecs_progress(world, 0);
    tt_masks_ready(world, terrain, false);
    tt_ready(world, terrain);
    ecs_progress(world, 0);

    // The heights alone do not make a tile with masks ready
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_int(status->drawn, 0);
    test_assert(status->pending > 0);

    tt_masks_ready(world, terrain, true);
    ecs_progress(world, 0);
    status = ecs_get(world, terrain, TerrainStatus);
    int32_t count;
    box_terrain_tiles(world, terrain, &count);
    test_int(status->pending, 0);
    test_int(status->drawn, count);
    ecs_fini(world);
}

void Terrain_masks_blend_surface(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 4);
    tt_masks(world, terrain);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 4);

    // Masks without a TextureBlend keep the plain material of the tiles
    for (i = 0; i < count; i ++) {
        const ShapeMesh *sm = ecs_get(world, tiles[i]->entity, ShapeMesh);
        test_flt(sm->surface.p[3][0], 0.0f);
        test_bool(ecs_get(world, tiles[i]->entity, TerrainTile)->masked, false);
        test_assert(!ecs_has(world, tiles[i]->entity, TextureMapping));
    }

    // With a TextureBlend the tiles blend by their masks, mapped on the world by default
    ecs_entity_t rock = ecs_new(world);
    ecs_set(world, terrain, TextureBlend, {.layers = {rock}});
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        const ShapeMesh *sm = ecs_get(world, tiles[i]->entity, ShapeMesh);
        test_flt(sm->surface.p[3][0], 1.0f);
        test_bool(ecs_get(world, tiles[i]->entity, TerrainTile)->masked, true);
        const TextureBlend *b = ecs_get(world, tiles[i]->entity, TextureBlend);
        test_assert(b != NULL);
        test_int(b->layers[0], rock);
        const TextureMapping *m = ecs_get(world, tiles[i]->entity, TextureMapping);
        test_assert(m != NULL);
        test_int(m->mode, MappingWorld);
        test_flt(m->size, 1.0f);
    }

    // The TextureMapping of the terrain wins
    ecs_set(world, terrain, TextureMapping, {.mode = MappingWorld, .size = 4, .blend = 0.5f});
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        const TextureMapping *m = ecs_get(world, tiles[i]->entity, TextureMapping);
        test_flt(m->size, 4.0f);
        test_flt(m->blend, 0.5f);
    }

    ecs_remove(world, terrain, TextureBlend);
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        const ShapeMesh *sm = ecs_get(world, tiles[i]->entity, ShapeMesh);
        test_flt(sm->surface.p[3][0], 0.0f);
        test_assert(!ecs_has(world, tiles[i]->entity, TextureBlend));
    }

    ecs_fini(world);
}

void Terrain_masks_change_remakes_tiles(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 4);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 4);
    for (i = 0; i < count; i ++) {
        test_int(tiles[i]->masks, 0);
        test_int(tiles[i]->coarse, 0);
    }

    test_int(box_terrain_mask_node(world, terrain, 1), 0);
    tt_masks(world, terrain);
    ecs_progress(world, 0);
    tiles = box_terrain_tiles(world, terrain, &count);
    test_int(count, 4);
    test_int(ecs_get(world, terrain, TerrainStatus)->created, 8);
    for (i = 0; i < count; i ++) {
        test_assert(tiles[i]->masks != 0);
        test_assert(tiles[i]->coarse != 0);
    }

    ecs_fini(world);
}

void Terrain_sample_window_covers_drawn(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    tt_ready(world, terrain);
    ecs_progress(world, 0);
    box_terrain_window_t w;
    test_bool(box_terrain_window(world, terrain, &w), true);
    test_int(w.levels, 4);
    test_int(w.resolution, 17);
    test_int(w.focus_count, 1);
    test_flt(w.tile_size, TT_TILE);
    test_flt(w.extent, 0);
    test_assert(w.cells > 0 && w.cells <= BOX_TERRAIN_WINDOW_CELLS);
    test_int(w.cells % 2, 0);

    // The windows center on the focus at the origin
    int32_t count, i, level;
    for (level = 0; level < w.levels; level ++) {
        test_int(w.first[level][0], -w.cells / 2);
        test_int(w.first[level][1], -w.cells / 2);
        test_assert(w.morph[level][0] < w.morph[level][1]);
        test_flt(w.morph[level][1], TT_RANGE * (double)(1 << level));
    }

    // Every tile that draws has a cell in the window of its level, with the morph of its tile
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        if (!t->drawn) {
            continue;
        }

        int32_t x = t->ix - w.first[t->level][0], z = t->iz - w.first[t->level][1];
        test_assert(x >= 0 && x < w.cells);
        test_assert(z >= 0 && z < w.cells);
        const TerrainTile *tile = ecs_get(world, t->entity, TerrainTile);
        test_assert(tile != NULL);
        test_flt(tile->morph[0], w.morph[t->level][0]);
        test_flt(tile->morph[1], w.morph[t->level][1]);
    }

    ecs_fini(world);
}

void Terrain_sample_window_follows_focus(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    box_terrain_window_t before, after;
    test_bool(box_terrain_window(world, terrain, &before), true);
    ecs_set(world, focus, Position, {200, 10, -100});
    ecs_progress(world, 0);
    test_bool(box_terrain_window(world, terrain, &after), true);
    test_int(after.cells, before.cells);
    test_flt(after.foci[0][0], 200);
    test_flt(after.foci[0][1], -100);
    int32_t level;
    for (level = 0; level < after.levels; level ++) {
        double size = TT_TILE * (double)(1 << level);
        test_int(after.first[level][0], (int32_t)floor(200 / size) - after.cells / 2);
        test_int(after.first[level][1], (int32_t)floor(-100 / size) - after.cells / 2);
    }

    ecs_fini(world);
}

void Terrain_sample_window_without_terrain(void) {
    ecs_entity_t terrain, focus;
    ecs_world_t *world = tt_world(&terrain, &focus, 1000);
    ecs_progress(world, 0);
    box_terrain_window_t w;
    test_bool(box_terrain_window(world, focus, &w), false);
    test_int(w.levels, 0);
    ecs_remove(world, terrain, Terrain);
    ecs_progress(world, 0);
    test_bool(box_terrain_window(world, terrain, &w), false);
    ecs_fini(world);
}

void Terrain_sampled_tag(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(TerrainSampled != 0);
    test_assert(ecs_lookup(world, "box.terrain.TerrainSampled") == TerrainSampled);
    test_int(ecs_script_run(world, "sampled",
        "using box.terrain\n"
        "hills { TerrainSampled }\n", NULL), 0);
    ecs_entity_t hills = ecs_lookup(world, "hills");
    test_assert(hills != 0);
    test_bool(ecs_has_id(world, hills, TerrainSampled), true);
    ecs_fini(world);
}
