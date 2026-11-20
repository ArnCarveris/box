/**
 * @file terrain/tiles.c
 * @brief The Terrain height field: a quadtree of tiles around the focus of the world, each a
 * TextureGraph of the heights over its rectangle and a TerrainTile shape that the vertex stage
 * displaces by them, made and dropped within a budget per frame.
 *
 * The tiles are children of a container without a transform in the scene of the terrain, roots
 * that a shift of the WorldOrigin moves, placed in double precision from the terrain position.
 * A tile is disabled while it does not draw: its heights are pending, or another tile covers it.
 *
 * With Terrain.masks a tile also has the TextureGraph of the masks over the window of its heights
 * and of the masks of the next coarser level at every second texel, which the fragment stage
 * fades to as the grid morphs. The levels above 0 read the masks through a TexBlur per level.
 */

#include "terrain.h"
#include "../texture/texture.h"
#include "../world/world.h"
#include <math.h>
#include <float.h>

ECS_COMPONENT_DECLARE(TerrainTile);

/** Tiles the focus of a terrain wants at most */
#define BOX_TERRAIN_MAX_TILES (4096)

/** Latest share of the range of a level where its morph into the grid of the coarser level
 * starts */
#define BOX_TERRAIN_MORPH (0.95)

/** Share of the finest tile size a focus moves before the tiles morph around its new position */
#define BOX_TERRAIN_FOCUS_STEP (1.0 / 16.0)

/** Tiles dropped per frame, per tile of the budget */
#define BOX_TERRAIN_DROPS (4)

/** Material components of a terrain that its tiles get */
#define BOX_TERRAIN_MATERIALS (13)

/** Reach of the TexBlur of the masks of a level above 0 in cells of the level: three standard
 * deviations, so a standard deviation of half a cell */
#define BOX_TERRAIN_MASK_BLUR (1.5)

/** The values of a Terrain with its defaults and limits applied */
typedef struct box_terrain_settings_t {
    ecs_entity_t height;    /**< Height node */
    ecs_entity_t masks;     /**< Masks node, 0 for none */
    double tile_size;       /**< Edge of a tile of level 0 */
    double lod_distance;    /**< Range of level 0 */
    double extent;          /**< Half size, 0 unbounded */
    float min_height;       /**< Bottom of the bounds */
    float max_height;       /**< Top of the bounds */
    int32_t resolution;     /**< Vertices per edge, odd */
    int32_t levels;         /**< Levels of detail */
    int32_t budget;         /**< Tiles made per frame */
    int32_t pad;            /**< Zero */
} box_terrain_settings_t;

/** A node of the quadtree a focus wants as a tile */
typedef struct box_terrain_node_t {
    int32_t level;          /**< Level of detail */
    int32_t ix;             /**< Column */
    int32_t iz;             /**< Row */
    double distance;        /**< Distance to the nearest focus */
} box_terrain_node_t;

/** The tiles of a terrain */
typedef struct box_terrain_tiles_t {
    ecs_map_t map;          /**< Key of a node to its box_terrain_tile_t */
    ecs_vec_t list;         /**< box_terrain_tile_t*, every tile */
    ecs_entity_t container; /**< Parent of the tile entities */
    ecs_entity_t filters[BOX_TERRAIN_LEVELS + 1]; /**< TexBlur node of the masks of each level
                             *   above 0, children of the container, 0 until made */
    box_terrain_settings_t settings; /**< Settings the tiles were made with */
    bool made;              /**< settings holds the settings of the tiles */
    double foci[BOX_TERRAIN_FOCI][2]; /**< Foci the tiles morph around, x and z in the frame
                             *   of the terrain */
    int32_t focus_count;    /**< Foci in foci */
    double placed[3];       /**< Position of the terrain the tiles were placed for */
    bool positioned;        /**< placed holds a position */
    uint64_t materials;     /**< Hash of the material components the tiles have */
    int32_t created;        /**< Tiles made */
    int32_t dropped;        /**< Tiles deleted */
} box_terrain_tiles_t;

/** State of box.terrain in a world */
typedef struct box_terrain_ctx_t {
    ecs_map_t terrains;     /**< Terrain entity to its box_terrain_tiles_t */
    ecs_vec_t free_slots;   /**< int32_t, slots of the heights given back */
    int32_t next_slot;      /**< Slot after the highest one taken */
    ecs_vec_t nodes;        /**< box_terrain_node_t, the nodes a terrain wants this frame */
    ecs_map_t roots;        /**< Keys of the root nodes already selected this frame */
    ecs_vec_t terrain_list; /**< ecs_entity_t, the terrains to update this frame */
    ecs_id_t materials[BOX_TERRAIN_MATERIALS]; /**< Material components the tiles get */
} box_terrain_ctx_t;

/** Singleton that holds the state of box.terrain */
typedef struct TerrainModule {
    box_terrain_ctx_t *ctx; /**< The state */
} TerrainModule;

static ECS_COMPONENT_DECLARE(TerrainModule);

/** Get the vertices per edge of a tile from a resolution: odd, 5 to 257, 65 for 0 or less.
 *
 * @param resolution The resolution
 * @return Vertices per edge
 */
static int32_t box_terrain_vertices(
    int32_t resolution)
{
    int32_t result = resolution > 0 ? resolution : 65;
    result = result < 5 ? 5 : result > 257 ? 257 : result;

    return result + ((result & 1) ? 0 : 1);
}

int32_t box_terrain_coarse_texels(
    int32_t resolution)
{
    return (box_terrain_vertices(resolution) + 2 * BOX_TERRAIN_BORDER + 1) / 2;
}

uint32_t box_terrain_row_bytes(
    int32_t resolution)
{
    uint32_t texels = (uint32_t)box_terrain_vertices(resolution) + 2u * BOX_TERRAIN_BORDER;

    return (texels * 4u + 255u) & ~255u;
}

/** Apply the defaults and limits of a Terrain.
 *
 * @param t The terrain
 * @param out Receives the settings
 */
static void box_terrain_settings(
    const Terrain *t,
    box_terrain_settings_t *out)
{
    ecs_os_memset_t(out, 0, box_terrain_settings_t);
    out->height = t->height;
    out->masks = t->masks;
    out->tile_size = isfinite(t->tile_size) && t->tile_size > 0.0f ? t->tile_size : 32.0;
    out->resolution = box_terrain_vertices(t->resolution);
    out->levels = t->levels > 0 ? t->levels : 6;
    out->levels = out->levels > BOX_TERRAIN_LEVELS ? BOX_TERRAIN_LEVELS : out->levels;
    out->lod_distance = isfinite(t->lod_distance) && t->lod_distance > 0.0f
        ? t->lod_distance : 4.0 * out->tile_size;
    out->lod_distance = fmin(fmax(out->lod_distance, 3.0 * out->tile_size),
        16.0 * out->tile_size);
    out->extent = isfinite(t->extent) && t->extent > 0.0f ? t->extent : 0.0;
    out->min_height = t->min_height;
    out->max_height = t->max_height;
    if (!(isfinite(t->min_height) && isfinite(t->max_height) && t->max_height > t->min_height)) {
        out->min_height = -256.0f;
        out->max_height = 1024.0f;
    }

    out->budget = t->budget > 0 ? t->budget : 8;
}

