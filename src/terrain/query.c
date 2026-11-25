/**
 * @file terrain/query.c
 * @brief The query tiles of a Terrain: CPU copies of its heights and masks around the foci and
 * where box_terrain_request asks for them, independent of the tiles it draws, and the height,
 * normal, mask and ray queries and script functions that read them.
 *
 * A query tile of level L is a TextureGraph of BOX_TERRAIN_QUERY_CELLS cells of query_spacing *
 * 8^L meters with a TextureReadback, made within query_radius * 8^L of a focus nearest first.
 * It is dropped once every focus is a tile further away and no request holds it.
 */

#include "terrain.h"
#include "../texture/texture.h"
#include "../world/world.h"
#include <math.h>
#include <float.h>

/** Frames a request holds the tiles it asked for by default (Terrain.query_hold) */
#define BOX_TERRAIN_QUERY_HOLD (8)

/** Query tiles a terrain wants per frame at most */
#define BOX_TERRAIN_QUERY_MAX (4096)

/** Tiles dropped per frame, per tile of the budget */
#define BOX_TERRAIN_QUERY_DROPS (4)

/** Steps a ray marches through a terrain at most */
#define BOX_TERRAIN_RAY_STEPS (8192)

/** Bisections that refine where a ray crosses the surface */
#define BOX_TERRAIN_RAY_REFINE (32)

/** Share of the spacing of the heights a ray marches per step along the ground */
#define BOX_TERRAIN_RAY_STEP (0.5)

/** The values of a Terrain the query tiles follow, with its defaults and limits applied */
typedef struct box_terrain_query_settings_t {
    ecs_entity_t height;    /**< Height node */
    ecs_entity_t masks;     /**< Masks node, 0 for none */
    double spacing;         /**< Meters between the texels of level 0 */
    double radius;          /**< Reach of level 0 around a focus */
    double extent;          /**< Half size, 0 unbounded */
    float min_height;       /**< Bottom of the heights */
    float max_height;       /**< Top of the heights */
    int32_t budget;         /**< Tiles made per frame */
    int32_t hold;           /**< Frames a request holds the tiles it asked for */
} box_terrain_query_settings_t;

/** A query tile a terrain wants this frame */
typedef struct box_terrain_query_node_t {
    int32_t level;          /**< Level */
    int32_t ix;             /**< Column */
    int32_t iz;             /**< Row */
    double distance;        /**< Distance to the nearest focus, negative when requested */
} box_terrain_query_node_t;

struct box_terrain_query_ctx_t;

/** The query tiles of a terrain */
typedef struct box_terrain_query_t {
    struct box_terrain_query_ctx_t *ctx; /**< The state of the queries */
    ecs_entity_t entity;    /**< The terrain entity */
    ecs_entity_t scene;     /**< Scene of the terrain when it was last updated */
    ecs_entity_t container; /**< Parent of the texture entities of the tiles */
    ecs_map_t map;          /**< Key of a tile to its box_terrain_query_tile_t */
    ecs_vec_t list;         /**< box_terrain_query_tile_t*, every tile */
    ecs_map_t requests;     /**< Key of a requested tile of level 0 to the frame of the request */
    box_terrain_query_settings_t settings; /**< Settings the tiles were made with */
    bool made;              /**< settings holds the settings of the tiles */
    double local[3];        /**< Position of the terrain of the last frame */
    bool placed;            /**< local holds a position */
    double foci[BOX_TERRAIN_FOCI][2]; /**< Foci the tiles were last selected for */
    int32_t focus_count;    /**< Foci in foci, -1 before the first selection */
    uint64_t expiry;        /**< First frame in which a request no longer holds */
    uint32_t readbacks;     /**< Serial of the CPU copies the status was computed with */
    int32_t missing;        /**< Tiles wanted that the budget did not make, as of the status */
    int32_t requested;      /**< Requests as of the status */
    bool dirty;             /**< A request asked for a tile no request held */
    bool active;            /**< The last update made or dropped a tile, so the next may too */
    int32_t updates;        /**< Updates that selected the tiles */
    int32_t statuses;       /**< Computations of the status */
} box_terrain_query_t;

/** State of the queries of box.terrain in a world */
typedef struct box_terrain_query_ctx_t {
    ecs_map_t terrains;     /**< Terrain entity to its box_terrain_query_t */
    uint64_t serial;        /**< Bumped when a terrain gets or loses a query tile or its
                             *   settings, scene or position change (box_terrain_revision) */
    ecs_vec_t nodes;        /**< box_terrain_query_node_t, the tiles a terrain wants */
    ecs_map_t seen;         /**< Key of a node to its index + 1 in nodes */
    ecs_vec_t terrain_list; /**< ecs_entity_t, the terrains to update this frame */
    ecs_vec_t expired;      /**< uint64_t, keys of requests that no longer hold */
} box_terrain_query_ctx_t;

/** Singleton that holds the state of the queries */
typedef struct TerrainQueryModule {
    box_terrain_query_ctx_t *ctx; /**< The state */
} TerrainQueryModule;

static ECS_COMPONENT_DECLARE(TerrainQueryModule);

/** Get the state of the queries of a world.
 *
 * @param world The world
 * @return The state, NULL before box.terrain is imported
 */
static box_terrain_query_ctx_t* box_terrain_query_ctx(
    const ecs_world_t *world)
{
    const TerrainQueryModule *m = ecs_id(TerrainQueryModule)
        ? ecs_singleton_get(world, TerrainQueryModule)
        : NULL;

    return m ? m->ctx : NULL;
}

/** Apply the defaults and limits of a Terrain to its query tiles.
 *
 * @param t The terrain
 * @param out Receives the settings
 */
static void box_terrain_query_settings(
    const Terrain *t,
    box_terrain_query_settings_t *out)
{
    ecs_os_memset_t(out, 0, box_terrain_query_settings_t);
    out->height = t->height;
    out->masks = t->masks;
    out->spacing = isfinite(t->query_spacing) && t->query_spacing > 0.0f
        ? t->query_spacing : 0.5;
    out->radius = isfinite(t->query_radius) && t->query_radius > 0.0f ? t->query_radius : 64.0;
    out->extent = isfinite(t->extent) && t->extent > 0.0f ? t->extent : 0.0;
    out->min_height = t->min_height;
    out->max_height = t->max_height;
    if (!(isfinite(t->min_height) && isfinite(t->max_height) && t->max_height > t->min_height)) {
        out->min_height = -256.0f;
        out->max_height = 1024.0f;
    }

    out->budget = t->budget > 0 ? t->budget : 4;
    out->hold = t->query_hold > 0 ? t->query_hold : BOX_TERRAIN_QUERY_HOLD;
}

/** Get the meters between the texels of a level.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_query_spacing(
    const box_terrain_query_settings_t *s,
    int32_t level)
{
    return s->spacing * pow((double)BOX_TERRAIN_QUERY_SCALE, (double)level);
}

/** Get the edge of a tile of a level.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_query_edge(
    const box_terrain_query_settings_t *s,
    int32_t level)
{
    return box_terrain_query_spacing(s, level) * BOX_TERRAIN_QUERY_CELLS;
}

/** Get the reach of a level around a focus.
 *
 * @param s The settings
 * @param level The level
 * @return Meters
 */
