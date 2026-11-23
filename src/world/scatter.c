/**
 * @file world/scatter.c
 * @brief Scatter: deterministic placement of weighted prefab instances over a region or a
 * ChunkGrid cell, by density and spacing, filtered by the ground and created in bulk.
 *
 * Candidates lie on a lattice in absolute world coordinates, each a function of its lattice
 * cell and the seed, so a region only picks the candidates within it and adjacent regions join.
 */

#include "world.h"
#include <math.h>

/** Candidates an evaluation visits at most, beyond which a Scatter places nothing */
#define BOX_SCATTER_MAX_CANDIDATES (1 << 22)

/** Intensity of the Poisson disk process of a spacing d, times d squared: (1 - e^-2pi) / pi */
#define BOX_SCATTER_POISSON_RATE (0.31769)

/** Salts of the random values of a candidate */
#define BOX_SCATTER_SALT_X (1u)
#define BOX_SCATTER_SALT_Z (2u)
#define BOX_SCATTER_SALT_PRIORITY (3u)
#define BOX_SCATTER_SALT_KEEP (4u)
#define BOX_SCATTER_SALT_MASK (5u)
#define BOX_SCATTER_SALT_ITEM (6u)
#define BOX_SCATTER_SALT_YAW (7u)
#define BOX_SCATTER_SALT_SCALE (8u)

/** Outcomes of a candidate */
#define BOX_SCATTER_KEPT (0)
#define BOX_SCATTER_THINNED (1)
#define BOX_SCATTER_FILTERED (2)

/** Engine state of a Scatter, added with it */
typedef struct ScatterState {
    Scatter config;            /**< Config of the last finished evaluation */
    bool done;                 /**< Whether config holds a finished evaluation */
    int32_t evaluations;       /**< Finished evaluations */
} ScatterState;

static ECS_COMPONENT_DECLARE(ScatterState);

/** A Scatter prepared for sampling */
typedef struct box_scatter_ctx_t {
    ecs_world_t *world;        /**< The world */
    const Scatter *config;     /**< The scatter */
    ecs_entity_t terrain;      /**< Ground Terrain, 0 for the plane */
    bool masks;                /**< Filters by a mask channel of the terrain */
    bool normals;              /**< Needs the normal of the ground */
    double frame[3];           /**< Absolute position of the origin of the frame of the terrain */
    double plane;              /**< Absolute Y of the plane without a terrain */
    double lattice;            /**< Edge of a cell of the lattice of candidates */
    double spacing;            /**< Least distance between instances */
    double jitter;             /**< Range of a candidate within its lattice cell */
    double keep;               /**< Chance that a spaced candidate stays */
    bool clip;                 /**< Candidates must be within rect */
    double rect[4];            /**< Absolute region: x0, z0, x1, z1, x1 and z1 excluded */
    float weight;              /**< Sum of the weights of the items */
} box_scatter_ctx_t;

/** The ground under a candidate */
typedef struct box_scatter_ground_t {
    double y;                  /**< Absolute Y of the ground */
    float height;              /**< Height that the filter reads: of the terrain, 0 on a plane */
    vec3 normal;               /**< Unit normal */
    float slope;               /**< Slope in degrees */
    float mask;                /**< Value of the filtering mask channel, 1 without */
} box_scatter_ground_t;

/** An instance waiting for creation */
typedef struct box_scatter_instance_t {
    int32_t item;              /**< Index of the item */
    Position p;                /**< Position relative to the scatter entity */
    Rotation r;                /**< Rotation */
    Scale s;                   /**< Scale */
} box_scatter_instance_t;

/** Construct Scatter values with the defaults, their padding zero so that the system can
 * compare them by bytes.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_scatter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Scatter *values = ptr;
    int32_t i;
    (void)ti;
    ecs_os_memset(values, 0, ECS_SIZEOF(Scatter) * count);
    for (i = 0; i < count; i ++) {
        values[i].density = 1;
        values[i].mask_channel = -1;
        values[i].mask_min = 0.5f;
        values[i].mask_max = 1;
        values[i].slope_max = 90;
        values[i].yaw = 360;
        values[i].scale_min = 1;
        values[i].scale_max = 1;
    }
}

/** Mix the bits of a 64 bit value (splitmix64 finalizer).
 *
 * @param h The value
 * @return The mixed value
 */