/** Get the edge of a tile of a level.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_size(
    const box_terrain_settings_t *s,
    int32_t level)
{
    return ldexp(s->tile_size, level);
}

/** Get the range of a level: a tile of the level splits into finer tiles while a focus is closer
 * than the range of the finer level.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_range(
    const box_terrain_settings_t *s,
    int32_t level)
{
    return ldexp(s->lod_distance, level);
}

/** Get the distance of a focus from the center of a tile of a level where its morph into the
 * grid of the next coarser level starts; it ends at the range of the level.
 *
 * A tile of level L meets finer tiles no farther from a focus than the range of level L - 1 plus
 * the diagonal of a tile of level L, so its morph starts beyond that, and ends at its range,
 * where it meets coarser tiles. Ranges of at least 3 tile sizes leave room for the morph.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_morph(
    const box_terrain_settings_t *s,
    int32_t level)
{
    double size = box_terrain_size(s, level), range = box_terrain_range(s, level);

    return fmin(0.5 * range + 1.42 * size, BOX_TERRAIN_MORPH * range);
}

/** Get the key of a node of the quadtree.
 *
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return The key
 */
static uint64_t box_terrain_key(
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    return ((uint64_t)(level + 1) << 58) | ((uint64_t)((uint32_t)ix & 0x1FFFFFFFu) << 29) |
        (uint64_t)((uint32_t)iz & 0x1FFFFFFFu);
}

/** Get the index of the parent of a node along an axis: the index halved, rounded down.
 *
 * @param v The index
 * @return The index of the parent
 */
static int32_t box_terrain_half(
    int32_t v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

/** Get the distance from a focus to the rectangle of a node, 0 inside it.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @param focus The focus, x and z in the frame of the terrain
 * @return Meters
 */
static double box_terrain_node_distance(
    const box_terrain_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz,
    const double focus[2])
{
    double size = box_terrain_size(s, level);
    double x0 = (double)ix * size, z0 = (double)iz * size;
    double dx = fmax(fmax(x0 - focus[0], focus[0] - (x0 + size)), 0.0);
    double dz = fmax(fmax(z0 - focus[1], focus[1] - (z0 + size)), 0.0);

    return sqrt(dx * dx + dz * dz);
}

/** Get the distance from the nearest focus to the rectangle of a node.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @param foci The foci
 * @param count Number of foci
 * @return Meters, DBL_MAX without foci
 */
static double box_terrain_nearest(
    const box_terrain_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz,
    const double foci[][2],
    int32_t count)
{
    double result = DBL_MAX;
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = fmin(result, box_terrain_node_distance(s, level, ix, iz, foci[i]));
    }

    return result;
}

/** Check whether a node overlaps the extent of a bounded terrain, always for an unbounded one.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return Whether the node has a part of the terrain
 */
static bool box_terrain_inside(
    const box_terrain_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    double size = box_terrain_size(s, level), e = s->extent;
    double x0 = (double)ix * size, z0 = (double)iz * size;

    return e <= 0.0 || (x0 < e && x0 + size > -e && z0 < e && z0 + size > -e);
}

/** Select the tiles of a node: the node itself, or the tiles of its four children while a focus
 * is within the range of the finer level.
 *
 * @param ctx The state
 * @param s The settings
 * @param foci The foci
 * @param count Number of foci
 * @param level The level of the node
 * @param ix The column
 * @param iz The row
 */
static void box_terrain_select_node(
    box_terrain_ctx_t *ctx,
    const box_terrain_settings_t *s,
    const double foci[][2],
    int32_t count,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    double distance;
    int32_t a, b;
    if (!box_terrain_inside(s, level, ix, iz)) {
        goto done;
    }

    distance = box_terrain_nearest(s, level, ix, iz, foci, count);
    if (level > 0 && distance < box_terrain_range(s, level - 1) &&
        ecs_vec_count(&ctx->nodes) + 4 <= BOX_TERRAIN_MAX_TILES)
    {
        for (b = 0; b < 2; b ++) {
            for (a = 0; a < 2; a ++) {
                box_terrain_select_node(ctx, s, foci, count, level - 1, ix * 2 + a, iz * 2 + b);
            }
        }

        goto done;
    }

    *ecs_vec_append_t(NULL, &ctx->nodes, box_terrain_node_t) = (box_terrain_node_t){
        .level = level, .ix = ix, .iz = iz, .distance = distance };
done:
    return;
}

/** Order nodes by their distance to the nearest focus, nearest first.
 *
 * @param a The first node
 * @param b The second node
 * @return Negative, zero or positive
 */
static int box_terrain_node_compare(
    const void *a,
    const void *b)
{
    const box_terrain_node_t *na = a, *nb = b;

    return (na->distance > nb->distance) - (na->distance < nb->distance);
}

/** Select the tiles the foci want: the root nodes of the coarsest level within its range of a
 * focus (and inside the extent of a bounded terrain), refined around the foci, nearest first.
 *
 * @param ctx The state, receives the nodes
 * @param s The settings
 * @param foci The foci
 * @param count Number of foci
 */
static void box_terrain_select(
    box_terrain_ctx_t *ctx,
    const box_terrain_settings_t *s,
    const double foci[][2],
    int32_t count)
{
    int32_t top = s->levels - 1, i, ix, iz;
    double size = box_terrain_size(s, top), range = box_terrain_range(s, top);
    ecs_vec_clear(&ctx->nodes);
    ecs_map_clear(&ctx->roots);
    for (i = 0; i < count; i ++) {
        double lo_x = foci[i][0] - range, hi_x = foci[i][0] + range;
        double lo_z = foci[i][1] - range, hi_z = foci[i][1] + range;
        if (s->extent > 0.0) {
            lo_x = fmax(lo_x, -s->extent);
            lo_z = fmax(lo_z, -s->extent);
            hi_x = fmin(hi_x, s->extent);
            hi_z = fmin(hi_z, s->extent);
        }

        if (lo_x > hi_x || lo_z > hi_z) {
            continue;
        }

        int32_t x0 = (int32_t)floor(lo_x / size), x1 = (int32_t)floor(hi_x / size);
        int32_t z0 = (int32_t)floor(lo_z / size), z1 = (int32_t)floor(hi_z / size);
        for (iz = z0; iz <= z1; iz ++) {
            for (ix = x0; ix <= x1; ix ++) {
                uint64_t key = box_terrain_key(top, ix, iz);
                if (ecs_map_get(&ctx->roots, key) ||
                    box_terrain_node_distance(s, top, ix, iz, foci[i]) >= range)
                {
                    continue;
                }

                ecs_map_insert(&ctx->roots, key, 1);
                box_terrain_select_node(ctx, s, foci, count, top, ix, iz);
            }
        }
    }

    if (ecs_vec_count(&ctx->nodes) > 1) {
        qsort(ecs_vec_first(&ctx->nodes), (size_t)ecs_vec_count(&ctx->nodes),
            sizeof(box_terrain_node_t), box_terrain_node_compare);
    }
}

