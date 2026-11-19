/**
 * @file movement/movement.c
 * @brief Integrates Velocity and AngularVelocity into Position and Rotation, and moves
 * PathFollow entities along their Path3D.
 */

#define BOX_MOVEMENT_IMPL
#include <box.h>
#include <float.h>
#include <math.h>
#include "../geometry/paths.h"

/* Samples of a curved segment in the arc length table, a line needs one */
#define BOX_PATH_FOLLOW_STEPS 64

/** Arc length table of the Path3D owned by one entity */
typedef struct box_path_follow_table_t {
    bool valid;      /**< The path is valid, otherwise the rest is empty */
    int32_t count;   /**< Segments, including the closing segment of a closed path */
    int32_t samples; /**< Entries of lengths and params */
    vec3 (*cubics)[4]; /**< Cubic Bezier points of every segment */
    float *lengths;  /**< Arc length at every sample, from 0 to total */
    float *params;   /**< Segment index plus segment parameter at every sample */
    float total;     /**< Length of the path */
} box_path_follow_table_t;

/** Arc length tables of the path owners, shared by the PathFollow system and the observer that
 * drops a table when its Path3D changes */
typedef struct box_path_follow_cache_t {
    int32_t refs;     /**< System and observer holding the cache */
    ecs_map_t tables; /**< Path owner to box_path_follow_table_t* */
    ecs_map_t errors; /**< Follower to the path that its error was logged for */
} box_path_follow_cache_t;

/** Add velocity times delta time to positions.
 *
 * @param it Iterator over moving entities
 */
static void BoxIntegrateVelocity(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    Velocity *v = ecs_field(it, Velocity, 1);
    float dt = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x += v[i].x * dt;
        p[i].y += v[i].y * dt;
        p[i].z += v[i].z * dt;
    }
}

/** Add angular velocity times delta time to rotations.
 *
 * @param it Iterator over rotating entities
 */
static void BoxIntegrateAngularVelocity(
    ecs_iter_t *it)
{
    Rotation *r = ecs_field(it, Rotation, 0);
    AngularVelocity *v = ecs_field(it, AngularVelocity, 1);
    float dt = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        r[i].x += v[i].x * dt;
        r[i].y += v[i].y * dt;
        r[i].z += v[i].z * dt;
    }
}

/** Free an arc length table.
 *
 * @param table The table, may be NULL
 */
static void box_path_follow_table_free(
    box_path_follow_table_t *table)
{
    if (table) {
        ecs_os_free(table->cubics);
        ecs_os_free(table->lengths);
        ecs_os_free(table->params);
        ecs_os_free(table);
    }
}

/** Drop the arc length table of a path owner.
 *
 * @param cache The cache
 * @param owner The entity that owns the Path3D
 */
static void box_path_follow_drop(
    box_path_follow_cache_t *cache,
    ecs_entity_t owner)
{
    box_path_follow_table_t *table = ecs_map_get_deref(
        &cache->tables, box_path_follow_table_t, owner);
    if (table) {
        ecs_map_remove(&cache->tables, owner);
        box_path_follow_table_free(table);
    }
}

/** Release a reference to the cache, freeing it with the last one.
 *
 * @param ptr The cache
 */
static void box_path_follow_cache_release(
    void *ptr)
{
    box_path_follow_cache_t *cache = ptr;
    if (-- cache->refs == 0) {
        ecs_map_iter_t it = ecs_map_iter(&cache->tables);
        while (ecs_map_next(&it)) {
            box_path_follow_table_free(ecs_map_ptr(&it));
        }

        ecs_map_fini(&cache->tables);
        ecs_map_fini(&cache->errors);
        ecs_os_free(cache);
    }
}

/** Fill the cubics and the arc length samples of a table from a valid path.
 *
 * @param path The path, valid
 * @param table The empty table, receives the segments and samples
 * @return The length of the path
 */
static double box_path_follow_fill(
    const Path3D *path,
    box_path_follow_table_t *table)
{
    int32_t count = box_path3_count(path);
    table->count = count;
    table->cubics = ecs_os_malloc(ECS_SIZEOF(vec3[4]) * count);
    int32_t samples = 1;
    for (int32_t i = 0; i < count; i ++) {
        PathSegmentKind kind = box_path3_cubic(path, i, table->cubics[i]);
        samples += kind == PathLine ? 1 : BOX_PATH_FOLLOW_STEPS;
    }

    table->samples = samples;
    table->lengths = ecs_os_malloc_n(float, samples);
    table->params = ecs_os_malloc_n(float, samples);
    table->lengths[0] = 0;
    table->params[0] = 0;

    int32_t n = 1;
    double length = 0;
    for (int32_t i = 0; i < count; i ++) {
        vec3 *c = table->cubics[i];
        int32_t steps = box_path3_cubic(path, i, c) == PathLine ? 1 : BOX_PATH_FOLLOW_STEPS;
        vec3 previous;
        glm_vec3_copy(c[0], previous);
        for (int32_t j = 1; j <= steps; j ++) {
            vec3 point;
            if (j == steps) {
                glm_vec3_copy(c[3], point);
            } else {
                box_curve_cubic3(c[0], c[1], c[2], c[3], (float)j / steps, point);
            }

            length += glm_vec3_distance(previous, point);
            glm_vec3_copy(point, previous);
            table->lengths[n] = (float)length;
            table->params[n] = (float)i + (float)j / steps;
            n ++;
        }
    }

    return length;
}