static double box_terrain_query_reach(
    const box_terrain_query_settings_t *s,
    int32_t level)
{
    return s->radius * pow((double)BOX_TERRAIN_QUERY_SCALE, (double)level);
}

/** Get the key of a query tile.
 *
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return The key
 */
static uint64_t box_terrain_query_key(
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    return ((uint64_t)(level + 1) << 58) | ((uint64_t)((uint32_t)ix & 0x1FFFFFFFu) << 29) |
        (uint64_t)((uint32_t)iz & 0x1FFFFFFFu);
}

/** Get the column and row of a key of level 0.
 *
 * @param key The key
 * @param ix Receives the column
 * @param iz Receives the row
 */
static void box_terrain_query_unkey(
    uint64_t key,
    int32_t *ix,
    int32_t *iz)
{
    uint32_t x = (uint32_t)(key >> 29) & 0x1FFFFFFFu, z = (uint32_t)key & 0x1FFFFFFFu;
    *ix = (int32_t)(x & 0x10000000u ? x | 0xE0000000u : x);
    *iz = (int32_t)(z & 0x10000000u ? z | 0xE0000000u : z);
}

/** Get the column or row of the tile of a level a coordinate lies in.
 *
 * @param v The coordinate
 * @param edge Edge of a tile
 * @return The index
 */
static int32_t box_terrain_query_index(
    double v,
    double edge)
{
    double i = floor(v / edge);

    return i < -268435456.0 ? -268435456 : i > 268435455.0 ? 268435455 : (int32_t)i;
}

/** Get the distance from a point to the rectangle of a tile.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @param x X of the point
 * @param z Z of the point
 * @return Meters
 */
static double box_terrain_query_distance(
    const box_terrain_query_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz,
    double x,
    double z)
{
    double edge = box_terrain_query_edge(s, level);
    double x0 = (double)ix * edge, z0 = (double)iz * edge;
    double dx = fmax(fmax(x0 - x, x - (x0 + edge)), 0.0);
    double dz = fmax(fmax(z0 - z, z - (z0 + edge)), 0.0);

    return sqrt(dx * dx + dz * dz);
}

/** Check whether a tile overlaps the extent of a bounded terrain.
 *
 * @param s The settings
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return Whether it does, always for an unbounded terrain
 */
static bool box_terrain_query_inside(
    const box_terrain_query_settings_t *s,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    double edge = box_terrain_query_edge(s, level);
    double x0 = (double)ix * edge, z0 = (double)iz * edge;

    return s->extent <= 0.0 || (x0 < s->extent && x0 + edge > -s->extent &&
        z0 < s->extent && z0 + edge > -s->extent);
}

/** Get the query tiles of a terrain, made on first use without a container.
 *
 * @param ctx The state
 * @param e The terrain
 * @return The tiles
 */
static box_terrain_query_t* box_terrain_query_ensure(
    box_terrain_query_ctx_t *ctx,
    ecs_entity_t e)
{
    box_terrain_query_t *result = ecs_map_get_deref(&ctx->terrains, box_terrain_query_t, e);
    if (!result) {
        result = ecs_os_calloc_t(box_terrain_query_t);
        result->ctx = ctx;
        result->entity = e;
        result->focus_count = -1;
        result->active = true;
        ecs_map_init(&result->map, NULL);
        ecs_map_init(&result->requests, NULL);
        ecs_vec_init_t(NULL, &result->list, box_terrain_query_tile_t*, 0);
        ecs_map_insert_ptr(&ctx->terrains, e, result);
    }

    return result;
}

/** Get the query tiles of a terrain that has settings.
 *
 * @param world The world
 * @param e The terrain
 * @return The tiles, NULL when the terrain has none
 */
static const box_terrain_query_t* box_terrain_query_get(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    const box_terrain_query_t *result = ctx && e
        ? ecs_map_get_deref(&ctx->terrains, box_terrain_query_t, e)
        : NULL;

    return result && result->made && result->settings.height ? result : NULL;
}

/** Find the query tile of a node.
 *
 * @param q The tiles of the terrain
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @return The tile, NULL when the node has none
 */
static box_terrain_query_tile_t* box_terrain_query_find(
    const box_terrain_query_t *q,
    int32_t level,
    int32_t ix,
    int32_t iz)
{
    return ecs_map_get_deref(&q->map, box_terrain_query_tile_t,
        box_terrain_query_key(level, ix, iz));
}

/** Make the texture entity of a window of a query tile: a TextureGraph of a node over the
 * window and a TextureReadback of it.
 *
 * @param world The world
 * @param parent The container
 * @param node The node
 * @param t The tile
 * @param format Format of the image
 * @return The texture entity
 */
static ecs_entity_t box_terrain_query_texture(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t node,
    const box_terrain_query_tile_t *t,
    TextureGraphFormat format)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, result, TextureGraph, {
        .output = node,
        .width = t->texels,
        .height = t->texels,
        .domain = DomainWorld,
        .window = {
            .x = t->window[0],
            .z = t->window[1],
            .width = (float)t->window[2],
            .depth = (float)t->window[2]
        },
        .format = format,
        .priority = (float)-t->distance
    });
    ecs_set(world, result, TextureReadback, { .level = 0, .keep = true });

    return result;
}

/** Make the query tile of a node.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param node The node
 * @return The tile
 */
static box_terrain_query_tile_t* box_terrain_query_tile_new(
    ecs_world_t *world,
    box_terrain_query_t *q,
    const box_terrain_query_node_t *node)
{
    const box_terrain_query_settings_t *s = &q->settings;
    box_terrain_query_tile_t *t = ecs_os_calloc_t(box_terrain_query_tile_t);
    double cell = box_terrain_query_spacing(s, node->level);
    double edge = box_terrain_query_edge(s, node->level);
    t->level = node->level;
    t->ix = node->ix;
    t->iz = node->iz;
    t->distance = node->distance;
    t->texels = BOX_TERRAIN_QUERY_CELLS + 1 + 2 * BOX_TERRAIN_QUERY_BORDER;
    t->window[0] = (double)node->ix * edge - BOX_TERRAIN_QUERY_BORDER * cell;
    t->window[1] = (double)node->iz * edge - BOX_TERRAIN_QUERY_BORDER * cell;
    t->window[2] = cell * (double)(t->texels - 1);
    t->heights = box_terrain_query_texture(world, q->container, s->height, t, GraphFloat);
    t->masks = s->masks
        ? box_terrain_query_texture(world, q->container, s->masks, t, GraphFloat4)
        : 0;
    ecs_map_insert_ptr(&q->map, box_terrain_query_key(t->level, t->ix, t->iz), t);
    *ecs_vec_append_t(NULL, &q->list, box_terrain_query_tile_t*) = t;
    q->ctx->serial ++;

    return t;
}

/** Delete a query tile.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param index Index of the tile in the list
 * @param entities Whether to delete the texture entities, false when the container goes
 */