/** Take a slot for the heights of a tile.
 *
 * @param ctx The state
 * @return The slot
 */
static int32_t box_terrain_slot_take(
    box_terrain_ctx_t *ctx)
{
    int32_t result;
    if (ecs_vec_count(&ctx->free_slots)) {
        result = *ecs_vec_last_t(&ctx->free_slots, int32_t);
        ecs_vec_remove_last(&ctx->free_slots);
    } else {
        result = ctx->next_slot ++;
    }

    return result;
}

/** Give back the slot of a tile.
 *
 * @param ctx The state
 * @param slot The slot
 */
static void box_terrain_slot_give(
    box_terrain_ctx_t *ctx,
    int32_t slot)
{
    *ecs_vec_append_t(NULL, &ctx->free_slots, int32_t) = slot;
}

/** Find the tile of a node.
 *
 * @param tiles The tiles
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return The tile, NULL when the node has none
 */
static box_terrain_tile_t* box_terrain_find(
    const box_terrain_tiles_t *tiles,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    return ecs_map_get_deref(&tiles->map, box_terrain_tile_t, box_terrain_key(level, ix, iz));
}

/** Check whether a tile lies inside the rectangle of a coarser tile.
 *
 * @param outer The coarser tile
 * @param inner The finer tile
 * @return Whether inner is a descendant of outer
 */
static bool box_terrain_contains(
    const box_terrain_tile_t *outer,
    const box_terrain_tile_t *inner)
{
    int32_t level = inner->level, ix = inner->ix, iz = inner->iz;
    while (level < outer->level) {
        ix = box_terrain_half(ix);
        iz = box_terrain_half(iz);
        level ++;
    }

    return level == outer->level && ix == outer->ix && iz == outer->iz;
}

/** Hash bytes (FNV-1a) into a hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash
 */
static uint64_t box_terrain_hash(
    uint64_t hash,
    const void *data,
    ecs_size_t size)
{
    const uint8_t *bytes = data;
    ecs_size_t i;
    for (i = 0; i < size; i ++) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }

    return hash;
}

/** Hash the material components of a terrain, so its tiles get them again when one changes.
 *
 * @param world The world
 * @param ctx The state
 * @param e The terrain
 * @return The hash
 */
static uint64_t box_terrain_materials_hash(
    const ecs_world_t *world,
    const box_terrain_ctx_t *ctx,
    ecs_entity_t e)
{
    uint64_t result = 0xCBF29CE484222325ull;
    int32_t i;
    for (i = 0; i < BOX_TERRAIN_MATERIALS; i ++) {
        ecs_id_t id = ctx->materials[i];
        const ecs_type_info_t *ti = id ? ecs_get_type_info(world, id) : NULL;
        const void *ptr = ti && ti->size ? ecs_get_id(world, e, id) : NULL;
        uint8_t has = id && ecs_has_id(world, e, id) ? 1 : 0;
        result = box_terrain_hash(result, &has, 1);
        if (ptr) {
            result = box_terrain_hash(result, ptr, ti->size);
        }
    }

    return result;
}

/** Give a tile the material components of its terrain, removing those the terrain lost.
 *
 * @param world The world
 * @param ctx The state
 * @param e The terrain
 * @param tile The tile entity
 */
static void box_terrain_materials_copy(
    ecs_world_t *world,
    const box_terrain_ctx_t *ctx,
    ecs_entity_t e,
    ecs_entity_t tile)
{
    int32_t i;
    for (i = 0; i < BOX_TERRAIN_MATERIALS; i ++) {
        ecs_id_t id = ctx->materials[i];
        const ecs_type_info_t *ti = id ? ecs_get_type_info(world, id) : NULL;
        if (!id) {
            continue;
        }

        if (ti && ti->size) {
            const void *ptr = ecs_get_id(world, e, id);
            if (ptr) {
                ecs_set_id(world, tile, id, (size_t)ti->size, ptr);
            } else if (ecs_has_id(world, tile, id)) {
                ecs_remove_id(world, tile, id);
            }
        } else if (ecs_has_id(world, e, id)) {
            ecs_add_id(world, tile, id);
        } else if (ecs_has_id(world, tile, id)) {
            ecs_remove_id(world, tile, id);
        }
    }

    const Terrain *terrain = ecs_get(world, e, Terrain);
    if (terrain && terrain->masks && ecs_has(world, e, TextureBlend) &&
        !ecs_has(world, e, TextureMapping))
    {
        ecs_set(world, tile, TextureMapping, { .mode = MappingWorld, .size = 1.0f });
    }
}

/** Get the node the tiles of a level read their masks from: the masks node at level 0, else
 * the TexBlur of the level, made under the container on first use and kept on the masks node
 * and the reach of the level.
 *
 * @param world The world
 * @param tiles The tiles of the terrain, with their settings
 * @param level The level
 * @return The node, 0 without masks
 */
static ecs_entity_t box_terrain_filter(
    ecs_world_t *world,
    box_terrain_tiles_t *tiles,
    int32_t level)
{
    const box_terrain_settings_t *s = &tiles->settings;
    ecs_entity_t result = s->masks;
    if (!s->masks || level <= 0 || level > BOX_TERRAIN_LEVELS) {
        goto done;
    }

    double cell = box_terrain_size(s, level) / (double)(s->resolution - 1);
    float radius = (float)(BOX_TERRAIN_MASK_BLUR * cell);
    result = tiles->filters[level];
    if (!result || !ecs_is_alive(world, result)) {
        result = tiles->container
            ? ecs_new_w_pair(world, EcsChildOf, tiles->container) : ecs_new(world);
        tiles->filters[level] = result;
    }

    const TexBlur *blur = ecs_get(world, result, TexBlur);
    if (!blur || blur->input != s->masks || blur->radius != radius) {
        ecs_set(world, result, TexBlur, { .input = s->masks, .radius = radius });
    }

done:
    return result;
}