static uint64_t box_scatter_mix(
    uint64_t h)
{
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;

    return h;
}

/** Hash a lattice cell of a seed for a salt.
 *
 * @param seed The seed
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param salt What the value is for
 * @return The hash
 */
static uint64_t box_scatter_hash(
    int32_t seed,
    int64_t ix,
    int64_t iz,
    uint32_t salt)
{
    uint64_t h = box_scatter_mix((uint64_t)(uint32_t)seed ^ ((uint64_t)salt << 32));
    h = box_scatter_mix(h ^ ((uint64_t)ix * 0x9E3779B97F4A7C15ull));
    h = box_scatter_mix(h ^ ((uint64_t)iz * 0xC2B2AE3D27D4EB4Full));

    return h;
}

/** Get a random value of a lattice cell in [0, 1).
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param salt What the value is for
 * @return The value
 */
static double box_scatter_random(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    uint32_t salt)
{
    uint64_t h = box_scatter_hash(ctx->config->seed, ix, iz, salt);

    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

/** Get the position of the candidate of a lattice cell in absolute X and Z.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param out Receives X and Z
 */
static void box_scatter_candidate(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    double out[2])
{
    double margin = (ctx->lattice - ctx->jitter) * 0.5;
    out[0] = (double)ix * ctx->lattice + margin +
        box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_X) * ctx->jitter;
    out[1] = (double)iz * ctx->lattice + margin +
        box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_Z) * ctx->jitter;
}

/** Find the terrain a scatter places on.
 *
 * @param world The world
 * @param config The scatter
 * @return The terrain, 0 for the plane
 */
static ecs_entity_t box_scatter_terrain(
    const ecs_world_t *world,
    const Scatter *config)
{
    ecs_entity_t result = 0;
    if (config->ground && ecs_id(Terrain) && ecs_is_alive(world, config->ground) &&
        ecs_has(world, config->ground, Terrain))
    {
        result = config->ground;
    }

    return result;
}

/** Prepare a scatter for sampling: its lattice, spacing, chance to keep and ground.
 *
 * @param world The world
 * @param config The scatter
 * @param ctx Receives the prepared scatter
 * @return Whether the scatter places anything
 */
static bool box_scatter_prepare(
    ecs_world_t *world,
    const Scatter *config,
    box_scatter_ctx_t *ctx)
{
    double density = (double)config->density / 100.0;
    double spacing = config->spacing > 0 ? (double)config->spacing : 0.0;
    int32_t i;
    *ctx = (box_scatter_ctx_t){ .world = world, .config = config };
    for (i = 0; i < BOX_WORLD_SCATTER_ITEMS; i ++) {
        if (config->items[i].prefab && config->items[i].weight > 0) {
            ctx->weight += config->items[i].weight;
        }
    }

    bool result = density > 0 && isfinite(density) && ctx->weight > 0 && isfinite(spacing);
    if (result && config->mode == ScatterPoisson) {
        double d = spacing > 0 ? spacing : sqrt(BOX_SCATTER_POISSON_RATE / density);
        double rate = BOX_SCATTER_POISSON_RATE / (d * d);
        ctx->spacing = d;
        ctx->lattice = d / sqrt(2.0);
        ctx->jitter = ctx->lattice;
        ctx->keep = density < rate ? density / rate : 1.0;
    } else if (result) {
        double lattice = sqrt(1.0 / density);
        ctx->lattice = lattice > spacing ? lattice : spacing;
        ctx->spacing = spacing;
        ctx->jitter = ctx->lattice - spacing;
        ctx->keep = density * ctx->lattice * ctx->lattice;
        ctx->keep = ctx->keep < 1.0 ? ctx->keep : 1.0;
    }

    ctx->terrain = box_scatter_terrain(world, config);
    if (ctx->terrain) {
        const Terrain *t = ecs_get(world, ctx->terrain, Terrain);
        vec3 zero = {0, 0, 0};
        double local[3], origin[3];
        box_terrain_origin(world, ctx->terrain, local);
        box_world_to_absolute(world, zero, origin);
        for (i = 0; i < 3; i ++) {
            ctx->frame[i] = origin[i] + local[i];
        }

        ctx->masks = t->masks && config->mask_channel >= 0 && config->mask_channel < 4;
    }

    ctx->normals = config->align > 0 || config->slope_min > 0 || config->slope_max < 90;

    return result && ctx->lattice > 0;
}