static void box_terrain_query_tile_drop(
    ecs_world_t *world,
    box_terrain_query_t *q,
    int32_t index,
    bool entities)
{
    box_terrain_query_tile_t *t = *ecs_vec_get_t(&q->list, box_terrain_query_tile_t*, index);
    if (entities && t->heights && ecs_is_alive(world, t->heights)) {
        ecs_delete(world, t->heights);
    }

    if (entities && t->masks && ecs_is_alive(world, t->masks)) {
        ecs_delete(world, t->masks);
    }

    ecs_map_remove(&q->map, box_terrain_query_key(t->level, t->ix, t->iz));
    ecs_vec_remove_t(&q->list, box_terrain_query_tile_t*, index);
    ecs_os_free(t);
    q->ctx->serial ++;
    q->active = true;
}

/** Delete every query tile of a terrain.
 *
 * @param world The world
 * @param q The tiles
 * @param entities Whether to delete the texture entities
 */
static void box_terrain_query_drop_all(
    ecs_world_t *world,
    box_terrain_query_t *q,
    bool entities)
{
    while (ecs_vec_count(&q->list)) {
        box_terrain_query_tile_drop(world, q, ecs_vec_count(&q->list) - 1, entities);
    }
}

/** Get the CPU copy of a window of a query tile.
 *
 * @param world The world
 * @param t The tile
 * @param masks The masks window, else the heights
 * @param out Receives the copy
 * @return Whether a copy of at least 2 x 2 texels is ready
 */
static bool box_terrain_query_copy(
    const ecs_world_t *world,
    const box_terrain_query_tile_t *t,
    bool masks,
    box_texture_copy_t *out)
{
    ecs_entity_t texture = masks ? t->masks : t->heights;
    bool result = texture && box_texture_readback(world, texture, out) &&
        out->width > 1 && out->height > 1;

    return result;
}

/** Check whether a query tile has its heights, and its masks when the terrain has them.
 *
 * @param world The world
 * @param t The tile
 * @return Whether it is resident
 */
static bool box_terrain_query_resident(
    const ecs_world_t *world,
    const box_terrain_query_tile_t *t)
{
    box_texture_copy_t copy;

    return box_terrain_query_copy(world, t, false, &copy) &&
        (!t->masks || box_terrain_query_copy(world, t, true, &copy));
}

/** Sample the heights or masks of a level of a terrain bilinearly at a point.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param level The level
 * @param x X in the frame of the terrain
 * @param z Z in the frame of the terrain
 * @param masks Sample the masks, else the heights
 * @param out Receives the value
 * @return Whether the level is resident at the point
 */
static bool box_terrain_query_sample_level(
    const ecs_world_t *world,
    const box_terrain_query_t *q,
    int32_t level,
    double x,
    double z,
    bool masks,
    float out[4])
{
    const box_terrain_query_settings_t *s = &q->settings;
    double edge = box_terrain_query_edge(s, level);
    const box_terrain_query_tile_t *t = box_terrain_query_find(q, level,
        box_terrain_query_index(x, edge), box_terrain_query_index(z, edge));
    box_texture_copy_t copy;
    float v[4][4];
    int32_t c;
    bool result = t && box_terrain_query_copy(world, t, masks, &copy);
    if (!result) {
        goto done;
    }

    double fx = (x - t->window[0]) / t->window[2] * (double)(copy.width - 1);
    double fz = (z - t->window[1]) / t->window[2] * (double)(copy.height - 1);
    double ix = floor(fx), iz = floor(fz);
    ix = ix < 0.0 ? 0.0 : ix > (double)(copy.width - 2) ? (double)(copy.width - 2) : ix;
    iz = iz < 0.0 ? 0.0 : iz > (double)(copy.height - 2) ? (double)(copy.height - 2) : iz;
    float tx = (float)fmin(fmax(fx - ix, 0.0), 1.0), tz = (float)fmin(fmax(fz - iz, 0.0), 1.0);
    box_texture_copy_texel(&copy, (int32_t)ix, (int32_t)iz, v[0]);
    box_texture_copy_texel(&copy, (int32_t)ix + 1, (int32_t)iz, v[1]);
    box_texture_copy_texel(&copy, (int32_t)ix, (int32_t)iz + 1, v[2]);
    box_texture_copy_texel(&copy, (int32_t)ix + 1, (int32_t)iz + 1, v[3]);
    for (c = 0; c < 4; c ++) {
        float a = v[0][c] + (v[1][c] - v[0][c]) * tx;
        float b = v[2][c] + (v[3][c] - v[2][c]) * tx;
        out[c] = a + (b - a) * tz;
    }

done:
    return result;
}

/** Check whether a point lies within the extent of a bounded terrain.
 *
 * @param s The settings
 * @param x X in the frame of the terrain
 * @param z Z in the frame of the terrain
 * @return Whether it does, always for an unbounded terrain
 */
static bool box_terrain_query_within(
    const box_terrain_query_settings_t *s,
    double x,
    double z)
{
    double e = s->extent * (1.0 + 1e-9);

    return s->extent <= 0.0 || (fabs(x) <= e && fabs(z) <= e);
}

/** Sample the heights or masks of a terrain at a point from the finest level resident there.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param x X in the frame of the terrain
 * @param z Z in the frame of the terrain
 * @param masks Sample the masks, else the heights
 * @param out Receives the value, zero when not known
 * @param level Receives the level that answered, may be NULL
 * @return Whether a level is resident at the point
 */
static bool box_terrain_query_sample(
    const ecs_world_t *world,
    const box_terrain_query_t *q,
    double x,
    double z,
    bool masks,
    float out[4],
    int32_t *level)
{
    int32_t l;
    bool result = false;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (!isfinite(x) || !isfinite(z) || !box_terrain_query_within(&q->settings, x, z)) {
        goto done;
    }

    for (l = 0; l < BOX_TERRAIN_QUERY_LEVELS && !result; l ++) {
        result = box_terrain_query_sample_level(world, q, l, x, z, masks, out);
        if (result && level) {
            *level = l;
        }
    }

    if (!result) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
    }

done:
    return result;
}

/** Get the normal of a terrain at a point from the differences of its heights.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param x X in the frame of the terrain
 * @param z Z in the frame of the terrain
 * @param out Receives the unit normal, up when not known
 * @return Whether the heights around the point are resident
 */
static bool box_terrain_query_normal(
    const ecs_world_t *world,
    const box_terrain_query_t *q,
    double x,
    double z,
    vec3 out)
{
    float c[4], h[4][4];
    int32_t level = 0;
    bool result = box_terrain_query_sample(world, q, x, z, false, c, &level);
    out[0] = 0.0f;
    out[1] = 1.0f;
    out[2] = 0.0f;
    if (result) {
        double d = box_terrain_query_spacing(&q->settings, level);
        double e = q->settings.extent;
        double x0 = x - d, x1 = x + d, z0 = z - d, z1 = z + d;
        if (e > 0.0) {
            x0 = fmax(x0, -e);
            x1 = fmin(x1, e);
            z0 = fmax(z0, -e);
            z1 = fmin(z1, e);
        }

        result = box_terrain_query_sample(world, q, x0, z, false, h[0], NULL) &&
            box_terrain_query_sample(world, q, x1, z, false, h[1], NULL) &&
            box_terrain_query_sample(world, q, x, z0, false, h[2], NULL) &&
            box_terrain_query_sample(world, q, x, z1, false, h[3], NULL) &&
            x1 > x0 && z1 > z0;
        if (result) {
            out[0] = -(float)((double)(h[1][0] - h[0][0]) / (x1 - x0));
            out[1] = 1.0f;
            out[2] = -(float)((double)(h[3][0] - h[2][0]) / (z1 - z0));
            glm_vec3_normalize(out);
        }
    }

    return result;
}

