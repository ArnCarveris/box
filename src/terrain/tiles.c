/**
 * @file terrain/tiles.c
 * @brief The Terrain height field: a quadtree of tiles around the focus of the world, each a
 * TextureGraph of the heights over its rectangle and a TerrainTile record that the vertex stage
 * displaces by them, made and dropped within a budget per frame.
 *
 * Tiles are not entities: the renderer draws them as the instances of a batch of the terrain
 * (box_render_terrain_draws). A tile does not draw while its heights are pending, or another
 * tile covers it. Its textures are children of a container in the scene of the terrain.
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

/** Tiles waiting for their heights at most, per tile of the budget */
#define BOX_TERRAIN_WAITING (4)

/** Reach of the TexBlur of the masks of a level above 0 in cells of the level: three standard
 * deviations, so a standard deviation of half a cell */
#define BOX_TERRAIN_MASK_BLUR (1.5)

/** Depth of the skirts of a tile in cells of its level */
#define BOX_TERRAIN_SKIRT (0.1)

/** Heights sampled along each axis of the square around a focus whose heights give its
 * clearance above or below the terrain */
#define BOX_TERRAIN_CLEARANCE_SAMPLES (17)

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

/** The heights of the grid around a focus that give its clearance (box_terrain_clearance) */
typedef struct box_terrain_grid_t {
    int32_t x0;             /**< Column of the first sample */
    int32_t z0;             /**< Row of the first sample */
    double step;            /**< Meters between the samples */
    double lo;              /**< Lowest height known, above hi when none is */
    double hi;              /**< Highest height known */
    uint64_t revision;      /**< box_terrain_revision the heights were read at */
    bool valid;             /**< The other members are set */
} box_terrain_grid_t;

/** The tiles of a terrain */
typedef struct box_terrain_tiles_t {
    ecs_map_t map;          /**< Key of a node to its box_terrain_tile_t */
    ecs_vec_t list;         /**< box_terrain_tile_t*, every tile */
    ecs_entity_t container; /**< Parent of the texture entities of the tiles and the filters */
    ecs_entity_t filters[BOX_TERRAIN_LEVELS + 1]; /**< TexBlur node of the masks of each level
                             *   above 0, children of the container, 0 until made */
    box_terrain_settings_t settings; /**< Settings the tiles were made with */
    bool made;              /**< settings holds the settings of the tiles */
    double foci[BOX_TERRAIN_FOCI][3]; /**< Foci the tiles are selected for and morph around, x
                             *   and z in the frame of the terrain and the clearance of the
                             *   focus above or below the terrain around it */
    int32_t focus_count;    /**< Foci in foci */
    double placed[3];       /**< Position of the terrain the tiles were placed for */
    bool positioned;        /**< placed holds a position */
    bool blended;           /**< The terrain had a TextureBlend when the tiles were written */
    int32_t created;        /**< Tiles made */
    int32_t dropped;        /**< Tiles deleted */
    ecs_vec_t nodes;        /**< box_terrain_node_t, the nodes the foci want, nearest first */
    ecs_map_t roots;        /**< Keys of the root nodes the foci select */
    bool selected;          /**< nodes and roots are those of the settings and foci */
    double seen[BOX_TERRAIN_FOCI][3]; /**< Foci of the last frame as they are, x, z and y in the
                             *   frame of the terrain */
    int32_t seen_count;     /**< Foci in seen, -1 before the first frame */
    box_terrain_grid_t grids[BOX_TERRAIN_FOCI]; /**< Heights around each focus */
    double radius;          /**< Terrain.query_radius the clearances were measured with */
    bool query_only;        /**< Terrain.query_only of the last frame */
    uint64_t revision;      /**< box_terrain_revision of the last frame */
    bool dirty;             /**< The status of a texture of a tile was set since the last update */
    bool style;             /**< The terrain got or lost its TextureBlend since the tiles were
                             *   written */
    bool active;            /**< The last update changed a tile, so the next one may too */
    int32_t updates;        /**< Updates that went over the tiles */
    int32_t selects;        /**< Selections of the nodes the foci want */
    int32_t version;        /**< Bumped when the record, the position or the drawing of a tile
                             *   changed, or a tile was dropped */
} box_terrain_tiles_t;

/** State of box.terrain in a world */
typedef struct box_terrain_ctx_t {
    ecs_map_t terrains;     /**< Terrain entity to its box_terrain_tiles_t */
    ecs_vec_t free_slots;   /**< int32_t, slots of the heights given back */
    int32_t next_slot;      /**< Slot after the highest one taken */
    ecs_map_t textures;     /**< Texture entity of a tile to the box_terrain_tiles_t of its
                             *   terrain */
    ecs_vec_t terrain_list; /**< ecs_entity_t, the terrains to update this frame */
    ecs_entity_t module;    /**< The box.terrain module */
    ecs_entity_t texture_observer; /**< The TextureStatus observer, made with the first terrain */
} box_terrain_ctx_t;