/** Check that the candidate of a lattice cell is spaced from its neighbors and survives the
 * thinning of the density.
 *
 * In Poisson mode a candidate stays when no candidate within the spacing has a higher
 * priority, which only the candidates of the two lattice cells around can be.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param p The candidate
 * @return Whether it stays
 */
static bool box_scatter_spaced(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    const double p[2])
{
    bool result = true;
    int32_t dx, dz;
    if (ctx->config->mode == ScatterPoisson) {
        uint64_t own = box_scatter_hash(ctx->config->seed, ix, iz, BOX_SCATTER_SALT_PRIORITY);
        double d2 = ctx->spacing * ctx->spacing;
        for (dz = -2; dz <= 2 && result; dz ++) {
            for (dx = -2; dx <= 2 && result; dx ++) {
                uint64_t other = box_scatter_hash(ctx->config->seed, ix + dx, iz + dz,
                    BOX_SCATTER_SALT_PRIORITY);
                bool higher = other > own || (other == own && (dz > 0 || (!dz && dx > 0)));
                double q[2];
                if ((!dx && !dz) || !higher) {
                    continue;
                }

                box_scatter_candidate(ctx, ix + dx, iz + dz, q);
                result = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) >= d2;
            }
        }
    }

    if (result && ctx->keep < 1.0) {
        result = box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_KEEP) < ctx->keep;
    }

    return result;
}

/** Sample the ground of a scatter under a point.
 *
 * @param ctx The scatter
 * @param p Absolute X and Z
 * @param out Receives the ground
 * @return Whether the ground there is known
 */
static bool box_scatter_ground(
    const box_scatter_ctx_t *ctx,
    const double p[2],
    box_scatter_ground_t *out)
{
    bool result = true;
    *out = (box_scatter_ground_t){ .y = ctx->plane, .normal = {0, 1, 0}, .mask = 1 };
    if (ctx->terrain) {
        double x = p[0] - ctx->frame[0], z = p[1] - ctx->frame[2];
        float h = 0;
        result = box_terrain_height(ctx->world, ctx->terrain, x, z, &h);
        out->y = ctx->frame[1] + (double)h;
        out->height = h;
        if (result && ctx->normals) {
            result = box_terrain_normal(ctx->world, ctx->terrain, x, z, out->normal);
            float ny = out->normal[1] < -1 ? -1 : out->normal[1] > 1 ? 1 : out->normal[1];
            out->slope = acosf(ny) * (float)(180.0 / GLM_PI);
        }

        if (result && ctx->masks) {
            vec4 m = {0, 0, 0, 0};
            result = box_terrain_mask(ctx->world, ctx->terrain, x, z, m);
            out->mask = m[ctx->config->mask_channel];
        }
    }

    return result;
}

/** Get the weight of a mask value: 1 within the range, fading to 0 over the edge outside it.
 *
 * @param config The scatter
 * @param v The value
 * @return The weight
 */
static float box_scatter_mask_weight(
    const Scatter *config,
    float v)
{
    float d = v < config->mask_min ? config->mask_min - v :
        v > config->mask_max ? v - config->mask_max : 0;
    float result = d <= 0 ? 1.0f : config->mask_edge > 0 ? 1.0f - d / config->mask_edge : 0;

    return result > 0 ? result : 0;
}