/** Check whether the image of a texture of a tile is computed.
 *
 * @param world The world
 * @param texture The texture entity, 0 for none
 * @return Whether it has an image, true for none
 */
static bool box_terrain_texture_ready(
    const ecs_world_t *world,
    ecs_entity_t texture)
{
    const TextureStatus *status = texture ? ecs_get(world, texture, TextureStatus) : NULL;

    return !texture || (status && status->state == TextureReady && status->revision > 0);
}

/** Write the TerrainTile of a tile: its node, slot, focus and morph (box_terrain_morph).
 *
 * @param world The world
 * @param tiles The tiles of the terrain
 * @param s The settings
 * @param e The terrain
 * @param t The tile
 */
static void box_terrain_tile_write(
    ecs_world_t *world,
    const box_terrain_tiles_t *tiles,
    const box_terrain_settings_t *s,
    ecs_entity_t e,
    const box_terrain_tile_t *t)
{
    double size = box_terrain_size(s, t->level), range = box_terrain_range(s, t->level);
    double cx = ((double)t->ix + 0.5) * size, cz = ((double)t->iz + 0.5) * size;
    double morph = box_terrain_morph(s, t->level);
    double fx = cx, fz = cz, best = DBL_MAX;
    int32_t i;
    for (i = 0; i < tiles->focus_count; i ++) {
        double dx = tiles->foci[i][0] - cx, dz = tiles->foci[i][1] - cz;
        if (dx * dx + dz * dz < best) {
            best = dx * dx + dz * dz;
            fx = tiles->foci[i][0];
            fz = tiles->foci[i][1];
        }
    }

    float cell = (float)(size / (double)(s->resolution - 1));
    TerrainTile v = {
        .terrain = e,
        .texture = t->texture,
        .masks = t->masks,
        .coarse = t->coarse,
        .level = t->level,
        .ix = t->ix,
        .iz = t->iz,
        .resolution = s->resolution,
        .slot = t->slot,
        .size = (float)size,
        .focus = { (float)(fx - cx), (float)(fz - cz) },
        .morph = { (float)morph, (float)range },
        .skirt = 2.0f * cell + 1.0f,
        .min_height = s->min_height,
        .max_height = s->max_height,
        .bounded = s->extent > 0.0,
        .masked = t->masks && t->coarse && ecs_has(world, e, TextureBlend),
        .clip = { (float)(-s->extent - cx), (float)(-s->extent - cz),
            (float)(s->extent - cx), (float)(s->extent - cz) }
    };
    ecs_set_ptr(world, t->entity, TerrainTile, &v);
}

/** Place a tile at the center of its rectangle in the frame of its terrain.
 *
 * @param world The world
 * @param s The settings
 * @param t The tile
 * @param local The local position of the terrain
 */
static void box_terrain_tile_place(
    ecs_world_t *world,
    const box_terrain_settings_t *s,
    const box_terrain_tile_t *t,
    const double local[3])
{
    double size = box_terrain_size(s, t->level);
    ecs_set(world, t->entity, Position, {
        (float)(local[0] + ((double)t->ix + 0.5) * size),
        (float)local[1],
        (float)(local[2] + ((double)t->iz + 0.5) * size)
    });
}

/** Make the tile of a node: a disabled tile entity under the container with the TextureGraph of
 * its heights as a child, the window grown by BOX_TERRAIN_BORDER texels on each side.
 *
 * @param world The world
 * @param ctx The state
 * @param tiles The tiles of the terrain
 * @param node The node
 * @return The tile
 */
static box_terrain_tile_t* box_terrain_tile_new(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    box_terrain_tiles_t *tiles,
    const box_terrain_node_t *node)
{
    const box_terrain_settings_t *s = &tiles->settings;
    box_terrain_tile_t *t = ecs_os_calloc_t(box_terrain_tile_t);
    double size = box_terrain_size(s, node->level);
    double cell = size / (double)(s->resolution - 1);
    int32_t texels = s->resolution + 2 * BOX_TERRAIN_BORDER;
    t->level = node->level;
    t->ix = node->ix;
    t->iz = node->iz;
    t->distance = node->distance;
    t->slot = box_terrain_slot_take(ctx);
    t->entity = ecs_new_w_pair(world, EcsChildOf, tiles->container);
    ecs_enable(world, t->entity, false);
    t->texture = ecs_new_w_pair(world, EcsChildOf, t->entity);
    ecs_set(world, t->texture, TextureGraph, {
        .output = s->height,
        .width = texels,
        .height = texels,
        .domain = DomainWorld,
        .window = {
            .x = (double)node->ix * size - BOX_TERRAIN_BORDER * cell,
            .z = (double)node->iz * size - BOX_TERRAIN_BORDER * cell,
            .width = (float)(cell * (double)(texels - 1)),
            .depth = (float)(cell * (double)(texels - 1))
        },
        .format = GraphFloat
    });
    if (s->masks) {
        const TextureGraph *heights = ecs_get(world, t->texture, TextureGraph);
        TextureGraphWindow window = heights->window;
        int32_t coarse = box_terrain_coarse_texels(s->resolution);
        t->masks = ecs_new_w_pair(world, EcsChildOf, t->entity);
        ecs_set(world, t->masks, TextureGraph, {
            .output = box_terrain_filter(world, tiles, node->level),
            .width = texels,
            .height = texels,
            .domain = DomainWorld,
            .window = window,
            .format = GraphLinear
        });
        t->coarse = ecs_new_w_pair(world, EcsChildOf, t->entity);
        ecs_set(world, t->coarse, TextureGraph, {
            .output = box_terrain_filter(world, tiles, node->level + 1),
            .width = coarse,
            .height = coarse,
            .domain = DomainWorld,
            .window = window,
            .format = GraphLinear
        });
    }

    ecs_map_insert_ptr(&tiles->map, box_terrain_key(t->level, t->ix, t->iz), t);
    *ecs_vec_append_t(NULL, &tiles->list, box_terrain_tile_t*) = t;
    tiles->created ++;

    return t;
}

/** Delete a tile and give back its slot.
 *
 * @param world The world
 * @param ctx The state
 * @param tiles The tiles of the terrain
 * @param index Index of the tile in the list
 * @param entities Whether to delete the tile entity, false when the container goes with it
 */