bool box_terrain_height(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double x,
    double z,
    float *out)
{
    const box_terrain_query_t *q = box_terrain_query_get(world, terrain);
    float v[4] = { 0, 0, 0, 0 };
    bool result = q && box_terrain_query_sample(world, q, x, z, false, v, NULL);
    if (out) {
        *out = result ? v[0] : 0.0f;
    }

    return result;
}

bool box_terrain_normal(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double x,
    double z,
    vec3 out)
{
    const box_terrain_query_t *q = box_terrain_query_get(world, terrain);
    bool result = false;
    if (q) {
        result = box_terrain_query_normal(world, q, x, z, out);
    } else {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
    }

    return result;
}

bool box_terrain_slope(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double x,
    double z,
    float *out)
{
    vec3 n;
    bool result = box_terrain_normal(world, terrain, x, z, n);
    if (out) {
        *out = result ? acosf(glm_clamp(n[1], -1.0f, 1.0f)) : 0.0f;
    }

    return result;
}

bool box_terrain_mask(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double x,
    double z,
    vec4 out)
{
    const box_terrain_query_t *q = box_terrain_query_get(world, terrain);
    float v[4] = { 0, 0, 0, 0 };
    bool result = q && q->settings.masks &&
        box_terrain_query_sample(world, q, x, z, true, v, NULL);
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    out[3] = v[3];

    return result;
}

/** Clip a rectangle to the extent of a bounded terrain.
 *
 * @param s The settings
 * @param r The rectangle: min x, min z, max x, max z, clipped in place
 * @return Whether anything of it is left
 */
static bool box_terrain_query_clip(
    const box_terrain_query_settings_t *s,
    double r[4])
{
    if (r[2] < r[0]) {
        double v = r[0];
        r[0] = r[2];
        r[2] = v;
    }

    if (r[3] < r[1]) {
        double v = r[1];
        r[1] = r[3];
        r[3] = v;
    }

    if (s->extent > 0.0) {
        r[0] = fmax(r[0], -s->extent);
        r[1] = fmax(r[1], -s->extent);
        r[2] = fmin(r[2], s->extent);
        r[3] = fmin(r[3], s->extent);
    }

    return r[0] <= r[2] && r[1] <= r[3];
}

bool box_terrain_resident(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double x0,
    double z0,
    double x1,
    double z1)
{
    const box_terrain_query_t *q = box_terrain_query_get(world, terrain);
    double r[4] = { x0, z0, x1, z1 };
    bool result = q != NULL && isfinite(x0 + z0 + x1 + z1);
    int32_t ix, iz;
    if (!result || !box_terrain_query_clip(&q->settings, r)) {
        goto done;
    }

    double edge = box_terrain_query_edge(&q->settings, 0);
    int32_t ix0 = box_terrain_query_index(r[0], edge), ix1 = box_terrain_query_index(r[2], edge);
    int32_t iz0 = box_terrain_query_index(r[1], edge), iz1 = box_terrain_query_index(r[3], edge);
    if ((int64_t)(ix1 - ix0 + 1) * (iz1 - iz0 + 1) > BOX_TERRAIN_QUERY_MAX) {
        result = false;
        goto done;
    }

    for (iz = iz0; iz <= iz1 && result; iz ++) {
        for (ix = ix0; ix <= ix1 && result; ix ++) {
            const box_terrain_query_tile_t *t = box_terrain_query_find(q, 0, ix, iz);
            result = t && box_terrain_query_resident(world, t);
        }
    }

done:
    return result;
}

void box_terrain_request(
    ecs_world_t *world,
    ecs_entity_t terrain,
    double x0,
    double z0,
    double x1,
    double z1)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    const Terrain *value = ctx && terrain ? ecs_get(world, terrain, Terrain) : NULL;
    box_terrain_query_settings_t s;
    double r[4] = { x0, z0, x1, z1 };
    int32_t ix, iz;
    if (!value || !isfinite(x0 + z0 + x1 + z1)) {
        goto done;
    }

    box_terrain_query_settings(value, &s);
    if (!box_terrain_query_clip(&s, r)) {
        goto done;
    }

    box_terrain_query_t *q = box_terrain_query_ensure(ctx, terrain);
    double edge = box_terrain_query_edge(&s, 0);
    int32_t ix0 = box_terrain_query_index(r[0], edge), ix1 = box_terrain_query_index(r[2], edge);
    int32_t iz0 = box_terrain_query_index(r[1], edge), iz1 = box_terrain_query_index(r[3], edge);
    uint64_t frame = (uint64_t)ecs_get_world_info(world)->frame_count_total;
    int32_t held = ecs_map_count(&q->requests);
    if ((int64_t)(ix1 - ix0 + 1) * (iz1 - iz0 + 1) > BOX_TERRAIN_QUERY_MAX) {
        goto done;
    }

    for (iz = iz0; iz <= iz1; iz ++) {
        for (ix = ix0; ix <= ix1; ix ++) {
            *ecs_map_ensure(&q->requests, box_terrain_query_key(0, ix, iz)) = frame;
        }
    }

    q->dirty = q->dirty || ecs_map_count(&q->requests) != held;

done:
    return;
}

uint64_t box_terrain_revision(
    const ecs_world_t *world)
{
    const box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    const box_textures_t *textures = box_textures_get(world);
    const WorldOriginStatus *origin = ecs_id(WorldOriginStatus)
        ? ecs_singleton_get(world, WorldOriginStatus) : NULL;

    return (ctx ? ctx->serial : 0) + (textures ? textures->readback_serial : 0) +
        (origin ? (uint64_t)origin->shifts : 0);
}

void box_terrain_query_counters(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *updates,
    int32_t *statuses)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    const box_terrain_query_t *q = ctx
        ? ecs_map_get_deref(&ctx->terrains, box_terrain_query_t, terrain)
        : NULL;
    *updates = q ? q->updates : 0;
    *statuses = q ? q->statuses : 0;
}

void box_terrain_origin(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    double out[3])
{
    box_terrain_local(world, terrain, out);
}

bool box_terrain_ground(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    float x,
    float z,
    float *y,
    vec3 normal)
{
    const box_terrain_query_t *q = box_terrain_query_get(world, terrain);
    double o[3];
    float v[4] = { 0, 0, 0, 0 };
    bool result = false;
    if (q) {
        box_terrain_local(world, terrain, o);
        result = box_terrain_query_sample(world, q, (double)x - o[0], (double)z - o[2], false,
            v, NULL);
        if (y) {
            *y = result ? (float)(o[1] + (double)v[0]) : 0.0f;
        }

        if (normal) {
            box_terrain_query_normal(world, q, (double)x - o[0], (double)z - o[2], normal);
        }
    } else {
        if (y) {
            *y = 0.0f;
        }

        if (normal) {
            normal[0] = 0.0f;
            normal[1] = 1.0f;
            normal[2] = 0.0f;
        }
    }

    return result;
}