/** Check the filters of a scatter on the ground of a candidate.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param g The ground
 * @return Whether the candidate passes
 */
static bool box_scatter_filter(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    const box_scatter_ground_t *g)
{
    const Scatter *c = ctx->config;
    bool result = true;
    if (c->height_min < c->height_max) {
        result = g->height >= c->height_min && g->height <= c->height_max;
    }

    if (result && ctx->normals) {
        result = g->slope >= c->slope_min && g->slope <= c->slope_max;
    }

    if (result && ctx->masks) {
        float w = box_scatter_mask_weight(c, g->mask);
        result = w >= 1.0f ||
            (w > 0 && box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_MASK) < (double)w);
    }

    return result;
}

/** Decide on the candidate of a lattice cell, without avoid.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param p The candidate
 * @param g Receives the ground of a kept candidate
 * @return BOX_SCATTER_KEPT, BOX_SCATTER_THINNED or BOX_SCATTER_FILTERED
 */
static int32_t box_scatter_accept(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    const double p[2],
    box_scatter_ground_t *g)
{
    int32_t result = BOX_SCATTER_KEPT;
    if (!box_scatter_spaced(ctx, ix, iz, p)) {
        result = BOX_SCATTER_THINNED;
    } else if (!box_scatter_ground(ctx, p, g) || !box_scatter_filter(ctx, ix, iz, g)) {
        result = BOX_SCATTER_FILTERED;
    }

    return result;
}

/** Check whether a scatter places an instance within a radius of a point, wherever its regions
 * are: within its extent when it has one, anywhere for the cells of a grid.
 *
 * @param ctx The scatter
 * @param p Absolute X and Z
 * @param radius The radius
 * @return Whether it does
 */
static bool box_scatter_near(
    const box_scatter_ctx_t *ctx,
    const double p[2],
    double radius)
{
    int64_t x0 = (int64_t)floor((p[0] - radius) / ctx->lattice);
    int64_t x1 = (int64_t)floor((p[0] + radius) / ctx->lattice);
    int64_t z0 = (int64_t)floor((p[1] - radius) / ctx->lattice);
    int64_t z1 = (int64_t)floor((p[1] + radius) / ctx->lattice);
    int64_t ix, iz;
    bool result = false;
    for (iz = z0; iz <= z1 && !result; iz ++) {
        for (ix = x0; ix <= x1 && !result; ix ++) {
            box_scatter_ground_t g;
            double q[2];
            box_scatter_candidate(ctx, ix, iz, q);
            double dx = q[0] - p[0], dz = q[1] - p[1];
            if (dx * dx + dz * dz >= radius * radius) {
                continue;
            }

            if (ctx->clip && (q[0] < ctx->rect[0] || q[1] < ctx->rect[1] ||
                q[0] >= ctx->rect[2] || q[1] >= ctx->rect[3]))
            {
                continue;
            }

            result = box_scatter_accept(ctx, ix, iz, q, &g) == BOX_SCATTER_KEPT;
        }
    }

    return result;
}

/** Get the absolute region of a scatter and the absolute position of its entity: its extent
 * from the entity, else the cell of its nearest ChunkGrid ancestor.
 *
 * @param world The world
 * @param e The scatter entity
 * @param config The scatter
 * @param rect Receives the region: x0, z0, x1, z1
 * @param anchor Receives the absolute position of the entity
 * @param plane Receives the absolute Y of the plane Y 0 of the parent, may be NULL
 * @return Whether the scatter has a region
 */