/** Singleton that holds the state of box.terrain */
typedef struct TerrainModule {
    box_terrain_ctx_t *ctx; /**< The state */
} TerrainModule;

static ECS_COMPONENT_DECLARE(TerrainModule);

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

    out->budget = t->budget > 0 ? t->budget : 4;
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

/** Get the distance from a focus to the rectangle of a node, its clearance inside it.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @param focus The focus, x and z in the frame of the terrain and its clearance
 * @return Meters
 */
static double box_terrain_node_distance(
    const box_terrain_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz,
    const double focus[3])
{
    double size = box_terrain_size(s, level);
    double x0 = (double)ix * size, z0 = (double)iz * size;
    double dx = fmax(fmax(x0 - focus[0], focus[0] - (x0 + size)), 0.0);
    double dz = fmax(fmax(z0 - focus[1], focus[1] - (z0 + size)), 0.0);

    return sqrt(dx * dx + dz * dz + focus[2] * focus[2]);
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
    const double foci[][3],
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
 * @param tiles The tiles, receive the nodes
 * @param s The settings
 * @param foci The foci
 * @param count Number of foci
 * @param level The level of the node
 * @param ix The column
 * @param iz The row
 */
static void box_terrain_select_node(
    box_terrain_tiles_t *tiles,
    const box_terrain_settings_t *s,
    const double foci[][3],
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
        ecs_vec_count(&tiles->nodes) + 4 <= BOX_TERRAIN_MAX_TILES)
    {
        for (b = 0; b < 2; b ++) {
            for (a = 0; a < 2; a ++) {
                box_terrain_select_node(tiles, s, foci, count, level - 1, ix * 2 + a,
                    iz * 2 + b);
            }
        }

        goto done;
    }

    *ecs_vec_append_t(NULL, &tiles->nodes, box_terrain_node_t) = (box_terrain_node_t){
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
 * @param tiles The tiles, receive the nodes and the root nodes
 * @param s The settings
 * @param foci The foci
 * @param count Number of foci
 */
static void box_terrain_select(
    box_terrain_tiles_t *tiles,
    const box_terrain_settings_t *s,
    const double foci[][3],
    int32_t count)
{
    int32_t top = s->levels - 1, i, ix, iz;
    double size = box_terrain_size(s, top), range = box_terrain_range(s, top);
    ecs_vec_clear(&tiles->nodes);
    ecs_map_clear(&tiles->roots);
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
                if (ecs_map_get(&tiles->roots, key) ||
                    box_terrain_node_distance(s, top, ix, iz, foci[i]) >= range)
                {
                    continue;
                }

                ecs_map_insert(&tiles->roots, key, 1);
                box_terrain_select_node(tiles, s, foci, count, top, ix, iz);
            }
        }
    }

    if (ecs_vec_count(&tiles->nodes) > 1) {
        qsort(ecs_vec_first(&tiles->nodes), (size_t)ecs_vec_count(&tiles->nodes),
            sizeof(box_terrain_node_t), box_terrain_node_compare);
    }

    tiles->selects ++;
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

/** Check whether a texture of a tile failed to load, so that its heights never come.
 *
 * @param world The world
 * @param t The tile
 * @return Whether a texture of the tile failed
 */
static bool box_terrain_tile_failed(
    const ecs_world_t *world,
    const box_terrain_tile_t *t)
{
    ecs_entity_t textures[4] = { t->texture, t->rough, t->masks, t->coarse };
    bool result = false;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        const TextureStatus *status = textures[i]
            ? ecs_get(world, textures[i], TextureStatus) : NULL;
        result = result || (status && status->state == TextureFailed);
    }

    return result;
}

/** Write the TerrainTile of a tile: its node, slot, focus and morph (box_terrain_morph), and
 * bump the version of the tiles when it changed.
 *
 * The vertex stage measures the distance along the ground, so the morph distances are those
 * along the ground at which the focus with its clearance is that far. The bounds of the tile are
 * the heights it has once measured, else those of the terrain.
 *
 * @param world The world
 * @param tiles The tiles of the terrain
 * @param s The settings
 * @param e The terrain
 * @param t The tile
 */