/** Clip a ray to a box: the part of the ray inside it.
 *
 * @param p Origin of the ray
 * @param d Unit direction
 * @param min Minimum corner
 * @param max Maximum corner
 * @param t0 Start of the part, written in place
 * @param t1 End of the part, written in place
 * @return Whether a part is left
 */
static bool box_terrain_ray_clip(
    const double p[3],
    const double d[3],
    const double min[3],
    const double max[3],
    double *t0,
    double *t1)
{
    int32_t i;
    bool result = true;
    for (i = 0; i < 3 && result; i ++) {
        if (fabs(d[i]) < 1e-12) {
            result = p[i] >= min[i] && p[i] <= max[i];
        } else {
            double a = (min[i] - p[i]) / d[i], b = (max[i] - p[i]) / d[i];
            if (a > b) {
                double v = a;
                a = b;
                b = v;
            }

            *t0 = fmax(*t0, a);
            *t1 = fmin(*t1, b);
        }
    }

    return result && *t0 <= *t1;
}

/** March a ray through the resident heights of a terrain.
 *
 * The ray is clipped to the box of the resident query tiles between the lowest and highest
 * height, then steps half a spacing of the level below it along the ground until it goes from
 * above the surface to below it, where bisection finds the crossing.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param origin Origin in the local frame of the world
 * @param dir Unit direction
 * @param reach Longest distance that counts
 * @param out Receives the distance of the hit
 * @return Whether the ray hit the terrain
 */
static bool box_terrain_query_ray(
    const ecs_world_t *world,
    const box_terrain_query_t *q,
    const vec3 origin,
    const vec3 dir,
    double reach,
    float *out)
{
    const box_terrain_query_settings_t *s = &q->settings;
    box_terrain_query_tile_t *const *tiles = ecs_vec_first(&q->list);
    double o[3], p[3], d[3], min[3], max[3], t0 = 0.0, t1 = reach;
    double flat, t, prev_t = 0.0, prev_diff = 0.0;
    int32_t i, count = ecs_vec_count(&q->list);
    bool result = false, any = false, prev = false;
    box_terrain_local(world, q->entity, o);
    for (i = 0; i < 3; i ++) {
        p[i] = (double)origin[i] - o[i];
        d[i] = (double)dir[i];
    }

    min[0] = min[2] = DBL_MAX;
    max[0] = max[2] = -DBL_MAX;
    min[1] = (double)s->min_height;
    max[1] = (double)s->max_height;
    for (i = 0; i < count; i ++) {
        const box_terrain_query_tile_t *tile = tiles[i];
        double edge = box_terrain_query_edge(s, tile->level);
        box_texture_copy_t copy;
        if (!box_terrain_query_copy(world, tile, false, &copy)) {
            continue;
        }

        min[0] = fmin(min[0], (double)tile->ix * edge);
        min[2] = fmin(min[2], (double)tile->iz * edge);
        max[0] = fmax(max[0], (double)(tile->ix + 1) * edge);
        max[2] = fmax(max[2], (double)(tile->iz + 1) * edge);
        any = true;
    }

    if (!any || !box_terrain_ray_clip(p, d, min, max, &t0, &t1)) {
        goto done;
    }

    flat = sqrt(d[0] * d[0] + d[2] * d[2]);
    t = t0;
    for (i = 0; i < BOX_TERRAIN_RAY_STEPS && t <= t1; i ++) {
        float h[4];
        int32_t level = BOX_TERRAIN_QUERY_LEVELS - 1;
        double x = p[0] + d[0] * t, y = p[1] + d[1] * t, z = p[2] + d[2] * t;
        bool known = box_terrain_query_sample(world, q, x, z, false, h, &level);
        double diff = y - (double)h[0];
        if (known && prev && prev_diff > 0.0 && diff <= 0.0) {
            double a = prev_t, b = t;
            int32_t k;
            for (k = 0; k < BOX_TERRAIN_RAY_REFINE; k ++) {
                double m = (a + b) * 0.5, hy;
                float hm[4];
                if (!box_terrain_query_sample(world, q, p[0] + d[0] * m, p[2] + d[2] * m,
                    false, hm, NULL))
                {
                    break;
                }

                hy = p[1] + d[1] * m - (double)hm[0];
                if (hy > 0.0) {
                    a = m;
                } else {
                    b = m;
                }
            }

            *out = (float)b;
            result = true;
            break;
        }

        prev = known;
        prev_diff = diff;
        prev_t = t;
        if (t >= t1) {
            break;
        }

        double step = box_terrain_query_spacing(s, level) * BOX_TERRAIN_RAY_STEP;
        step = flat > 1e-3 ? step / flat : step * 1e3;
        t = fmin(t + fmax(step, 1e-4), t1);
    }

done:
    return result;
}

bool box_terrain_ray(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 origin,
    const vec3 dir,
    float max_distance,
    ecs_entity_t *terrain,
    float *distance)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    float len = glm_vec3_norm(ECS_CONST_CAST(float*, dir)), best = 0.0f;
    double reach = max_distance > 0.0f ? (double)max_distance : 1e7;
    ecs_entity_t hit = 0;
    vec3 unit;
    bool result = false;
    if (!ctx || !(len > 0.0f) || !isfinite(len)) {
        goto done;
    }

    glm_vec3_scale(ECS_CONST_CAST(float*, dir), 1.0f / len, unit);
    ecs_map_iter_t it = ecs_map_iter(&ctx->terrains);
    while (ecs_map_next(&it)) {
        const box_terrain_query_t *q = ecs_map_ptr(&it);
        float t = 0.0f;
        if (!q->made || !q->settings.height || q->scene != scene ||
            !ecs_is_alive(world, q->entity))
        {
            continue;
        }

        if (box_terrain_query_ray(world, q, origin, unit, reach, &t)) {
            best = t;
            reach = t;
            hit = q->entity;
            result = true;
        }
    }

done:
    if (terrain) {
        *terrain = hit;
    }

    if (distance) {
        *distance = best;
    }

    return result;
}

int32_t box_terrain_scene(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t *out,
    int32_t max)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    int32_t result = 0;
    if (!ctx) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&ctx->terrains);
    while (ecs_map_next(&it) && result < max) {
        const box_terrain_query_t *q = ecs_map_ptr(&it);
        if (q->made && q->settings.height && q->scene == scene && ecs_vec_count(&q->list)) {
            out[result ++] = q->entity;
        }
    }

done:
    return result;
}

box_terrain_query_tile_t* const* box_terrain_query_tiles(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *count)
{
    box_terrain_query_ctx_t *ctx = box_terrain_query_ctx(world);
    box_terrain_query_t *q = ctx
        ? ecs_map_get_deref(&ctx->terrains, box_terrain_query_t, terrain)
        : NULL;
    *count = q ? ecs_vec_count(&q->list) : 0;

    return q && *count ? ecs_vec_first(&q->list) : NULL;
}