/** Build the arc length table of a path.
 *
 * @param path The path, may be NULL
 * @return The table, invalid when the path is
 */
static box_path_follow_table_t* box_path_follow_build(
    const Path3D *path)
{
    box_path_follow_table_t *table = ecs_os_calloc_t(box_path_follow_table_t);
    if (path && box_path3_valid(path)) {
        double length = box_path_follow_fill(path, table);
        if (isfinite(length) && length < FLT_MAX) {
            table->valid = true;
            table->total = (float)length;
        }
    }

    return table;
}

/** Find the arc length table of the path of a follower, building it when needed.
 *
 * @param world The world
 * @param cache The cache
 * @param path The entity with the Path3D
 * @return The table, NULL when the entity has no Path3D
 */
static const box_path_follow_table_t* box_path_follow_table(
    const ecs_world_t *world,
    box_path_follow_cache_t *cache,
    ecs_entity_t path)
{
    ecs_entity_t owner = path && ecs_is_alive(world, path) ?
        ecs_get_target_for_id(world, path, EcsIsA, ecs_id(Path3D)) : 0;
    box_path_follow_table_t *table = NULL;
    if (owner) {
        table = ecs_map_get_deref(&cache->tables, box_path_follow_table_t, owner);
        if (!table) {
            table = box_path_follow_build(ecs_get(world, owner, Path3D));
            ecs_map_insert_ptr(&cache->tables, owner, table);
        }
    }

    return table;
}

/** Log once that a follower has no valid path.
 *
 * @param world The world
 * @param cache The cache
 * @param follower The entity with the PathFollow
 * @param path The path entity of the PathFollow
 */
static void box_path_follow_error(
    const ecs_world_t *world,
    box_path_follow_cache_t *cache,
    ecs_entity_t follower,
    ecs_entity_t path)
{
    ecs_map_val_t *reported = ecs_map_get(&cache->errors, follower);
    if (!reported || *reported != path) {
        *ecs_map_ensure(&cache->errors, follower) = path;
        char *name = ecs_get_path(world, follower);
        char *path_name = path && ecs_is_alive(world, path) ?
            ecs_get_path(world, path) : NULL;
        ecs_err("PathFollow of '%s': path '%s' has no valid Path3D", name,
            path_name ? path_name : "<none>");
        ecs_os_free(path_name);
        ecs_os_free(name);
    }
}

/** Wrap, clamp or reflect a distance into the path according to the mode.
 *
 * @param mode The mode
 * @param total The length of the path, greater than zero
 * @param distance The distance, updated
 * @param speed The speed, its sign flipped by PingPong when it reflects an odd number of times
 */
static void box_path_follow_limit(
    PathFollowMode mode,
    float total,
    float *distance,
    float *speed)
{
    float d = *distance;
    if (mode == PathFollowLoop) {
        d = fmodf(d, total);
        if (d < 0) {
            d += total;
        }

        if (d >= total) {
            d = 0;
        }
    } else if (mode == PathFollowPingPong) {
        if (d < 0 || d > total) {
            double n = floor((double)d / total);
            double r = (double)d - n * total;
            if (fmod(fabs(n), 2.0) == 1.0) {
                d = (float)(total - r);
                *speed = -*speed;
            } else {
                d = (float)r;
            }
        }
    } else {
        d = glm_clamp(d, 0, total);
    }

    *distance = glm_clamp(d, 0, total);
}

/** Evaluate the point and tangent at a distance along a path.
 *
 * @param table The arc length table of a valid path
 * @param distance The distance, 0 to the total length
 * @param point Receives the point
 * @param tangent Receives the unnormalized tangent
 */