static void box_terrain_tile_write(
    ecs_world_t *world,
    box_terrain_tiles_t *tiles,
    const box_terrain_settings_t *s,
    ecs_entity_t e,
    box_terrain_tile_t *t)
{
    double size = box_terrain_size(s, t->level), range = box_terrain_range(s, t->level);
    double cx = ((double)t->ix + 0.5) * size, cz = ((double)t->iz + 0.5) * size;
    double morph = box_terrain_morph(s, t->level);
    double fx = cx, fz = cz, fc = 0.0, best = DBL_MAX;
    int32_t i;
    for (i = 0; i < tiles->focus_count; i ++) {
        double dx = tiles->foci[i][0] - cx, dz = tiles->foci[i][1] - cz;
        double d = dx * dx + dz * dz + tiles->foci[i][2] * tiles->foci[i][2];
        if (d < best) {
            best = d;
            fx = tiles->foci[i][0];
            fz = tiles->foci[i][1];
            fc = tiles->foci[i][2];
        }
    }

    morph = sqrt(fmax(morph * morph - fc * fc, 0.0));
    range = sqrt(fmax(range * range - fc * fc, 0.0));
    float cell = (float)(size / (double)(s->resolution - 1));
    TerrainTile v;
    ecs_os_memset_t(&v, 0, TerrainTile);
    v.terrain = e;
    v.texture = t->texture;
    v.masks = t->masks;
    v.coarse = t->coarse;
    v.rough = t->rough;
    v.level = t->level;
    v.ix = t->ix;
    v.iz = t->iz;
    v.resolution = s->resolution;
    v.slot = t->slot;
    v.size = (float)size;
    v.focus[0] = (float)(fx - cx);
    v.focus[1] = (float)(fz - cz);
    v.morph[0] = (float)morph;
    v.morph[1] = (float)range;
    v.skirt = (float)BOX_TERRAIN_SKIRT * cell;
    v.min_height = t->measured ? t->low : s->min_height;
    v.max_height = t->measured ? t->high : s->max_height;
    v.bounded = s->extent > 0.0;
    v.masked = t->masks && t->coarse && ecs_has(world, e, TextureBlend);
    v.clip[0] = (float)(-s->extent - cx);
    v.clip[1] = (float)(-s->extent - cz);
    v.clip[2] = (float)(s->extent - cx);
    v.clip[3] = (float)(s->extent - cz);
    if (ecs_os_memcmp_t(&t->shape, &v, TerrainTile)) {
        t->shape = v;
        tiles->version ++;
    }
}

/** Place a tile at the center of its rectangle in the frame of its terrain, and bump the
 * version of the tiles when it moved.
 *
 * @param tiles The tiles of the terrain
 * @param s The settings
 * @param t The tile
 * @param local The local position of the terrain
 */
static void box_terrain_tile_place(
    box_terrain_tiles_t *tiles,
    const box_terrain_settings_t *s,
    box_terrain_tile_t *t,
    const double local[3])
{
    double size = box_terrain_size(s, t->level);
    double position[3] = {
        local[0] + ((double)t->ix + 0.5) * size,
        local[1],
        local[2] + ((double)t->iz + 0.5) * size
    };
    if (ecs_os_memcmp(t->position, position, ECS_SIZEOF(double) * 3)) {
        ecs_os_memcpy_n(t->position, position, double, 3);
        tiles->version ++;
    }
}

/** Make the tile of a node, which does not draw yet, with the TextureGraph of its heights under
 * the container, the window grown by BOX_TERRAIN_BORDER texels on each side.
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
    float priority = (float)-node->distance;
    t->level = node->level;
    t->ix = node->ix;
    t->iz = node->iz;
    t->distance = node->distance;
    t->slot = box_terrain_slot_take(ctx);
    t->texture = ecs_new_w_pair(world, EcsChildOf, tiles->container);
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
        .format = GraphFloat,
        .priority = priority
    });
    ecs_set(world, t->texture, TextureReadback, { .keep = false });
    const TextureGraph *heights = ecs_get(world, t->texture, TextureGraph);
    TextureGraphWindow window = heights->window;
    int32_t coarse = box_terrain_coarse_texels(s->resolution);
    t->rough = ecs_new_w_pair(world, EcsChildOf, tiles->container);
    ecs_set(world, t->rough, TextureGraph, {
        .output = s->height,
        .width = coarse,
        .height = coarse,
        .domain = DomainWorld,
        .window = window,
        .format = GraphFloat,
        .priority = priority
    });
    ecs_set(world, t->rough, TextureReadback, { .keep = false });
    if (s->masks) {
        t->masks = ecs_new_w_pair(world, EcsChildOf, tiles->container);
        ecs_set(world, t->masks, TextureGraph, {
            .output = box_terrain_filter(world, tiles, node->level),
            .width = texels,
            .height = texels,
            .domain = DomainWorld,
            .window = window,
            .format = GraphLinear,
            .priority = priority
        });
        t->coarse = ecs_new_w_pair(world, EcsChildOf, tiles->container);
        ecs_set(world, t->coarse, TextureGraph, {
            .output = box_terrain_filter(world, tiles, node->level + 1),
            .width = coarse,
            .height = coarse,
            .domain = DomainWorld,
            .window = window,
            .format = GraphLinear,
            .priority = priority
        });
    }

    ecs_map_insert_ptr(&tiles->map, box_terrain_key(t->level, t->ix, t->iz), t);
    *ecs_vec_append_t(NULL, &tiles->list, box_terrain_tile_t*) = t;
    tiles->created ++;
    ecs_map_insert_ptr(&ctx->textures, t->texture, tiles);
    ecs_map_insert_ptr(&ctx->textures, t->rough, tiles);
    if (t->masks) {
        ecs_map_insert_ptr(&ctx->textures, t->masks, tiles);
        ecs_map_insert_ptr(&ctx->textures, t->coarse, tiles);
    }

    return t;
}

/** Delete a tile and give back its slot.
 *
 * @param world The world
 * @param ctx The state
 * @param tiles The tiles of the terrain
 * @param index Index of the tile in the list
 * @param entities Whether to delete the texture entities, false when the container goes with
 * them
 */