/** Add a node a terrain wants, once: a node added again keeps the smaller distance.
 *
 * @param ctx The state
 * @param level The level
 * @param ix The column
 * @param iz The row
 * @param distance Distance to the nearest focus, negative when requested
 */
static void box_terrain_query_want(
    box_terrain_query_ctx_t *ctx,
    int32_t level,
    int32_t ix,
    int32_t iz,
    double distance)
{
    uint64_t key = box_terrain_query_key(level, ix, iz);
    uint64_t *index = ecs_map_get(&ctx->seen, key);
    if (index) {
        box_terrain_query_node_t *n = ecs_vec_get_t(&ctx->nodes, box_terrain_query_node_t,
            (int32_t)(*index - 1));
        n->distance = fmin(n->distance, distance);
    } else if (ecs_vec_count(&ctx->nodes) < BOX_TERRAIN_QUERY_MAX) {
        *ecs_vec_append_t(NULL, &ctx->nodes, box_terrain_query_node_t) =
            (box_terrain_query_node_t){ level, ix, iz, distance };
        ecs_map_insert(&ctx->seen, key, (uint64_t)ecs_vec_count(&ctx->nodes));
    }
}

/** Order nodes by distance, requested ones first.
 *
 * @param a The first node
 * @param b The second node
 * @return Negative when a goes first
 */
static int box_terrain_query_node_compare(
    const void *a,
    const void *b)
{
    double da = ((const box_terrain_query_node_t*)a)->distance;
    double db = ((const box_terrain_query_node_t*)b)->distance;

    return (da > db) - (da < db);
}

/** Collect the nodes the foci and the requests of a terrain want, forget the requests
 * that no longer hold and find the first frame in which one of the others no longer does.
 *
 * @param world The world
 * @param ctx The state
 * @param q The tiles of the terrain
 * @param foci The foci in the frame of the terrain
 * @param count Number of foci
 */
static void box_terrain_query_select(
    const ecs_world_t *world,
    box_terrain_query_ctx_t *ctx,
    box_terrain_query_t *q,
    const double foci[][2],
    int32_t count)
{
    const box_terrain_query_settings_t *s = &q->settings;
    uint64_t frame = (uint64_t)ecs_get_world_info(world)->frame_count_total;
    int32_t level, f, ix, iz, i;
    ecs_vec_clear(&ctx->nodes);
    ecs_vec_clear(&ctx->expired);
    ecs_map_clear(&ctx->seen);
    for (level = 0; level < BOX_TERRAIN_QUERY_LEVELS; level ++) {
        double edge = box_terrain_query_edge(s, level);
        double reach = box_terrain_query_reach(s, level);
        for (f = 0; f < count; f ++) {
            int32_t ix0 = box_terrain_query_index(foci[f][0] - reach, edge);
            int32_t ix1 = box_terrain_query_index(foci[f][0] + reach, edge);
            int32_t iz0 = box_terrain_query_index(foci[f][1] - reach, edge);
            int32_t iz1 = box_terrain_query_index(foci[f][1] + reach, edge);
            for (iz = iz0; iz <= iz1; iz ++) {
                for (ix = ix0; ix <= ix1; ix ++) {
                    double dist = box_terrain_query_distance(s, level, ix, iz, foci[f][0],
                        foci[f][1]);
                    if (dist <= reach && box_terrain_query_inside(s, level, ix, iz)) {
                        box_terrain_query_want(ctx, level, ix, iz, dist);
                    }
                }
            }
        }
    }

    q->expiry = UINT64_MAX;
    ecs_map_iter_t it = ecs_map_iter(&q->requests);
    while (ecs_map_next(&it)) {
        uint64_t key = ecs_map_key(&it), when = ecs_map_value(&it);
        if (frame > when + (uint64_t)s->hold) {
            *ecs_vec_append_t(NULL, &ctx->expired, uint64_t) = key;
            continue;
        }

        q->expiry = when + (uint64_t)s->hold + 1 < q->expiry
            ? when + (uint64_t)s->hold + 1 : q->expiry;

        box_terrain_query_unkey(key, &ix, &iz);
        if (box_terrain_query_inside(s, 0, ix, iz)) {
            box_terrain_query_want(ctx, 0, ix, iz, -1.0);
        }
    }

    for (i = 0; i < ecs_vec_count(&ctx->expired); i ++) {
        ecs_map_remove(&q->requests, *ecs_vec_get_t(&ctx->expired, uint64_t, i));
    }

    if (ecs_vec_count(&ctx->nodes) > 1) {
        qsort(ecs_vec_first(&ctx->nodes), (size_t)ecs_vec_count(&ctx->nodes),
            sizeof(box_terrain_query_node_t), box_terrain_query_node_compare);
    }
}

/** Check whether a tile is close enough to a focus to keep: within the reach of its level plus
 * the edge of a tile, so a focus that moves back and forth does not make and drop it again.
 *
 * @param q The tiles of the terrain
 * @param t The tile
 * @param foci The foci in the frame of the terrain
 * @param count Number of foci
 * @return Whether to keep it
 */
static bool box_terrain_query_near(
    const box_terrain_query_t *q,
    const box_terrain_query_tile_t *t,
    const double foci[][2],
    int32_t count)
{
    const box_terrain_query_settings_t *s = &q->settings;
    double keep = box_terrain_query_reach(s, t->level) + box_terrain_query_edge(s, t->level);
    int32_t f;
    bool result = count == 0;
    for (f = 0; f < count && !result; f ++) {
        result = box_terrain_query_distance(s, t->level, t->ix, t->iz, foci[f][0],
            foci[f][1]) <= keep;
    }

    return result;
}

/** Write the TerrainQueryStatus of a terrain when it changed.
 *
 * @param world The world
 * @param q The tiles of the terrain
 * @param missing Tiles wanted that the budget did not make yet
 */
static void box_terrain_query_status(
    ecs_world_t *world,
    const box_terrain_query_t *q,
    int32_t missing)
{
    TerrainQueryStatus status = {0};
    int32_t i;
    for (i = 0; i < ecs_vec_count(&q->list); i ++) {
        const box_terrain_query_tile_t *t =
            *ecs_vec_get_t(&q->list, box_terrain_query_tile_t*, i);
        box_texture_copy_t copy;
        bool resident = box_terrain_query_resident(world, t);
        status.resident += resident;
        status.pending += t->wanted && !resident;
        if (box_terrain_query_copy(world, t, false, &copy)) {
            status.cpu_bytes += (int64_t)copy.width * copy.height *
                box_texture_copy_texel_size(copy.format);
        }

        if (t->masks && box_terrain_query_copy(world, t, true, &copy)) {
            status.cpu_bytes += (int64_t)copy.width * copy.height *
                box_texture_copy_texel_size(copy.format);
        }
    }

    status.tiles = ecs_vec_count(&q->list);
    status.pending += missing;
    status.requested = ecs_map_count(&q->requests);
    const TerrainQueryStatus *old = ecs_get(world, q->entity, TerrainQueryStatus);
    if (!old || ecs_os_memcmp(old, &status, ECS_SIZEOF(TerrainQueryStatus))) {
        ecs_set_ptr(world, q->entity, TerrainQueryStatus, &status);
    }
}