static void box_terrain_tile_drop(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    box_terrain_tiles_t *tiles,
    int32_t index,
    bool entities)
{
    box_terrain_tile_t *t = *ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, index);
    if (entities && ecs_is_alive(world, t->entity)) {
        ecs_delete(world, t->entity);
    }

    box_terrain_slot_give(ctx, t->slot);
    ecs_map_remove(&tiles->map, box_terrain_key(t->level, t->ix, t->iz));
    ecs_vec_remove_t(&tiles->list, box_terrain_tile_t*, index);
    ecs_os_free(t);
    tiles->dropped ++;
}

/** Delete every tile of a terrain.
 *
 * @param world The world
 * @param ctx The state
 * @param tiles The tiles
 */
static void box_terrain_drop_all(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    box_terrain_tiles_t *tiles)
{
    while (ecs_vec_count(&tiles->list)) {
        box_terrain_tile_drop(world, ctx, tiles, ecs_vec_count(&tiles->list) - 1, true);
    }

    tiles->positioned = false;
    tiles->materials = 0;
}

/** Decide which tiles draw: a wanted tile that is ready; for one that is not, its nearest ready
 * ancestor, else its ready descendants; and no tile under a coarser tile that draws.
 *
 * @param tiles The tiles
 * @param levels Levels of the terrain
 */
static void box_terrain_resolve(
    box_terrain_tiles_t *tiles,
    int32_t levels)
{
    box_terrain_tile_t **list = ecs_vec_first(&tiles->list);
    int32_t count = ecs_vec_count(&tiles->list), i, j, ready = 0;
    for (i = 0; i < count; i ++) {
        list[i]->drawn = false;
        list[i]->keep = false;
        ready += list[i]->ready;
    }

    for (i = 0; i < count; i ++) {
        box_terrain_tile_t *t = list[i], *found = NULL;
        int32_t level = t->level, ix = t->ix, iz = t->iz;
        if (!t->wanted) {
            continue;
        }

        if (t->ready) {
            t->drawn = true;
            continue;
        }

        while (!found && level + 1 < levels) {
            level ++;
            ix = box_terrain_half(ix);
            iz = box_terrain_half(iz);
            found = box_terrain_find(tiles, level, ix, iz);
            found = found && found->ready ? found : NULL;
        }

        if (found) {
            found->drawn = true;
            found->keep = true;
            continue;
        }

        for (j = 0; j < count && ready; j ++) {
            box_terrain_tile_t *d = list[j];
            if (d->ready && d->level < t->level && box_terrain_contains(t, d)) {
                d->drawn = true;
                d->keep = true;
            }
        }
    }

    for (i = 0; i < count; i ++) {
        box_terrain_tile_t *t = list[i];
        int32_t level = t->level, ix = t->ix, iz = t->iz;
        while (t->drawn && level + 1 < levels) {
            level ++;
            ix = box_terrain_half(ix);
            iz = box_terrain_half(iz);
            const box_terrain_tile_t *a = box_terrain_find(tiles, level, ix, iz);
            if (a && a->drawn) {
                t->drawn = false;
            }
        }
    }
}

void box_terrain_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    double out[3])
{
    const Position *p = ecs_get(world, e, Position);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    out[0] = out[1] = out[2] = 0.0;
    if (p && box_world_is_root(world, e)) {
        out[0] = p->x;
        out[1] = p->y;
        out[2] = p->z;
    } else if (t) {
        out[0] = t->m[3][0];
        out[1] = t->m[3][1];
        out[2] = t->m[3][2];
    } else if (ecs_id(WorldOrigin)) {
        const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
        out[0] = o ? -o->x : 0.0;
        out[1] = o ? -o->y : 0.0;
        out[2] = o ? -o->z : 0.0;
    }
}

int32_t box_terrain_foci(
    const ecs_world_t *world,
    ecs_entity_t e,
    const double local[3],
    double out[BOX_TERRAIN_FOCI][2])
{
    box_world_focus_t foci[BOX_TERRAIN_FOCI];
    ecs_entity_t scene = box_world_scene_of(world, e);
    int32_t count = box_world_focus(world, foci, BOX_TERRAIN_FOCI), i, result = 0;
    count = count < BOX_TERRAIN_FOCI ? count : BOX_TERRAIN_FOCI;
    for (i = 0; i < count; i ++) {
        if (foci[i].scene == scene) {
            out[result][0] = (double)foci[i].position[0] - local[0];
            out[result][1] = (double)foci[i].position[2] - local[2];
            result ++;
        }
    }

    for (i = 0; i < count && !result; i ++) {
        out[i][0] = (double)foci[i].position[0] - local[0];
        out[i][1] = (double)foci[i].position[2] - local[2];
    }

    return result ? result : count;
}

/** Check whether the foci moved far enough from those the tiles morph around that the tiles
 * take the new ones, and take them.
 *
 * @param tiles The tiles
 * @param foci The foci
 * @param count Number of foci
 * @return Whether the tiles morph around new foci
 */
static bool box_terrain_refocus(
    box_terrain_tiles_t *tiles,
    const double foci[][2],
    int32_t count)
{
    double step = tiles->settings.tile_size * BOX_TERRAIN_FOCUS_STEP;
    bool result = count != tiles->focus_count;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        result = fabs(foci[i][0] - tiles->foci[i][0]) > step ||
            fabs(foci[i][1] - tiles->foci[i][1]) > step;
    }

    if (result) {
        for (i = 0; i < count; i ++) {
            tiles->foci[i][0] = foci[i][0];
            tiles->foci[i][1] = foci[i][1];
        }

        tiles->focus_count = count;
    }

    return result;
}

/** Write the TerrainStatus of a terrain when it changed.
 *
 * @param world The world
 * @param e The terrain
 * @param tiles The tiles
 * @param missing Tiles the foci want that the budget did not make yet
 */
static void box_terrain_status(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_terrain_tiles_t *tiles,
    int32_t missing)
{
    TerrainStatus status = {0};
    int32_t i, texels = tiles->settings.resolution + 2 * BOX_TERRAIN_BORDER;
    int32_t coarse = box_terrain_coarse_texels(tiles->settings.resolution);
    int64_t bytes = (int64_t)texels * texels * 4 +
        (int64_t)box_terrain_row_bytes(tiles->settings.resolution) * texels;
    if (tiles->settings.masks) {
        bytes += 2 * ((int64_t)texels * texels * 4 + (int64_t)coarse * coarse * 4);
    }

    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        const box_terrain_tile_t *t = *ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, i);
        status.tiles[t->level] += t->drawn;
        status.drawn += t->drawn;
        status.pending += t->wanted && !t->ready;
        status.gpu_bytes += bytes;
    }

    status.pending += missing;
    status.count = ecs_vec_count(&tiles->list);
    status.created = tiles->created;
    status.dropped = tiles->dropped;
    const TerrainStatus *old = ecs_get(world, e, TerrainStatus);
    if (!old || ecs_os_memcmp(old, &status, ECS_SIZEOF(TerrainStatus))) {
        ecs_set_ptr(world, e, TerrainStatus, &status);
    }
}