static void box_terrain_tile_drop(
    ecs_world_t *world,
    box_terrain_ctx_t *ctx,
    box_terrain_tiles_t *tiles,
    int32_t index,
    bool entities)
{
    box_terrain_tile_t *t = *ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, index);
    ecs_entity_t textures[4] = { t->texture, t->rough, t->masks, t->coarse };
    int32_t i;
    for (i = 0; entities && i < 4; i ++) {
        if (textures[i] && ecs_is_alive(world, textures[i])) {
            ecs_delete(world, textures[i]);
        }
    }

    box_terrain_slot_give(ctx, t->slot);
    ecs_map_remove(&ctx->textures, t->texture);
    ecs_map_remove(&ctx->textures, t->rough);
    if (t->masks) {
        ecs_map_remove(&ctx->textures, t->masks);
        ecs_map_remove(&ctx->textures, t->coarse);
    }

    tiles->active = true;
    tiles->version ++;
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
    tiles->style = true;
}

/** Stop drawing every tile under a coarser tile that draws, so the tiles that draw do not
 * overlap.
 *
 * @param tiles The tiles
 * @param levels Levels of the terrain
 */
static void box_terrain_disjoint(
    box_terrain_tiles_t *tiles,
    int32_t levels)
{
    box_terrain_tile_t **list = ecs_vec_first(&tiles->list);
    int32_t count = ecs_vec_count(&tiles->list), i;
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

/** Check whether the tiles that draw cover the part of a node inside the terrain: the tile of
 * the node draws, or tiles of finer levels cover each of its four children.
 *
 * @param tiles The tiles
 * @param level The level of the node
 * @param ix The column
 * @param iz The row
 * @return Whether nothing of the node is left without a tile that draws
 */
static bool box_terrain_covered_below(
    const box_terrain_tiles_t *tiles,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    const box_terrain_tile_t *t = box_terrain_find(tiles, level, ix, iz);
    bool result = !box_terrain_inside(&tiles->settings, level, ix, iz) || (t && t->drawn);
    if (!result && level) {
        result = box_terrain_covered_below(tiles, level - 1, ix * 2, iz * 2) &&
            box_terrain_covered_below(tiles, level - 1, ix * 2 + 1, iz * 2) &&
            box_terrain_covered_below(tiles, level - 1, ix * 2, iz * 2 + 1) &&
            box_terrain_covered_below(tiles, level - 1, ix * 2 + 1, iz * 2 + 1);
    }

    return result;
}

/** Check whether the tiles that draw cover all of a tile: a coarser tile over it draws, or finer
 * tiles cover it (box_terrain_covered_below).
 *
 * @param tiles The tiles
 * @param t The tile
 * @param levels Levels of the terrain
 * @return Whether the tile can stop drawing without leaving a hole
 */
static bool box_terrain_covered(
    const box_terrain_tiles_t *tiles,
    const box_terrain_tile_t *t,
    int32_t levels)
{
    int32_t level = t->level, ix = t->ix, iz = t->iz;
    bool result = false;
    while (!result && level + 1 < levels) {
        level ++;
        ix = box_terrain_half(ix);
        iz = box_terrain_half(iz);
        const box_terrain_tile_t *a = box_terrain_find(tiles, level, ix, iz);
        result = a && a->drawn;
    }

    return result || box_terrain_covered_below(tiles, t->level, t->ix, t->iz);
}

/** Check whether a tile lies in a root node the foci selected this frame (box_terrain_select),
 * whose area the wanted tiles cover.
 *
 * @param tiles The tiles, with the root nodes the foci select
 * @param t The tile
 * @param levels Levels of the terrain
 * @return Whether the foci want the area of the tile
 */
static bool box_terrain_selected(
    const box_terrain_tiles_t *tiles,
    const box_terrain_tile_t *t,
    int32_t levels)
{
    int32_t level = t->level, ix = t->ix, iz = t->iz;
    while (level + 1 < levels) {
        level ++;
        ix = box_terrain_half(ix);
        iz = box_terrain_half(iz);
    }

    return ecs_map_get(&tiles->roots, box_terrain_key(level, ix, iz)) != NULL;
}

/** Decide which tiles draw: a wanted tile that is ready; for one that is not, its nearest ready
 * ancestor, else its ready descendants; and no tile under a coarser tile that draws.
 *
 * A tile that drew in the frame before keeps drawing while the foci want its area and the tiles
 * that draw do not cover all of it, as when the budget has not made its replacements yet, so
 * the terrain never shows a hole where it showed ground.
 *
 * @param tiles The tiles, with the root nodes the foci select
 * @param levels Levels of the terrain
 * @param focused Whether the foci selected the tiles, else every tile is wanted
 */
static void box_terrain_resolve(
    box_terrain_tiles_t *tiles,
    int32_t levels,
    bool focused)
{
    box_terrain_tile_t **list = ecs_vec_first(&tiles->list);
    int32_t count = ecs_vec_count(&tiles->list), i, j, ready = 0;
    bool held = false;
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

    box_terrain_disjoint(tiles, levels);
    if (!focused) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        box_terrain_tile_t *t = list[i];
        if (t->enabled && t->ready && !t->drawn && box_terrain_selected(tiles, t, levels) &&
            !box_terrain_covered(tiles, t, levels))
        {
            t->drawn = true;
            t->keep = true;
            held = true;
        }
    }

    if (held) {
        box_terrain_disjoint(tiles, levels);
    }
done:
    return;
}