/** Update the query tiles of a terrain: make the ones the foci and requests want nearest first
 * within the budget and drop the ones no focus is near and no request holds within the budget.
 *
 * The tiles are selected only when the Terrain, the foci or the requests changed, a request no
 * longer holds or the update before made or dropped a tile; the status is computed only when a
 * tile, a request or a CPU copy of a texture changed.
 *
 * @param world The world
 * @param ctx The state
 * @param e The terrain
 * @param value The terrain value
 */
static void box_terrain_query_update(
    ecs_world_t *world,
    box_terrain_query_ctx_t *ctx,
    ecs_entity_t e,
    const Terrain *value)
{
    box_terrain_query_settings_t s;
    box_terrain_query_t *q = box_terrain_query_ensure(ctx, e);
    const box_textures_t *textures = box_textures_get(world);
    double local[3], foci[BOX_TERRAIN_FOCI][2], seen[BOX_TERRAIN_FOCI][2];
    int32_t i, count, made = 0, drops = 0, missing = 0;
    uint64_t frame = (uint64_t)ecs_get_world_info(world)->frame_count_total;
    uint32_t readbacks = textures ? textures->readback_serial : 0;
    ecs_entity_t scene = box_world_scene_of(world, e);
    box_terrain_query_settings(value, &s);
    bool reset = !q->made ||
        ecs_os_memcmp(&q->settings, &s, ECS_SIZEOF(box_terrain_query_settings_t));
    if (q->made && reset) {
        box_terrain_query_drop_all(world, q, true);
    }

    if (reset || scene != q->scene) {
        ctx->serial ++;
    }

    q->settings = s;
    q->made = true;
    q->scene = scene;
    if (!q->container || !ecs_is_alive(world, q->container)) {
        box_terrain_query_drop_all(world, q, false);
        q->container = q->scene ? ecs_new_w_pair(world, EcsChildOf, q->scene) : ecs_new(world);
        reset = true;
    }

    if (!s.height) {
        if (!reset && !ecs_vec_count(&q->list) && !ecs_map_count(&q->requests)) {
            goto done;
        }

        box_terrain_query_drop_all(world, q, true);
        ecs_map_clear(&q->requests);
        q->active = false;
        q->dirty = false;
        goto status;
    }

    box_terrain_local(world, e, local);
    if (!q->placed || local[0] != q->local[0] || local[1] != q->local[1] ||
        local[2] != q->local[2])
    {
        ecs_os_memcpy_n(q->local, local, double, 3);
        q->placed = true;
        ctx->serial ++;
    }

    count = box_terrain_foci(world, e, local, foci, NULL);
    ecs_os_memset_n(seen, 0, double, 2 * BOX_TERRAIN_FOCI);
    ecs_os_memcpy_n(seen, foci, double, 2 * count);
    bool walked = count != q->focus_count ||
        ecs_os_memcmp(seen, q->foci, ECS_SIZEOF(double) * 2 * BOX_TERRAIN_FOCI);
    bool expire = ecs_map_count(&q->requests) && frame >= q->expiry;
    bool news = readbacks != q->readbacks;
    bool run = reset || walked || expire || q->dirty || q->active;
    bool changed = reset || news;
    if (!run && !news) {
        goto done;
    }

    q->readbacks = readbacks;
    if (run) {
        q->updates ++;
        q->dirty = false;
        q->focus_count = count;
        ecs_os_memcpy_n(q->foci, seen, double, 2 * BOX_TERRAIN_FOCI);
        box_terrain_query_select(world, ctx, q, foci, count);
        for (i = 0; i < ecs_vec_count(&q->list); i ++) {
            box_terrain_query_tile_t *t = *ecs_vec_get_t(&q->list, box_terrain_query_tile_t*, i);
            t->before = t->wanted;
            t->wanted = false;
        }

        int32_t first = ecs_vec_count(&q->list);
        for (i = 0; i < ecs_vec_count(&ctx->nodes); i ++) {
            const box_terrain_query_node_t *node =
                ecs_vec_get_t(&ctx->nodes, box_terrain_query_node_t, i);
            box_terrain_query_tile_t *t = box_terrain_query_find(q, node->level, node->ix,
                node->iz);
            if (!t && made < s.budget) {
                t = box_terrain_query_tile_new(world, q, node);
                made ++;
            }

            if (t) {
                t->wanted = true;
                t->distance = node->distance;
            } else {
                missing ++;
            }
        }

        for (i = ecs_vec_count(&q->list) - 1; i >= 0; i --) {
            const box_terrain_query_tile_t *t =
                *ecs_vec_get_t(&q->list, box_terrain_query_tile_t*, i);
            changed |= i < first && t->wanted != t->before;
            if (!t->wanted && !box_terrain_query_near(q, t, foci, count) &&
                drops < s.budget * BOX_TERRAIN_QUERY_DROPS)
            {
                box_terrain_query_tile_drop(world, q, i, true);
                drops ++;
            }
        }

        q->active = made || drops;
        changed |= made || drops || missing != q->missing ||
            ecs_map_count(&q->requests) != q->requested;
    }

    if (!changed) {
        goto done;
    }

status:
    q->missing = missing;
    q->requested = ecs_map_count(&q->requests);
    q->statuses ++;
    box_terrain_query_status(world, q, missing);
done:
    return;
}

/** Update the query tiles of every terrain in EcsPostUpdate. The system is immediate to create
 * the texture entities, and updates the terrains after the iteration as it sets their status.
 *
 * @param it The iterator of the system
 */
static void BoxTerrainQuery(
    ecs_iter_t *it)
{
    box_terrain_query_ctx_t *ctx = it->ctx;
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
            box_terrain_query_update(it->world, ctx, e, &terrain);
        }
    }
}

/** Free the query tiles of a terrain, without deleting entities.
 *
 * @param q The tiles
 */
static void box_terrain_query_free(
    box_terrain_query_t *q)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&q->list); i ++) {
        ecs_os_free(*ecs_vec_get_t(&q->list, box_terrain_query_tile_t*, i));
    }

    ecs_vec_fini_t(NULL, &q->list, box_terrain_query_tile_t*);
    ecs_map_fini(&q->map);
    ecs_map_fini(&q->requests);
    ecs_os_free(q);
}

/** Delete the query tiles of a terrain that loses its Terrain or is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxTerrainOnRemoveQuery(
    ecs_iter_t *it)
{
    box_terrain_query_ctx_t *ctx = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_terrain_query_t *q = ecs_map_get_deref(&ctx->terrains, box_terrain_query_t,
            it->entities[i]);
        if (!q) {
            continue;
        }

        box_terrain_query_drop_all(it->world, q, false);
        ctx->serial ++;
        if (q->container && ecs_is_alive(it->world, q->container)) {
            ecs_delete(it->stage, q->container);
        }

        ecs_map_remove(&ctx->terrains, it->entities[i]);
        box_terrain_query_free(q);
        if (ecs_has(it->world, it->entities[i], TerrainQueryStatus)) {
            ecs_remove(it->stage, it->entities[i], TerrainQueryStatus);
        }
    }

done:
    return;
}

/** Free the state of the queries.
 *
 * @param ptr The state
 */