/** Get the tiles of a terrain, made on first use with a container in the scene of the terrain.
 *
 * @param world The world
 * @param ctx The state
 * @param e The terrain
 * @return The tiles
 */
static box_terrain_tiles_t* box_terrain_tiles_of(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    ecs_entity_t e)
{
    box_terrain_tiles_t *result = ecs_map_get_deref(&ctx->terrains, box_terrain_tiles_t, e);
    if (!result) {
        result = ecs_os_calloc_t(box_terrain_tiles_t);
        ecs_map_init(&result->map, NULL);
        ecs_vec_init_t(NULL, &result->list, box_terrain_tile_t*, 0);
        ecs_map_insert_ptr(&ctx->terrains, e, result);
    }

    if (!result->container || !ecs_is_alive(world, result->container)) {
        ecs_entity_t scene = box_world_scene_of(world, e);
        if (ecs_vec_count(&result->list)) {
            while (ecs_vec_count(&result->list)) {
                box_terrain_tile_drop(world, ctx, result, ecs_vec_count(&result->list) - 1,
                    false);
            }
        }

        result->container = scene ? ecs_new_w_pair(world, EcsChildOf, scene) : ecs_new(world);
        ecs_os_memset_n(result->filters, 0, ecs_entity_t, BOX_TERRAIN_LEVELS + 1);
        result->positioned = false;
        result->materials = 0;
    }

    return result;
}

/** Update the tiles of a terrain: select the tiles the foci want, make the missing ones nearest
 * first within the budget, find which tiles draw, drop the others within the budget and keep
 * the TerrainTile, the position and the materials of each tile current.
 *
 * @param world The world
 * @param ctx The state
 * @param e The terrain
 * @param value The terrain value
 */
static void box_terrain_update(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    ecs_entity_t e,
    const Terrain *value)
{
    box_terrain_settings_t s;
    box_terrain_tiles_t *tiles = box_terrain_tiles_of(world, ctx, e);
    double local[3], foci[BOX_TERRAIN_FOCI][2];
    int32_t i, count, made = 0, drops = 0, missing = 0, budget;
    box_terrain_settings(value, &s);
    budget = s.budget;
    s.budget = tiles->settings.budget;
    if (tiles->made &&
        ecs_os_memcmp(&tiles->settings, &s, ECS_SIZEOF(box_terrain_settings_t)))
    {
        box_terrain_drop_all(world, ctx, tiles);
    }

    s.budget = budget;

    tiles->settings = s;
    tiles->made = true;
    if (!s.height) {
        box_terrain_drop_all(world, ctx, tiles);
        goto status;
    }

    box_terrain_local(world, e, local);
    count = box_terrain_foci(world, e, local, foci);
    bool refocus = count && box_terrain_refocus(tiles, foci, count);
    bool moved = !tiles->positioned || local[0] != tiles->placed[0] ||
        local[1] != tiles->placed[1] || local[2] != tiles->placed[2];
    uint64_t materials = box_terrain_materials_hash(world, ctx, e);
    bool restyle = materials != tiles->materials;
    tiles->materials = materials;
    tiles->positioned = true;
    ecs_os_memcpy_n(tiles->placed, local, double, 3);

    box_terrain_tile_t **list = ecs_vec_first(&tiles->list);
    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        list[i]->wanted = false;
    }

    int32_t first = ecs_vec_count(&tiles->list);
    if (count) {
        box_terrain_select(ctx, &s, foci, count);
        for (i = 0; i < ecs_vec_count(&ctx->nodes); i ++) {
            const box_terrain_node_t *node = ecs_vec_get_t(&ctx->nodes, box_terrain_node_t, i);
            box_terrain_tile_t *t = box_terrain_find(tiles, node->level, node->ix, node->iz);
            if (!t && made < s.budget) {
                t = box_terrain_tile_new(world, ctx, tiles, node);
                made ++;
            }

            if (t) {
                t->wanted = true;
                t->distance = node->distance;
            } else {
                missing ++;
            }
        }
    } else {
        for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
            list[i]->wanted = true;
        }
    }

    list = ecs_vec_first(&tiles->list);
    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        box_terrain_tile_t *t = list[i];
        if (!t->ready && i < first) {
            t->ready = box_terrain_texture_ready(world, t->texture) &&
                box_terrain_texture_ready(world, t->masks) &&
                box_terrain_texture_ready(world, t->coarse);
        }
    }

    box_terrain_resolve(tiles, s.levels);
    for (i = ecs_vec_count(&tiles->list) - 1; i >= 0; i --) {
        box_terrain_tile_t *t = *ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, i);
        bool fresh = i >= first;
        if (!t->wanted && !t->keep && drops < s.budget * BOX_TERRAIN_DROPS) {
            box_terrain_tile_drop(world, ctx, tiles, i, true);
            drops ++;
            continue;
        }

        if (fresh || refocus || restyle) {
            box_terrain_tile_write(world, tiles, &s, e, t);
        }

        if (fresh || moved) {
            box_terrain_tile_place(world, &s, t, local);
        }

        if (fresh || restyle) {
            box_terrain_materials_copy(world, ctx, e, t->entity);
        }

        if (t->drawn != t->enabled) {
            ecs_enable(world, t->entity, t->drawn);
            t->enabled = t->drawn;
        }
    }

status:
    box_terrain_status(world, e, tiles, missing);
}

/** Update the tiles of every terrain, in EcsPostUpdate before the world origin shifts. The system
 * is immediate to create the tile entities and textures, and updates the terrains after the
 * iteration as it sets their status.
 *
 * @param it The iterator of the system
 */
static void BoxTerrainTiles(
    ecs_iter_t *it)
{
    box_terrain_ctx_t *ctx = it->ctx;
    int32_t i;
    ecs_vec_clear(&ctx->terrain_list);
    while (ecs_iter_next(it)) {
        for (i = 0; i < it->count; i ++) {
            *ecs_vec_append_t(NULL, &ctx->terrain_list, ecs_entity_t) = it->entities[i];
        }
    }

    for (i = 0; WorldFocus && i < ecs_vec_count(&ctx->terrain_list); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&ctx->terrain_list, ecs_entity_t, i);
        const Terrain *value = ecs_get(it->world, e, Terrain);
        if (value) {
            Terrain terrain = *value;
            box_terrain_update(it->world, ctx, e, &terrain);
        }
    }
}