static bool box_scatter_region(
    const ecs_world_t *world,
    ecs_entity_t e,
    const Scatter *config,
    double rect[4],
    double anchor[3],
    double *plane)
{
    const Position *own = ecs_get(world, e, Position);
    const ChunkGridCell *cell = NULL;
    double offset[3] = {0, 0, 0};
    vec3 local = {0, 0, 0};
    ecs_entity_t cur = e;
    bool result = false;
    int32_t a;
    if (config->extent[0] > 0 && config->extent[1] > 0) {
        box_world_position(world, e, local);
        box_world_to_absolute(world, local, anchor);
        rect[0] = anchor[0];
        rect[1] = anchor[2];
        rect[2] = anchor[0] + (double)config->extent[0];
        rect[3] = anchor[2] + (double)config->extent[1];
        result = true;
        goto done;
    }

    while (cur && !(cell = ecs_get(world, cur, ChunkGridCell))) {
        const Position *p = ecs_get(world, cur, Position);
        if (p) {
            offset[0] += (double)p->x;
            offset[1] += (double)p->y;
            offset[2] += (double)p->z;
        }

        cur = ecs_get_parent(world, cur);
    }

    if (cell && cell->size[0] > 0 && cell->size[2] > 0) {
        for (a = 0; a < 3; a ++) {
            anchor[a] = (double)cell->cell[a] * (double)cell->size[a] + offset[a];
        }

        rect[0] = (double)cell->cell[0] * (double)cell->size[0];
        rect[1] = (double)cell->cell[2] * (double)cell->size[2];
        rect[2] = (double)(cell->cell[0] + 1) * (double)cell->size[0];
        rect[3] = (double)(cell->cell[2] + 1) * (double)cell->size[2];
        result = true;
    }

done:
    if (plane) {
        *plane = result ? anchor[1] - (own ? (double)own->y : 0.0) : 0.0;
    }

    return result;
}

/** Prepare the scatters a scatter avoids.
 *
 * @param world The world
 * @param e The scatter entity
 * @param config The scatter
 * @param out Receives the prepared scatters, BOX_WORLD_SCATTER_AVOID of them
 * @return Number of prepared scatters
 */
static int32_t box_scatter_avoid_prepare(
    ecs_world_t *world,
    ecs_entity_t e,
    const Scatter *config,
    box_scatter_ctx_t *out)
{
    int32_t result = 0, i;
    double anchor[3];
    for (i = 0; i < BOX_WORLD_SCATTER_AVOID && config->avoid_radius > 0; i ++) {
        ecs_entity_t a = config->avoid[i];
        const Scatter *other = a && a != e && ecs_is_alive(world, a) ?
            ecs_get(world, a, Scatter) : NULL;
        if (!other || !box_scatter_prepare(world, other, &out[result])) {
            continue;
        }

        out[result].clip = other->extent[0] > 0 && other->extent[1] > 0 &&
            box_scatter_region(world, a, other, out[result].rect, anchor, NULL);
        result ++;
    }

    return result;
}

/** Check whether the heights of a terrain are resident over a region with a margin, asking the
 * terrain for them when not.
 *
 * @param ctx The scatter whose terrain to check
 * @param rect The absolute region
 * @param margin Meters around the region that must be resident too
 * @return Whether they are, always without a terrain
 */
static bool box_scatter_ground_ready(
    const box_scatter_ctx_t *ctx,
    const double rect[4],
    double margin)
{
    bool result = true;
    if (ctx->terrain) {
        double x0 = rect[0] - margin - ctx->frame[0], z0 = rect[1] - margin - ctx->frame[2];
        double x1 = rect[2] + margin - ctx->frame[0], z1 = rect[3] + margin - ctx->frame[2];
        result = box_terrain_resident(ctx->world, ctx->terrain, x0, z0, x1, z1);
        if (!result) {
            box_terrain_request(ctx->world, ctx->terrain, x0, z0, x1, z1);
        }
    }

    return result;
}

/** Pick the item of a candidate by the weights of the items.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @return Index of the item
 */
static int32_t box_scatter_item(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz)
{
    const Scatter *c = ctx->config;
    float r = (float)box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_ITEM) * ctx->weight;
    int32_t result = -1, i;
    for (i = 0; i < BOX_WORLD_SCATTER_ITEMS; i ++) {
        if (!c->items[i].prefab || c->items[i].weight <= 0) {
            continue;
        }

        result = i;
        r -= c->items[i].weight;
        if (r < 0) {
            break;
        }
    }

    return result;
}