void box_terrain_position(
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
        box_dvec3_copy(t->position, out);
    }
}

int32_t box_terrain_foci(
    const ecs_world_t *world,
    ecs_entity_t e,
    const double local[3],
    double out[BOX_TERRAIN_FOCI][2],
    double *heights)
{
    box_world_focus_t foci[BOX_TERRAIN_FOCI];
    ecs_entity_t scene = box_world_scene_of(world, e);
    int32_t count = box_world_focus(world, foci, BOX_TERRAIN_FOCI), i, result = 0;
    count = count < BOX_TERRAIN_FOCI ? count : BOX_TERRAIN_FOCI;
    for (i = 0; i < count; i ++) {
        if (foci[i].scene == scene) {
            out[result][0] = foci[i].position[0] - local[0];
            out[result][1] = foci[i].position[2] - local[2];
            if (heights) {
                heights[result] = foci[i].position[1] - local[1];
            }

            result ++;
        }
    }

    for (i = 0; i < count && !result; i ++) {
        out[i][0] = foci[i].position[0] - local[0];
        out[i][1] = foci[i].position[2] - local[2];
        if (heights) {
            heights[i] = foci[i].position[1] - local[1];
        }
    }

    return result ? result : count;
}

/** Get the clearance of a focus: how far it is above the highest or below the lowest of the
 * heights of the terrain within the reach of its coarse query tiles, sampled on a grid fixed in
 * the frame of the terrain, 0 where none of them is known or it is between them.
 *
 * The heights of the grid are read again only when the focus reaches other samples or the
 * heights changed (box_terrain_revision).
 *
 * @param world The world
 * @param e The terrain
 * @param value The terrain value
 * @param grid The heights of the grid around the focus, kept between calls
 * @param revision The box_terrain_revision of the world
 * @param x X of the focus in the frame of the terrain
 * @param z Z of the focus in the frame of the terrain
 * @param y Y of the focus in the frame of the terrain
 * @return Meters
 */
static double box_terrain_clearance(
    const ecs_world_t *world,
    ecs_entity_t e,
    const Terrain *value,
    box_terrain_grid_t *grid,
    uint64_t revision,
    double x,
    double z,
    double y)
{
    double radius = isfinite(value->query_radius) && value->query_radius > 0.0f
        ? value->query_radius : 64.0;
    double reach = radius * BOX_TERRAIN_QUERY_SCALE;
    double step = 2.0 * reach / (double)(BOX_TERRAIN_CLEARANCE_SAMPLES - 1);
    double lo, hi, result = 0.0;
    int32_t x0 = (int32_t)ceil((x - reach) / step), z0 = (int32_t)ceil((z - reach) / step);
    int32_t i, j;
    float h;
    if (!grid->valid || grid->x0 != x0 || grid->z0 != z0 || grid->step != step ||
        grid->revision != revision)
    {
        lo = DBL_MAX;
        hi = -DBL_MAX;
        for (j = 0; j < BOX_TERRAIN_CLEARANCE_SAMPLES; j ++) {
            for (i = 0; i < BOX_TERRAIN_CLEARANCE_SAMPLES; i ++) {
                if (box_terrain_height(world, e, (double)(x0 + i) * step,
                    (double)(z0 + j) * step, &h))
                {
                    lo = fmin(lo, h);
                    hi = fmax(hi, h);
                }
            }
        }

        *grid = (box_terrain_grid_t){ .x0 = x0, .z0 = z0, .step = step, .lo = lo, .hi = hi,
            .revision = revision, .valid = true };
    }

    lo = grid->lo;
    hi = grid->hi;
    if (box_terrain_height(world, e, x, z, &h)) {
        lo = fmin(lo, h);
        hi = fmax(hi, h);
    }

    if (lo <= hi) {
        result = fmax(fmax(y - hi, lo - y), 0.0);
    }

    return result;
}