/** Free the tiles of a terrain, without deleting entities.
 *
 * @param tiles The tiles
 */
static void box_terrain_tiles_free(
    box_terrain_tiles_t *tiles)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        ecs_os_free(*ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, i));
    }

    ecs_vec_fini_t(NULL, &tiles->list, box_terrain_tile_t*);
    ecs_map_fini(&tiles->map);
    ecs_os_free(tiles);
}

/** Delete the tiles of a terrain that loses its Terrain or is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnRemoveTiles(
    ecs_iter_t *it)
{
    box_terrain_ctx_t *ctx = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_terrain_tiles_t *tiles = ecs_map_get_deref(&ctx->terrains, box_terrain_tiles_t,
            it->entities[i]);
        if (!tiles) {
            continue;
        }

        while (ecs_vec_count(&tiles->list)) {
            box_terrain_tile_drop(it->world, ctx, tiles, ecs_vec_count(&tiles->list) - 1,
                false);
        }

        if (tiles->container && ecs_is_alive(it->world, tiles->container)) {
            ecs_delete(it->stage, tiles->container);
        }

        ecs_map_remove(&ctx->terrains, it->entities[i]);
        box_terrain_tiles_free(tiles);
        if (ecs_has(it->world, it->entities[i], TerrainStatus)) {
            ecs_remove(it->stage, it->entities[i], TerrainStatus);
        }
    }

done:
    return;
}

/** Free the state of box.terrain.
 *
 * @param ptr The state
 */
static void box_terrain_ctx_free(
    void *ptr)
{
    box_terrain_ctx_t *ctx = ptr;
    ecs_map_iter_t it = ecs_map_iter(&ctx->terrains);
    while (ecs_map_next(&it)) {
        box_terrain_tiles_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&ctx->terrains);
    ecs_map_fini(&ctx->roots);
    ecs_vec_fini_t(NULL, &ctx->free_slots, int32_t);
    ecs_vec_fini_t(NULL, &ctx->nodes, box_terrain_node_t);
    ecs_vec_fini_t(NULL, &ctx->terrain_list, ecs_entity_t);
    ecs_os_free(ctx);
}

ecs_entity_t box_terrain_mask_node(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t level)
{
    const TerrainModule *m = ecs_singleton_get(world, TerrainModule);
    box_terrain_tiles_t *tiles = m && m->ctx
        ? ecs_map_get_deref(&m->ctx->terrains, box_terrain_tiles_t, terrain) : NULL;

    return tiles && tiles->made ? box_terrain_filter(world, tiles, level) : 0;
}

box_terrain_tile_t* const* box_terrain_tiles(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *count)
{
    const TerrainModule *m = ecs_singleton_get(world, TerrainModule);
    box_terrain_tiles_t *tiles = m && m->ctx
        ? ecs_map_get_deref(&m->ctx->terrains, box_terrain_tiles_t, terrain) : NULL;
    *count = tiles ? ecs_vec_count(&tiles->list) : 0;

    return tiles && *count ? ecs_vec_first(&tiles->list) : NULL;
}

bool box_terrain_window(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    box_terrain_window_t *out)
{
    const TerrainModule *m = ecs_singleton_get(world, TerrainModule);
    box_terrain_tiles_t *tiles = m && m->ctx
        ? ecs_map_get_deref(&m->ctx->terrains, box_terrain_tiles_t, terrain) : NULL;
    bool result = false;
    int32_t level, i, reach;
    ecs_os_memset_t(out, 0, box_terrain_window_t);
    if (!tiles || !tiles->made || !tiles->settings.height || !tiles->focus_count) {
        goto done;
    }

    const box_terrain_settings_t *s = &tiles->settings;
    reach = (int32_t)ceil(s->lod_distance / s->tile_size) + 2 + BOX_TERRAIN_WINDOW_MARGIN;
    out->levels = s->levels;
    out->cells = 2 * reach;
    out->cells = out->cells > BOX_TERRAIN_WINDOW_CELLS ? BOX_TERRAIN_WINDOW_CELLS : out->cells;
    out->resolution = s->resolution;
    out->focus_count = tiles->focus_count;
    out->tile_size = s->tile_size;
    out->extent = s->extent;
    for (i = 0; i < tiles->focus_count; i ++) {
        out->foci[i][0] = tiles->foci[i][0];
        out->foci[i][1] = tiles->foci[i][1];
    }

    for (level = 0; level < s->levels; level ++) {
        double size = box_terrain_size(s, level), range = box_terrain_range(s, level);
        out->first[level][0] = (int32_t)floor(tiles->foci[0][0] / size) - out->cells / 2;
        out->first[level][1] = (int32_t)floor(tiles->foci[0][1] / size) - out->cells / 2;
        out->morph[level][0] = (float)box_terrain_morph(s, level);
        out->morph[level][1] = (float)range;
    }

    result = true;
done:
    return result;
}

void box_terrain_tile_surface(
    const TerrainTile *tile,
    box_surface_data_t *data,
    vec3 min,
    vec3 max)
{
    int32_t resolution = box_terrain_vertices(tile->resolution);
    data->p[0][0] = BOX_TERRAIN_SURFACE;
    data->p[1][0] = (float)tile->slot;
    data->p[1][1] = (float)(box_terrain_row_bytes(resolution) / 4u);
    data->p[1][2] = (float)(resolution + 2 * BOX_TERRAIN_BORDER);
    data->p[1][3] = (float)resolution;
    data->p[2][0] = tile->focus[0];
    data->p[2][1] = tile->focus[1];
    data->p[2][2] = tile->morph[0];
    data->p[2][3] = tile->morph[1];
    data->p[3][0] = tile->masked ? 1.0f : 0.0f;
    data->p[4][0] = tile->size;
    data->p[4][1] = tile->skirt;
    data->p[4][2] = (float)tile->level;
    data->p[4][3] = tile->bounded ? 1.0f : 0.0f;
    data->p[5][0] = tile->clip[0];
    data->p[5][1] = tile->clip[1];
    data->p[5][2] = tile->clip[2];
    data->p[5][3] = tile->clip[3];
    glm_vec3_copy((vec3){ -0.5f, tile->min_height - tile->skirt, -0.5f }, min);
    glm_vec3_copy((vec3){ 0.5f, tile->max_height, 0.5f }, max);
}

/** Key of the mesh of a TerrainTile: its vertices per edge.
 *
 * @param value The tile
 * @param key The key
 */
static void box_terrain_tile_key(
    const void *value,
    box_shape_key_t *key)
{
    const TerrainTile *t = value;
    key->i[0] = box_terrain_vertices(t->resolution);
}