/** Fill the pose of an instance: its position on the ground relative to the scatter entity,
 * a random yaw, the tilt toward the normal of the ground by align and a random scale.
 *
 * The rotation is yaw, then pitch, then roll (Ry * Rx * Rz), so its up (-sin roll,
 * cos roll cos pitch, cos roll sin pitch) before the yaw is the tilted up turned back by it.
 *
 * @param ctx The scatter
 * @param ix Column of the lattice cell
 * @param iz Row of the lattice cell
 * @param p The candidate
 * @param g Its ground
 * @param anchor Absolute position of the scatter entity
 * @param out Receives the pose
 */
static void box_scatter_pose(
    const box_scatter_ctx_t *ctx,
    int64_t ix,
    int64_t iz,
    const double p[2],
    const box_scatter_ground_t *g,
    const double anchor[3],
    box_scatter_instance_t *out)
{
    const Scatter *c = ctx->config;
    float align = c->align < 0 ? 0 : c->align > 1 ? 1 : c->align;
    float yaw = (float)(box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_YAW) *
        (double)c->yaw * GLM_PI / 180.0);
    float t = (float)box_scatter_random(ctx, ix, iz, BOX_SCATTER_SALT_SCALE);
    float scale = c->scale_min + (c->scale_max - c->scale_min) * t;
    vec3 up = {0, 1, 0}, n;
    glm_vec3_lerp(up, ECS_CONST_CAST(float*, g->normal), align, n);
    if (glm_vec3_norm(n) > 1e-6f) {
        glm_vec3_normalize(n);
    } else {
        glm_vec3_copy(up, n);
    }

    float cy = cosf(yaw), sy = sinf(yaw);
    float nx = n[0] * cy - n[2] * sy, nz = n[0] * sy + n[2] * cy;
    nx = nx < -1 ? -1 : nx > 1 ? 1 : nx;
    out->p = (Position){
        (float)(p[0] - anchor[0]),
        (float)(g->y - (double)c->sink - anchor[1]),
        (float)(p[1] - anchor[2])
    };
    out->r = (Rotation){ atan2f(nz, n[1]), yaw, asinf(-nx) };
    out->s = (Scale){ scale, scale, scale };
}

/** Find the instances of a scatter over its region.
 *
 * @param ctx The scatter
 * @param avoid The scatters it avoids
 * @param avoid_count Number of them
 * @param rect The absolute region
 * @param anchor Absolute position of the scatter entity
 * @param out Receives box_scatter_instance_t
 * @param status Counts the candidates
 */
static void box_scatter_place(
    const box_scatter_ctx_t *ctx,
    const box_scatter_ctx_t *avoid,
    int32_t avoid_count,
    const double rect[4],
    const double anchor[3],
    ecs_vec_t *out,
    ScatterStatus *status)
{
    int64_t x0 = (int64_t)floor(rect[0] / ctx->lattice);
    int64_t x1 = (int64_t)floor(rect[2] / ctx->lattice);
    int64_t z0 = (int64_t)floor(rect[1] / ctx->lattice);
    int64_t z1 = (int64_t)floor(rect[3] / ctx->lattice);
    double radius = (double)ctx->config->avoid_radius;
    int64_t ix, iz;
    int32_t a;
    if ((double)(x1 - x0 + 1) * (double)(z1 - z0 + 1) > BOX_SCATTER_MAX_CANDIDATES) {
        ecs_warn("Scatter: more than %d candidates, density too high for the region",
            BOX_SCATTER_MAX_CANDIDATES);
        goto done;
    }

    for (iz = z0; iz <= z1; iz ++) {
        for (ix = x0; ix <= x1; ix ++) {
            box_scatter_ground_t g;
            double p[2];
            box_scatter_candidate(ctx, ix, iz, p);
            if (p[0] < rect[0] || p[1] < rect[1] || p[0] >= rect[2] || p[1] >= rect[3]) {
                continue;
            }

            status->candidates ++;
            int32_t outcome = box_scatter_accept(ctx, ix, iz, p, &g);
            if (outcome == BOX_SCATTER_THINNED) {
                status->thinned ++;
                continue;
            } else if (outcome == BOX_SCATTER_FILTERED) {
                status->filtered ++;
                continue;
            }

            bool avoided = false;
            for (a = 0; a < avoid_count && !avoided; a ++) {
                avoided = box_scatter_near(&avoid[a], p, radius);
            }

            if (avoided) {
                status->avoided ++;
                continue;
            }

            box_scatter_instance_t *inst = ecs_vec_append_t(NULL, out, box_scatter_instance_t);
            inst->item = box_scatter_item(ctx, ix, iz);
            box_scatter_pose(ctx, ix, iz, p, &g, anchor, inst);
        }
    }

done:
    return;
}