/** Check whether the foci moved far enough from those the tiles are selected for and morph
 * around that the tiles take the new ones, and take them.
 *
 * @param tiles The tiles
 * @param foci The foci with their clearance
 * @param count Number of foci
 * @return Whether the tiles morph around new foci
 */
static bool box_terrain_refocus(
    box_terrain_tiles_t *tiles,
    const double foci[][3],
    int32_t count)
{
    double step = tiles->settings.tile_size * BOX_TERRAIN_FOCUS_STEP;
    bool result = count != tiles->focus_count;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        result = fabs(foci[i][0] - tiles->foci[i][0]) > step ||
            fabs(foci[i][1] - tiles->foci[i][1]) > step ||
            fabs(foci[i][2] - tiles->foci[i][2]) > step;
    }

    if (result) {
        ecs_os_memcpy_n(tiles->foci, foci, double, 3 * count);
        tiles->focus_count = count;
    }

    return result;
}

/** Measure the bounds of a tile from the copies of its heights and rough heights once both
 * arrived, and stop reading them back; read them back again when an image changes after.
 *
 * @param world The world
 * @param t The tile
 * @return Whether the bounds of the tile changed
 */
static bool box_terrain_measure(
    ecs_world_t *world,
    box_terrain_tile_t *t)
{
    box_texture_copy_t copies[2];
    ecs_entity_t textures[2] = { t->texture, t->rough };
    float lo = FLT_MAX, hi = -FLT_MAX;
    bool result = false;
    int32_t k, i;
    if (t->measured) {
        for (k = 0; k < 2; k ++) {
            const TextureStatus *status = ecs_get(world, textures[k], TextureStatus);
            result |= status && status->revision > t->revisions[k];
        }

        for (k = 0; result && k < 2; k ++) {
            ecs_set(world, textures[k], TextureReadback, { .keep = false });
        }

        t->measured = !result;
        goto done;
    }

    if (!box_texture_readback(world, t->texture, &copies[0]) ||
        !box_texture_readback(world, t->rough, &copies[1]))
    {
        goto done;
    }

    for (k = 0; k < 2; k ++) {
        const float *texels = copies[k].data;
        int32_t n = copies[k].width * copies[k].height;
        if (copies[k].format != ReadbackR32Float || !texels) {
            goto done;
        }

        for (i = 0; i < n; i ++) {
            if (isfinite(texels[i])) {
                lo = texels[i] < lo ? texels[i] : lo;
                hi = texels[i] > hi ? texels[i] : hi;
            }
        }
    }

    for (k = 0; k < 2; k ++) {
        const TextureStatus *status = ecs_get(world, textures[k], TextureStatus);
        if (status && status->revision > copies[k].revision) {
            goto done;
        }
    }

    for (k = 0; k < 2; k ++) {
        t->revisions[k] = copies[k].revision;
        ecs_remove(world, textures[k], TextureReadback);
    }

    t->measured = lo <= hi;
    t->low = lo;
    t->high = hi;
    result = t->measured;
done:
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
    int64_t bytes = ((int64_t)texels * texels + (int64_t)coarse * coarse) * 4 +
        (int64_t)box_terrain_row_bytes(tiles->settings.resolution) * (texels + coarse);
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

/** Mark the tiles of the terrain of every tile texture whose TextureStatus was set, so their
 * next update reads which tiles are ready and which heights changed.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnTextureStatus(
    ecs_iter_t *it)
{
    box_terrain_ctx_t *ctx = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_terrain_tiles_t *tiles = ecs_map_get_deref(&ctx->textures, box_terrain_tiles_t,
            it->entities[i]);
        if (tiles) {
            tiles->dirty = true;
        }
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
        ecs_map_init(&result->roots, NULL);
        ecs_vec_init_t(NULL, &result->list, box_terrain_tile_t*, 0);
        ecs_vec_init_t(NULL, &result->nodes, box_terrain_node_t, 0);
        result->seen_count = -1;
        result->style = true;
        result->active = true;
        ecs_map_insert_ptr(&ctx->terrains, e, result);
        if (!ctx->texture_observer) {
            ctx->texture_observer = ecs_observer(world, {
                .entity = ecs_entity(world, { .name = "OnTextureStatus", .parent = ctx->module }),
                .query.terms = {{ .id = ecs_id(TextureStatus), .src.id = EcsSelf }},
                .events = { EcsOnSet },
                .callback = BoxTerrainOnTextureStatus,
                .ctx = ctx
            });
        }
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
        result->style = true;
        result->active = true;
    }

    return result;
}

/** Get the wanted, drawn and keep of a tile as bits.
 *
 * @param t The tile
 * @return The bits
 */
static uint8_t box_terrain_tile_bits(
    const box_terrain_tile_t *t)
{
    return (uint8_t)((t->wanted ? 1 : 0) | (t->drawn ? 2 : 0) | (t->keep ? 4 : 0));
}

/** Update the tiles of a terrain: select the tiles the foci want, make the missing ones nearest
 * first within the budget, find which tiles draw, drop the others within the budget and keep
 * the TerrainTile and the position of each tile current.
 *
 * It goes over the tiles only when the Terrain, its position, the foci beyond their step, the
 * heights the queries read, the status of a texture of a tile or a material of the terrain
 * changed, or the update before changed a tile. The nodes are selected only for other foci.
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
    double local[3], foci[BOX_TERRAIN_FOCI][2], heights[BOX_TERRAIN_FOCI];
    double seen[BOX_TERRAIN_FOCI][3], around[BOX_TERRAIN_FOCI][3];
    double radius = isfinite(value->query_radius) && value->query_radius > 0.0f
        ? value->query_radius : 64.0;
    int32_t i, count, made = 0, drops = 0, missing = 0, budget;
    uint64_t revision = box_terrain_revision(world);
    box_terrain_settings(value, &s);
    bool reset = !tiles->made ||
        ecs_os_memcmp(&tiles->settings, &s, ECS_SIZEOF(box_terrain_settings_t)) ||
        radius != tiles->radius || value->query_only != tiles->query_only;
    box_terrain_position(world, e, local);
    count = box_terrain_foci(world, e, local, foci, heights);
    ecs_os_memset_n(seen, 0, double, 3 * BOX_TERRAIN_FOCI);
    for (i = 0; i < count; i ++) {
        seen[i][0] = foci[i][0];
        seen[i][1] = foci[i][1];
        seen[i][2] = heights[i];
    }

    bool walked = count != tiles->seen_count ||
        ecs_os_memcmp(seen, tiles->seen, ECS_SIZEOF(double) * 3 * BOX_TERRAIN_FOCI);
    bool moved = !tiles->positioned || local[0] != tiles->placed[0] ||
        local[1] != tiles->placed[1] || local[2] != tiles->placed[2];
    bool news = revision != tiles->revision;
    if (!reset && !walked && !moved && !news && !tiles->dirty && !tiles->style &&
        !tiles->active)
    {
        goto done;
    }

    ecs_os_memcpy_n(tiles->seen, seen, double, 3 * BOX_TERRAIN_FOCI);
    tiles->seen_count = count;
    tiles->revision = revision;
    tiles->radius = radius;
    tiles->query_only = value->query_only;
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
    tiles->selected = tiles->selected && !reset;
    if (!s.height || value->query_only) {
        box_terrain_drop_all(world, ctx, tiles);
        tiles->dirty = false;
        tiles->active = false;
        tiles->updates ++;
        goto status;
    }

    for (i = 0; i < count; i ++) {
        around[i][0] = foci[i][0];
        around[i][1] = foci[i][1];
        around[i][2] = box_terrain_clearance(world, e, value, &tiles->grids[i], revision,
            foci[i][0], foci[i][1], heights[i]);
    }

    bool refocus = count && box_terrain_refocus(tiles, around, count);
    if (!reset && !moved && !news && !refocus && !tiles->dirty && !tiles->style &&
        !tiles->active)
    {
        goto done;
    }

    bool restyle = false;
    if (tiles->style) {
        bool blended = ecs_has(world, e, TextureBlend);
        restyle = blended != tiles->blended;
        tiles->blended = blended;
        tiles->style = false;
    }

    bool changed = reset || moved || refocus || restyle;
    tiles->updates ++;
    tiles->dirty = false;
    tiles->positioned = true;
    ecs_os_memcpy_n(tiles->placed, local, double, 3);

    box_terrain_tile_t **list = ecs_vec_first(&tiles->list);
    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        list[i]->before = box_terrain_tile_bits(list[i]);
        list[i]->wanted = false;
    }

    int32_t waiting = 0;
    for (i = 0; i < ecs_vec_count(&tiles->list); i ++) {
        box_terrain_tile_t *t = list[i];
        if (!t->ready) {
            t->ready = box_terrain_texture_ready(world, t->texture) &&
                box_terrain_texture_ready(world, t->rough) &&
                box_terrain_texture_ready(world, t->masks) &&
                box_terrain_texture_ready(world, t->coarse);
            changed |= t->ready;
        }

        waiting += !t->ready && !box_terrain_tile_failed(world, t);
    }

    int32_t first = ecs_vec_count(&tiles->list);
    if (count) {
        if (refocus || !tiles->selected) {
            box_terrain_select(tiles, &s, tiles->foci, tiles->focus_count);
            tiles->selected = true;
        }

        for (i = 0; i < ecs_vec_count(&tiles->nodes); i ++) {
            const box_terrain_node_t *node = ecs_vec_get_t(&tiles->nodes, box_terrain_node_t, i);
            box_terrain_tile_t *t = box_terrain_find(tiles, node->level, node->ix, node->iz);
            if (!t && made < s.budget && waiting < s.budget * BOX_TERRAIN_WAITING) {
                t = box_terrain_tile_new(world, ctx, tiles, node);
                made ++;
                waiting ++;
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

    box_terrain_resolve(tiles, s.levels, count > 0);
    for (i = ecs_vec_count(&tiles->list) - 1; i >= 0; i --) {
        box_terrain_tile_t *t = *ecs_vec_get_t(&tiles->list, box_terrain_tile_t*, i);
        bool fresh = i >= first;
        if (!t->wanted && !t->keep && drops < s.budget * BOX_TERRAIN_DROPS) {
            box_terrain_tile_drop(world, ctx, tiles, i, true);
            drops ++;
            continue;
        }

        bool measured = !fresh && box_terrain_measure(world, t);
        if (fresh || refocus || restyle || measured) {
            box_terrain_tile_write(world, tiles, &s, e, t);
        }

        if (fresh || moved) {
            box_terrain_tile_place(tiles, &s, t, local);
        }

        changed |= measured || t->drawn != t->enabled ||
            box_terrain_tile_bits(t) != t->before;
        if (t->drawn != t->enabled) {
            t->enabled = t->drawn;
            tiles->version ++;
        }
    }

    tiles->active = changed || made || drops;
status:
    box_terrain_status(world, e, tiles, missing);
done:
    return;
}

/** Update the tiles of every terrain, in EcsPostUpdate. The system
 * is immediate to create the textures of the tiles, and updates the terrains after the
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
    ecs_vec_fini_t(NULL, &tiles->nodes, box_terrain_node_t);
    ecs_map_fini(&tiles->map);
    ecs_map_fini(&tiles->roots);
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
    ecs_map_fini(&ctx->textures);
    ecs_vec_fini_t(NULL, &ctx->free_slots, int32_t);
    ecs_vec_fini_t(NULL, &ctx->terrain_list, ecs_entity_t);
    ecs_os_free(ctx);
}

/** Mark the tiles of every terrain that got or lost its TextureBlend, so their next update
 * writes whether they draw with masks again.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnMaterial(
    ecs_iter_t *it)
{
    box_terrain_ctx_t *ctx = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_terrain_tiles_t *tiles = ecs_map_get_deref(&ctx->terrains, box_terrain_tiles_t,
            it->entities[i]);
        if (tiles) {
            tiles->style = true;
        }
    }
}

void box_terrain_counters(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *updates,
    int32_t *selects)
{
    const TerrainModule *m = ecs_singleton_get(world, TerrainModule);
    box_terrain_tiles_t *tiles = m && m->ctx
        ? ecs_map_get_deref(&m->ctx->terrains, box_terrain_tiles_t, terrain) : NULL;
    *updates = tiles ? tiles->updates : 0;
    *selects = tiles ? tiles->selects : 0;
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

int32_t box_terrain_tiles_version(
    const ecs_world_t *world,
    ecs_entity_t terrain)
{
    const TerrainModule *m = ecs_singleton_get(world, TerrainModule);
    box_terrain_tiles_t *tiles = m && m->ctx
        ? ecs_map_get_deref(&m->ctx->terrains, box_terrain_tiles_t, terrain) : NULL;

    return tiles ? tiles->version : 0;
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
        double morph = box_terrain_morph(s, level), fc = tiles->foci[0][2];
        out->first[level][0] = (int32_t)floor(tiles->foci[0][0] / size) - out->cells / 2;
        out->first[level][1] = (int32_t)floor(tiles->foci[0][1] / size) - out->cells / 2;
        out->morph[level][0] = (float)sqrt(fmax(morph * morph - fc * fc, 0.0));
        out->morph[level][1] = (float)sqrt(fmax(range * range - fc * fc, 0.0));
    }

    result = true;
done:
    return result;
}

void box_terrain_tiles_import(
    ecs_world_t *world)
{
    box_terrain_ctx_t *ctx = ecs_os_calloc_t(box_terrain_ctx_t);
    ctx->module = ecs_get_scope(world);
    ecs_map_init(&ctx->terrains, NULL);
    ecs_map_init(&ctx->textures, NULL);
    ecs_vec_init_t(NULL, &ctx->free_slots, int32_t, 0);
    ecs_vec_init_t(NULL, &ctx->terrain_list, ecs_entity_t, 0);

    ECS_COMPONENT_DEFINE(world, TerrainTile);
    ecs_add_pair(world, ecs_id(TerrainTile), EcsOnInstantiate, EcsDontInherit);
    ECS_COMPONENT_DEFINE(world, TerrainModule);
    ecs_singleton_set(world, TerrainModule, { .ctx = ctx });

    box_terrain_tile_shape(world);

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

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnTextureBlend" }),
        .query.terms = {
            { .id = ecs_id(TextureBlend) },
            { .id = ecs_id(Terrain), .src.id = EcsSelf, .inout = EcsInOutFilter }
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxTerrainOnMaterial,
        .ctx = ctx
    });
}