/** Scale of a TerrainTile: its edge along x and z.
 *
 * @param value The tile
 * @param scale The scale
 */
static void box_terrain_tile_scale(
    const void *value,
    vec3 scale)
{
    const TerrainTile *t = value;
    float size = isfinite(t->size) && t->size > 0.0f ? t->size : 1.0f;
    glm_vec3_copy((vec3){ size, 1.0f, size }, scale);
}

/** Surface of a TerrainTile.
 *
 * @param value The tile
 * @param data The surface
 * @param min The minimum corner of the local bounds
 * @param max The maximum corner of the local bounds
 */
static void box_terrain_tile_data(
    const void *value,
    box_surface_data_t *data,
    vec3 min,
    vec3 max)
{
    box_terrain_tile_surface(value, data, min, max);
}

/** Append the skirt of an edge of the grid: a copy of each vertex of the edge at y -1 and the
 * quads between them, facing out.
 *
 * @param mesh The mesh
 * @param n Vertices per edge
 * @param start Index of the first vertex of the edge
 * @param stride Index step along the edge
 * @param outward Direction out of the grid across the edge
 */
static void box_terrain_skirt(
    box_mesh_t *mesh,
    int32_t n,
    int32_t start,
    int32_t stride,
    const vec3 outward)
{
    int32_t first = mesh->vertex_count, k;
    for (k = 0; k < n; k ++) {
        const box_vertex_t *top = &mesh->vertices[start + k * stride];
        vec3 position = { top->position[0], -1.0f, top->position[2] };
        box_mesh_vertex(mesh, position, (vec3){ 0.0f, 1.0f, 0.0f }, top->uv[0], top->uv[1]);
    }

    vec3 a, b, normal;
    glm_vec3_sub(mesh->vertices[first].position, mesh->vertices[start].position, a);
    glm_vec3_sub(mesh->vertices[start + stride].position, mesh->vertices[start].position, b);
    glm_vec3_cross(a, b, normal);
    bool flip = glm_vec3_dot(normal, ECS_CONST_CAST(float*, outward)) < 0.0f;
    for (k = 0; k < n - 1; k ++) {
        int32_t t0 = start + k * stride, t1 = start + (k + 1) * stride;
        int32_t s0 = first + k, s1 = first + k + 1;
        if (flip) {
            box_mesh_triangle(mesh, t0, t1, s0);
            box_mesh_triangle(mesh, t1, s1, s0);
        } else {
            box_mesh_triangle(mesh, t0, s0, t1);
            box_mesh_triangle(mesh, t1, s0, s1);
        }
    }
}

/** Generate the grid of a TerrainTile: n x n vertices over the unit square in XZ, each cell split
 * from its lower to its higher corner as the morphed cells are, skirts at y -1 below the edges,
 * and one surface asset, so its draws bind the heights the backend copies into the assets.
 *
 * @param value The tile
 * @param key The key
 * @param mesh The mesh
 */
static void box_terrain_tile_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    int32_t n = box_terrain_vertices(key->i[0]), i, j;
    float last = (float)(n - 1);
    (void)value;
    for (j = 0; j < n; j ++) {
        for (i = 0; i < n; i ++) {
            float u = (float)i / last, v = (float)j / last;
            box_mesh_vertex(mesh, (vec3){ u - 0.5f, 0.0f, v - 0.5f },
                (vec3){ 0.0f, 1.0f, 0.0f }, u, v);
        }
    }

    for (j = 0; j < n - 1; j ++) {
        for (i = 0; i < n - 1; i ++) {
            int32_t a = j * n + i, b = a + 1, c = a + n + 1, d = a + n;
            box_mesh_triangle(mesh, a, c, b);
            box_mesh_triangle(mesh, a, d, c);
        }
    }

    box_terrain_skirt(mesh, n, 0, 1, (vec3){ 0.0f, 0.0f, -1.0f });
    box_terrain_skirt(mesh, n, n - 1, n, (vec3){ 1.0f, 0.0f, 0.0f });
    box_terrain_skirt(mesh, n, (n - 1) * n, 1, (vec3){ 0.0f, 0.0f, 1.0f });
    box_terrain_skirt(mesh, n, 0, n, (vec3){ -1.0f, 0.0f, 0.0f });
    mesh->surface_assets = ecs_os_calloc_n(vec4, 1);
    mesh->surface_asset_count = 1;
}

void box_terrain_tiles_import(
    ecs_world_t *world)
{
    box_terrain_ctx_t *ctx = ecs_os_calloc_t(box_terrain_ctx_t);
    ecs_map_init(&ctx->terrains, NULL);
    ecs_map_init(&ctx->roots, NULL);
    ecs_vec_init_t(NULL, &ctx->free_slots, int32_t, 0);
    ecs_vec_init_t(NULL, &ctx->nodes, box_terrain_node_t, 0);
    ecs_vec_init_t(NULL, &ctx->terrain_list, ecs_entity_t, 0);
    ecs_id_t materials[BOX_TERRAIN_MATERIALS] = {
        ecs_id(Rgb), ecs_id(Tint), ecs_id(PbrMaterial), ecs_id(ClearCoat), ecs_id(Sheen),
        ecs_id(Emissive), ecs_id(PbrTextures), ecs_id(TextureMapping),
        ecs_id(TextureTransform), ecs_id(TextureWrap), ecs_id(TextureLayers), NoShadow,
        ecs_id(TextureBlend)
    };
    ecs_os_memcpy_n(ctx->materials, materials, ecs_id_t, BOX_TERRAIN_MATERIALS);

    ECS_COMPONENT_DEFINE(world, TerrainTile);
    ecs_add_pair(world, ecs_id(TerrainTile), EcsOnInstantiate, EcsDontInherit);
    ECS_COMPONENT_DEFINE(world, TerrainModule);
    ecs_singleton_set(world, TerrainModule, { .ctx = ctx });

    box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(TerrainTile),
        .key = box_terrain_tile_key,
        .generate = box_terrain_tile_generate,
        .scale = box_terrain_tile_scale,
        .surface = box_terrain_tile_data
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Tiles" }),
        .phase = EcsPostUpdate,
        .query.terms = {{ .id = ecs_id(Terrain), .src.id = EcsSelf }},
        .run = BoxTerrainTiles,
        .ctx = ctx,
        .ctx_free = box_terrain_ctx_free,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnRemoveTiles" }),
        .query.terms = {{ .id = ecs_id(Terrain), .src.id = EcsSelf }},
        .events = { EcsOnRemove },
        .callback = BoxTerrainOnRemoveTiles,
        .ctx = ctx
    });
}

bool box_terrain_is_tile(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_id(TerrainTile) && ecs_has(world, e, TerrainTile);
}