/** Create the instances of a scatter in bulk, a batch per item: IsA its prefab, ChildOf the
 * scatter entity, with Position, Rotation, Scale and optionally SpatialIgnore.
 *
 * @param world The world
 * @param e The scatter entity
 * @param config The scatter
 * @param instances The box_scatter_instance_t
 */
static void box_scatter_create(
    ecs_world_t *world,
    ecs_entity_t e,
    const Scatter *config,
    const ecs_vec_t *instances)
{
    int32_t count = ecs_vec_count(instances), i, item;
    const box_scatter_instance_t *src = ecs_vec_first_t(instances, box_scatter_instance_t);
    int32_t offset[BOX_WORLD_SCATTER_ITEMS + 1] = {0};
    int32_t fill[BOX_WORLD_SCATTER_ITEMS];
    Position *p = ecs_os_malloc_n(Position, count ? count : 1);
    Rotation *r = ecs_os_malloc_n(Rotation, count ? count : 1);
    Scale *s = ecs_os_malloc_n(Scale, count ? count : 1);
    for (i = 0; i < count; i ++) {
        offset[src[i].item + 1] ++;
    }

    for (item = 0; item < BOX_WORLD_SCATTER_ITEMS; item ++) {
        offset[item + 1] += offset[item];
        fill[item] = offset[item];
    }

    for (i = 0; i < count; i ++) {
        int32_t at = fill[src[i].item] ++;
        p[at] = src[i].p;
        r[at] = src[i].r;
        s[at] = src[i].s;
    }

    for (item = 0; item < BOX_WORLD_SCATTER_ITEMS; item ++) {
        int32_t n = offset[item + 1] - offset[item];
        ecs_entity_t prefab = config->items[item].prefab;
        if (!n || !ecs_is_alive(world, prefab)) {
            continue;
        }

        ecs_bulk_desc_t desc = {
            .count = n,
            .ids = {
                ecs_pair(EcsIsA, prefab),
                ecs_pair(EcsChildOf, e),
                ecs_id(Position),
                ecs_id(Rotation),
                ecs_id(Scale),
                config->spatial_ignore && SpatialIgnore ? SpatialIgnore : 0
            },
            .data = (void*[]){ NULL, NULL, &p[offset[item]], &r[offset[item]],
                &s[offset[item]], NULL }
        };
        ecs_bulk_init(world, &desc);
    }

    ecs_os_free(p);
    ecs_os_free(r);
    ecs_os_free(s);
}

/** Evaluate a scatter: wait for its ground, then replace its instances.
 *
 * @param world The world
 * @param e The scatter entity
 * @param config The scatter
 * @param status Receives the counts and times
 * @return Whether it finished, false while it waits for the ground
 */