static void box_terrain_query_ctx_free(
    void *ptr)
{
    box_terrain_query_ctx_t *ctx = ptr;
    ecs_map_iter_t it = ecs_map_iter(&ctx->terrains);
    while (ecs_map_next(&it)) {
        box_terrain_query_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&ctx->terrains);
    ecs_map_fini(&ctx->seen);
    ecs_vec_fini_t(NULL, &ctx->nodes, box_terrain_query_node_t);
    ecs_vec_fini_t(NULL, &ctx->terrain_list, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ctx->expired, uint64_t);
    ecs_os_free(ctx);
}

/** Read the terrain and the coordinates of a script call.
 *
 * @param argv The arguments: the terrain, x and z
 * @param terrain Receives the terrain
 * @param x Receives x
 * @param z Receives z
 */
static void box_terrain_fn_args(
    const ecs_value_t *argv,
    ecs_entity_t *terrain,
    double *x,
    double *z)
{
    *terrain = *(const ecs_entity_t*)argv[0].ptr;
    *x = *(const double*)argv[1].ptr;
    *z = *(const double*)argv[2].ptr;
}

/** box.terrain.height(terrain, x, z): the height, 0 where not known.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_height(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t terrain;
    double x, z;
    float h = 0.0f;
    (void)argc;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    box_terrain_height(ctx->world, terrain, x, z, &h);
    *(double*)result->ptr = (double)h;
}

/** Get a component of the normal for a script call.
 *
 * @param ctx The function context
 * @param argv The arguments
 * @param axis The component
 * @return The component, of an up normal where not known
 */
static double box_terrain_fn_normal_axis(
    const ecs_function_ctx_t *ctx,
    const ecs_value_t *argv,
    int32_t axis)
{
    ecs_entity_t terrain;
    double x, z;
    vec3 n;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    box_terrain_normal(ctx->world, terrain, x, z, n);

    return (double)n[axis];
}

/** box.terrain.normal_x(terrain, x, z): x of the normal.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_normal_x(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_terrain_fn_normal_axis(ctx, argv, 0);
}

/** box.terrain.normal_y(terrain, x, z): y of the normal, 1 where not known.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_normal_y(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_terrain_fn_normal_axis(ctx, argv, 1);
}

/** box.terrain.normal_z(terrain, x, z): z of the normal.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_normal_z(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_terrain_fn_normal_axis(ctx, argv, 2);
}

/** box.terrain.slope(terrain, x, z): the slope in radians, 0 where not known.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_slope(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t terrain;
    double x, z;
    float v = 0.0f;
    (void)argc;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    box_terrain_slope(ctx->world, terrain, x, z, &v);
    *(double*)result->ptr = (double)v;
}

/** box.terrain.mask(terrain, x, z, channel): a channel of the masks, 0 where not known or for
 * a channel outside 0..3.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_terrain_fn_mask(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t terrain;
    double x, z;
    vec4 v;
    int32_t c = *(const int32_t*)argv[3].ptr;
    (void)argc;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    box_terrain_mask(ctx->world, terrain, x, z, v);
    *(double*)result->ptr = c >= 0 && c < 4 ? (double)v[c] : 0.0;
}

/** box.terrain.resident(terrain, x, z): whether the point is resident at query_spacing.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the answer
 */
static void box_terrain_fn_resident(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t terrain;
    double x, z;
    (void)argc;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    *(bool*)result->ptr = box_terrain_resident(ctx->world, terrain, x, z, x, z);
}

/** box.terrain.known(terrain, x, z): whether the height at the point is known at any level.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the answer
 */
static void box_terrain_fn_known(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t terrain;
    double x, z;
    (void)argc;
    box_terrain_fn_args(argv, &terrain, &x, &z);
    *(bool*)result->ptr = box_terrain_height(ctx->world, terrain, x, z, NULL);
}

/** Register a script function of the queries that takes a terrain, x and z.
 *
 * @param world The world
 * @param module The module entity
 * @param name Name of the function
 * @param type Return type
 * @param channel The function takes a channel after z
 * @param callback The function
 */
static void box_terrain_query_function(
    ecs_world_t *world,
    ecs_entity_t module,
    const char *name,
    ecs_entity_t type,
    bool channel,
    ecs_function_callback_t callback)
{
    ecs_function_desc_t desc = {
        .name = name,
        .parent = module,
        .return_type = type,
        .params = {
            { "terrain", ecs_id(ecs_entity_t) },
            { "x", ecs_id(ecs_f64_t) },
            { "z", ecs_id(ecs_f64_t) }
        },
        .callback = callback
    };
    if (channel) {
        desc.params[3] = (ecs_script_parameter_t){ "channel", ecs_id(ecs_i32_t) };
    }

    ecs_function_init(world, &desc);
}

void box_terrain_query_import(
    ecs_world_t *world)
{
    box_terrain_query_ctx_t *ctx = ecs_os_calloc_t(box_terrain_query_ctx_t);
    ecs_entity_t module = ecs_get_scope(world);
    ecs_map_init(&ctx->terrains, NULL);
    ecs_map_init(&ctx->seen, NULL);
    ecs_vec_init_t(NULL, &ctx->nodes, box_terrain_query_node_t, 0);
    ecs_vec_init_t(NULL, &ctx->terrain_list, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ctx->expired, uint64_t, 0);

    ECS_COMPONENT_DEFINE(world, TerrainQueryModule);
    ecs_singleton_set(world, TerrainQueryModule, { .ctx = ctx });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Query" }),
        .phase = EcsPostUpdate,
        .query.terms = {{ .id = ecs_id(Terrain), .src.id = EcsSelf }},
        .run = BoxTerrainQuery,
        .ctx = ctx,
        .ctx_free = box_terrain_query_ctx_free,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnRemoveQuery" }),
        .query.terms = {{ .id = ecs_id(Terrain), .src.id = EcsSelf }},
        .events = { EcsOnRemove },
        .callback = BoxTerrainOnRemoveQuery,
        .ctx = ctx
    });

    box_terrain_query_function(world, module, "height", ecs_id(ecs_f64_t), false,
        box_terrain_fn_height);
    box_terrain_query_function(world, module, "normal_x", ecs_id(ecs_f64_t), false,
        box_terrain_fn_normal_x);
    box_terrain_query_function(world, module, "normal_y", ecs_id(ecs_f64_t), false,
        box_terrain_fn_normal_y);
    box_terrain_query_function(world, module, "normal_z", ecs_id(ecs_f64_t), false,
        box_terrain_fn_normal_z);
    box_terrain_query_function(world, module, "slope", ecs_id(ecs_f64_t), false,
        box_terrain_fn_slope);
    box_terrain_query_function(world, module, "mask", ecs_id(ecs_f64_t), true,
        box_terrain_fn_mask);
    box_terrain_query_function(world, module, "resident", ecs_id(ecs_bool_t), false,
        box_terrain_fn_resident);
    box_terrain_query_function(world, module, "known", ecs_id(ecs_bool_t), false,
        box_terrain_fn_known);
}