static void box_path_follow_eval(
    const box_path_follow_table_t *table,
    float distance,
    vec3 point,
    vec3 tangent)
{
    const float *lengths = table->lengths;
    int32_t lo = 0, hi = table->samples - 1;
    while (hi - lo > 1) {
        int32_t mid = (lo + hi) / 2;
        if (lengths[mid] <= distance) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    float span = lengths[hi] - lengths[lo];
    float f = span > 0 ? glm_clamp((distance - lengths[lo]) / span, 0, 1) : 0;
    float param = table->params[lo] + (table->params[hi] - table->params[lo]) * f;
    int32_t index = (int32_t)floorf(param);
    if (index > table->count - 1) {
        index = table->count - 1;
    }

    if (index < 0) {
        index = 0;
    }

    float t = glm_clamp(param - (float)index, 0, 1);
    vec3 *c = table->cubics[index];
    box_curve_cubic3(c[0], c[1], c[2], c[3], t, point);

    float u = 1 - t;
    for (int i = 0; i < 3; i ++) {
        tangent[i] = 3 * u * u * (c[1][i] - c[0][i]) + 6 * u * t * (c[2][i] - c[1][i]) +
            3 * t * t * (c[3][i] - c[2][i]);
    }

    if (glm_vec3_norm2(tangent) <= 1e-12f) {
        glm_vec3_sub(c[3], c[0], tangent);
    }
}

/** Convert a tangent to a Rotation that looks down -Z along it with +Y up, +X up when the
 * tangent is along Y.
 *
 * @param tangent The tangent, not zero
 * @param r Receives the rotation
 */
static void box_path_follow_orient(
    const vec3 tangent,
    Rotation *r)
{
    vec3 t;
    glm_vec3_normalize_to(ECS_CONST_CAST(float*, tangent), t);
    float horizontal = sqrtf(t[0] * t[0] + t[2] * t[2]);
    r->x = atan2f(t[1], horizontal);
    if (horizontal > 1e-6f) {
        r->y = atan2f(-t[0], -t[2]);
    } else {
        r->y = t[1] > 0 ? GLM_PI_2f : -GLM_PI_2f;
    }

    r->z = 0;
}

/** Move PathFollow entities along their path.
 *
 * @param it Iterator over following entities
 */
static void BoxPathFollowSystem(
    ecs_iter_t *it)
{
    box_path_follow_cache_t *cache = it->ctx;
    Position *p = ecs_field(it, Position, 0);
    PathFollow *f = ecs_field(it, PathFollow, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    float dt = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const box_path_follow_table_t *table =
            box_path_follow_table(it->world, cache, f[i].path);
        if (!table || !table->valid) {
            box_path_follow_error(it->world, cache, e, f[i].path);
            continue;
        }

        if (ecs_map_count(&cache->errors)) {
            ecs_map_remove(&cache->errors, e);
        }

        float distance = f[i].distance;
        float speed = f[i].speed;
        if (!isfinite(distance)) {
            distance = 0;
        }

        if (isfinite(speed)) {
            distance += speed * dt;
        }

        vec3 point, tangent;
        if (table->total > 0) {
            box_path_follow_limit(f[i].mode, table->total, &distance, &speed);
        } else {
            distance = 0;
        }

        box_path_follow_eval(table, distance, point, tangent);

        f[i].distance = distance;
        f[i].speed = speed;
        p[i].x = point[0] + f[i].offset[0];
        p[i].y = point[1] + f[i].offset[1];
        p[i].z = point[2] + f[i].offset[2];

        if (f[i].orient && glm_vec3_norm2(tangent) > 1e-12f) {
            if (speed < 0) {
                glm_vec3_negate(tangent);
            }

            Rotation rotation;
            box_path_follow_orient(tangent, &rotation);
            if (r) {
                r[i] = rotation;
            } else {
                ecs_set_ptr(it->stage, e, Rotation, &rotation);
            }
        }
    }
}

/** Drop the arc length table of a path owner when its Path3D is set or removed.
 *
 * @param it Iterator over the path owners
 */
static void BoxPathFollowInvalidate(
    ecs_iter_t *it)
{
    box_path_follow_cache_t *cache = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        box_path_follow_drop(cache, it->entities[i]);
    }
}

void BoxMovementImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMovement);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);

    ECS_META_COMPONENT(world, Velocity);
    ECS_META_COMPONENT(world, AngularVelocity);
    ECS_META_COMPONENT(world, PathFollowMode);
    ECS_META_COMPONENT(world, PathFollow);

    ecs_add_pair(world, ecs_id(Velocity), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(AngularVelocity), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(Velocity), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(AngularVelocity), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(PathFollow), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(PathFollow), EcsWith, ecs_id(Position));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "IntegrateVelocity" }),
        .query.terms = {
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(Velocity), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BoxIntegrateVelocity
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "IntegrateAngularVelocity" }),
        .query.terms = {
            { .id = ecs_id(Rotation), .inout = EcsInOut },
            { .id = ecs_id(AngularVelocity), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BoxIntegrateAngularVelocity
    });

    box_path_follow_cache_t *cache = ecs_os_calloc_t(box_path_follow_cache_t);
    cache->refs = 2;
    ecs_map_init(&cache->tables, NULL);
    ecs_map_init(&cache->errors, NULL);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FollowPath" }),
        .query.terms = {
            { .id = ecs_id(Position), .inout = EcsOut },
            { .id = ecs_id(PathFollow), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsOut, .oper = EcsOptional }
        },
        .phase = EcsOnUpdate,
        .callback = BoxPathFollowSystem,
        .ctx = cache,
        .ctx_free = box_path_follow_cache_release
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "PathFollowInvalidate" }),
        .query.terms = {{ .id = ecs_id(Path3D), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxPathFollowInvalidate,
        .ctx = cache,
        .ctx_free = box_path_follow_cache_release
    });
}