static bool box_scatter_evaluate(
    ecs_world_t *world,
    ecs_entity_t e,
    const Scatter *config,
    ScatterStatus *status)
{
    box_scatter_ctx_t ctx, avoid[BOX_WORLD_SCATTER_AVOID];
    double rect[4] = {0, 0, 0, 0}, anchor[3] = {0, 0, 0}, margin = 1.0;
    int32_t avoid_count = 0, a;
    bool result = true, valid;
    ecs_vec_t instances;
    ecs_time_t start, t;
    ecs_os_get_time(&start);
    ecs_vec_init_t(NULL, &instances, box_scatter_instance_t, 0);
    valid = box_scatter_prepare(world, config, &ctx);
    valid = box_scatter_region(world, e, config, rect, anchor, &ctx.plane) && valid;
    if (valid) {
        avoid_count = box_scatter_avoid_prepare(world, e, config, avoid);
    }

    for (a = 0; a < avoid_count; a ++) {
        double m = (double)config->avoid_radius + 3.0 * avoid[a].lattice + 1.0;
        margin = m > margin ? m : margin;
    }

    result = !valid || box_scatter_ground_ready(&ctx, rect, margin);
    for (a = 0; a < avoid_count && result; a ++) {
        result = box_scatter_ground_ready(&avoid[a], rect, margin);
    }

    if (!result) {
        status->waiting = true;
        goto done;
    }

    if (valid) {
        box_scatter_place(&ctx, avoid, avoid_count, rect, anchor, &instances, status);
    }

    ecs_os_get_time(&t);
    ecs_delete_with(world, ecs_pair(EcsChildOf, e));
    status->delete_ms = (float)(ecs_time_measure(&t) * 1000.0);
    ecs_os_get_time(&t);
    box_scatter_create(world, e, config, &instances);
    status->create_ms = (float)(ecs_time_measure(&t) * 1000.0);
    status->instances = ecs_vec_count(&instances);

done:
    status->time_ms = (float)(ecs_time_measure(&start) * 1000.0);
    status->per_ms = status->time_ms > 0 ? (float)status->instances / status->time_ms : 0;
    ecs_vec_fini_t(NULL, &instances, box_scatter_instance_t);

    return result;
}

/** Evaluate the scatters whose config changed since their last evaluation, or that wait for
 * their ground, after the ChunkGrid cells of the frame were created. A scatter entity without
 * a Position gets one, so that its instances are placed relative to it.
 *
 * @param it The iterator of the system
 */
static void BoxWorldScatter(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    ecs_vec_t dirty;
    int32_t i;
    ecs_vec_init_t(NULL, &dirty, ecs_entity_t, 0);
    while (ecs_query_next(it)) {
        const Scatter *config = ecs_field(it, Scatter, 0);
        const ScatterState *state = ecs_field(it, ScatterState, 1);
        for (i = 0; i < it->count; i ++) {
            if (!state[i].done ||
                ecs_os_memcmp(&state[i].config, &config[i], ECS_SIZEOF(Scatter)))
            {
                *ecs_vec_append_t(NULL, &dirty, ecs_entity_t) = it->entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&dirty); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&dirty, ecs_entity_t, i);
        if (ecs_is_alive(world, e) && !ecs_has(world, e, Position)) {
            ecs_set(world, e, Position, {0, 0, 0});
        }

        const Scatter *ptr = ecs_is_alive(world, e) ? ecs_get(world, e, Scatter) : NULL;
        if (!ptr) {
            continue;
        }

        Scatter config;
        ScatterStatus status = {0};
        ecs_os_memcpy_t(&config, ptr, Scatter);
        bool done = box_scatter_evaluate(world, e, &config, &status);
        ScatterState *state = ecs_ensure(world, e, ScatterState);
        ecs_os_memcpy_t(&state->config, &config, Scatter);
        state->done = done;
        state->evaluations += done;
        status.evaluations = state->evaluations;
        ecs_set_ptr(world, e, ScatterStatus, &status);
    }

    ecs_vec_fini_t(NULL, &dirty, ecs_entity_t);
}

void box_world_scatter_import(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, ScatterState);

    ecs_set_hooks(world, Scatter, { .ctor = box_scatter_ctor });
    ecs_set_hooks(world, ScatterState, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(Scatter), EcsWith, ecs_id(ScatterState));
    ecs_add_pair(world, ecs_id(Scatter), EcsWith, ecs_id(ScatterStatus));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ScatterPlace" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(Scatter), .inout = EcsIn },
            { .id = ecs_id(ScatterState), .inout = EcsIn }
        },
        .run = BoxWorldScatter,
        .immediate = true
    });
}
