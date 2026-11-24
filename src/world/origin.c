/**
 * @file world/origin.c
 * @brief Shifts of the floating world origin: the rebase system that follows the first focus
 * and the shift that moves the roots, particles, spatial index and render caches of a scene.
 */

#include "world.h"
#include "../spatial/spatial.h"
#include "../render/render.h"
#include "../render/bake.h"
#include <math.h>

/** A shift in progress: the roots it moves */
typedef struct box_world_shift_t {
    const ecs_world_t *world; /**< The world */
    ecs_map_t roots;          /**< Root entities that move, to 1 */
    ecs_vec_t list;           /**< ecs_entity_t, the same roots in the order they were found */
    bool stays;               /**< A root of the scene stays where it is, so not everything in
                               *   the scene moves */
} box_world_shift_t;

/** Check whether the ancestors of a root, or the root itself, keep it out of a shift: it is
 * fixed, a 2D layout tree or under a Surface.
 *
 * @param world The world
 * @param e The root
 * @return Whether the root stays
 */
static bool box_world_stays(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = ecs_has(world, e, LayoutItem);
    while (e && !result) {
        const ParticleEmitter *emitter = ecs_get(world, e, ParticleEmitter);
        result = ecs_has_id(world, e, WorldOriginFixed) || ecs_has(world, e, Layout) ||
            ecs_has(world, e, Surface) || (emitter && emitter->follow);
        e = ecs_get_parent(world, e);
    }

    return result;
}

/** Path of the cached query of the tables whose entities can be roots */
#define BOX_WORLD_ROOTS_QUERY "box.world.RebaseRoots"

/** Path of the cached query of the roots placed without a Position, which a shift leaves */
#define BOX_WORLD_STILL_QUERY "box.world.RebaseStill"

/** Check whether a scene has a root placed without a Position, such as a ManualTransform root
 * with only its WorldTransform, which a shift leaves where it is.
 *
 * @param world The world
 * @param scene The scene
 * @return Whether it has one
 */
static bool box_world_scene_has_still(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t e = ecs_lookup(world, BOX_WORLD_STILL_QUERY);
    const ecs_query_t *q = e ? ecs_query_get(world, e) : NULL;
    bool result = false;
    int32_t i;
    ecs_assert(q != NULL, ECS_INTERNAL_ERROR, BOX_WORLD_STILL_QUERY);

    ecs_iter_t it = ecs_query_iter(world, q);
    while (!result && ecs_query_next(&it)) {
        for (i = 0; i < it.count && !result; i ++) {
            result = box_world_scene_of(world, it.entities[i]) == scene;
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Collect the roots of a scene that a shift moves: the entities with a Position whose
 * ancestors have no WorldTransform and that box_world_stays does not keep.
 *
 * The cached query matches only the tables whose entities have no placed ancestor, so the
 * tables of their descendants are never visited.
 *
 * @param world The world
 * @param scene The scene
 * @param shift Receives the roots
 * @return Tables visited
 */
static int32_t box_world_shift_collect(
    const ecs_world_t *world,
    ecs_entity_t scene,
    box_world_shift_t *shift)
{
    ecs_entity_t e = ecs_lookup(world, BOX_WORLD_ROOTS_QUERY);
    const ecs_query_t *q = e ? ecs_query_get(world, e) : NULL;
    int32_t tables = 0;
    int32_t i;
    ecs_assert(q != NULL, ECS_INTERNAL_ERROR, BOX_WORLD_ROOTS_QUERY);

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        tables ++;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t root = it.entities[i];
            if (!box_world_is_root(world, root) || box_world_scene_of(world, root) != scene) {
                continue;
            }

            if (box_world_stays(world, root)) {
                shift->stays = true;
                continue;
            }

            ecs_map_insert(&shift->roots, root, 1);
            *ecs_vec_append_t(NULL, &shift->list, ecs_entity_t) = root;
        }
    }

    shift->stays = shift->stays || box_world_scene_has_still(world, scene);

    return tables;
}

/** Check whether a shift moves an entity: it or an ancestor is one of its roots.
 *
 * @param ctx The shift
 * @param e The entity
 * @return Whether the entity moves
 */
static bool box_world_shifted(
    void *ctx,
    ecs_entity_t e)
{
    box_world_shift_t *shift = ctx;
    bool result = false;
    while (e && !result) {
        result = ecs_map_get(&shift->roots, e) != NULL;
        e = ecs_get_parent(shift->world, e);
    }

    return result;
}

/** Check whether a shift moves the particles of an emitter: it moves, or the entity it
 * follows moves.
 *
 * @param ctx The shift
 * @param e The emitter
 * @return Whether its particles move
 */
static bool box_world_particles_shifted(
    void *ctx,
    ecs_entity_t e)
{
    box_world_shift_t *shift = ctx;
    const ParticleEmitter *emitter = ecs_is_alive(shift->world, e)
        ? ecs_get(shift->world, e, ParticleEmitter)
        : NULL;
    bool result = box_world_shifted(ctx, e);
    if (!result && emitter && emitter->follow && ecs_is_alive(shift->world, emitter->follow)) {
        result = box_world_shifted(ctx, emitter->follow);
    }

    return result;
}


/** How a shift maps the local positions of the old frame into the new one:
 * new = ratio * u * old + d */
typedef struct box_world_map_t {
    double u[3][3];      /**< Rotation, u[row][column]; the identity when only the origin
                          *   moves */
    double ratio;        /**< Scale: meters per unit of the old over the new frame */
    double d[3];         /**< Offset */
    bool linear;         /**< The rotation or scale changes, so k is not the identity */
} box_world_map_t;

/** Get the map of a shift from one frame to another.
 *
 * @param from The frame before the shift
 * @param to The frame after it
 * @param out Receives the map
 */
static void box_world_map_init(
    const box_world_frame_t *from,
    const box_world_frame_t *to,
    box_world_map_t *out)
{
    double moved[3];
    int32_t i, j;

    out->linear = from->q[0] != to->q[0] || from->q[1] != to->q[1] ||
        from->q[2] != to->q[2] || from->q[3] != to->q[3] || from->s != to->s;
    out->ratio = out->linear ? from->s / to->s : 1.0;

    for (i = 0; i < 3; i ++) {
        moved[i] = from->t[i] - to->t[i];
    }

    for (i = 0; i < 3; i ++) {
        double along = to->r[i][0] * moved[0] + to->r[i][1] * moved[1] +
            to->r[i][2] * moved[2];
        bool plain = !out->linear && box_world_frame_is_translation(to);
        out->d[i] = plain ? moved[i] : along / to->s;
        for (j = 0; j < 3; j ++) {
            double dot = to->r[i][0] * from->r[j][0] + to->r[i][1] * from->r[j][1] +
                to->r[i][2] * from->r[j][2];
            out->u[i][j] = out->linear ? dot : (double)(i == j);
        }
    }
}

/** Apply a map to a point: ratio * u * p + d.
 *
 * @param map The map
 * @param p The point
 * @param out Receives the mapped point, in f32
 */
static void box_world_map_point(
    const box_world_map_t *map,
    const float p[3],
    float out[3])
{
    double result[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        double turned = map->u[i][0] * (double)p[0] + map->u[i][1] * (double)p[1] +
            map->u[i][2] * (double)p[2];
        result[i] = map->ratio * turned + map->d[i];
    }

    for (i = 0; i < 3; i ++) {
        out[i] = (float)result[i];
    }
}

/** Turn the Rotation of a root by a rotation matrix, or set it to one.
 *
 * @param world The world
 * @param e The root
 * @param turn The rotation, turn[row][column]
 * @param absolute Whether turn replaces the rotation of the root instead
 */
static void box_world_turn_root(
    ecs_world_t *world,
    ecs_entity_t e,
    const double turn[3][3],
    bool absolute)
{
    const Rotation *r = ecs_get(world, e, Rotation);
    double result[3][3];
    mat4 old = GLM_MAT4_IDENTITY_INIT;
    int32_t i, j;
    Rotation turned;

    if (r && !absolute) {
        glm_euler_yxz((vec3){ r->x, r->y, r->z }, old);
    }

    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            result[i][j] = turn[i][0] * (double)old[j][0] + turn[i][1] * (double)old[j][1] +
                turn[i][2] * (double)old[j][2];
        }
    }

    box_world_rotation_of_matrix(result, &turned);
    ecs_set_ptr(world, e, Rotation, &turned);
}

/** Map a root into the new frame of a shift: its Position, the WorldTransform of a
 * ManualTransform root and the offset of a PathFollow, whose path is in its world space.
 *
 * When the rotation or scale of the frame changes, its Rotation and Scale too. A WorldCell
 * root is placed at its cell in the new frame instead.
 *
 * @param world The world
 * @param e The root
 * @param map The map of the shift
 * @param frame The frame after the shift
 * @return Components written
 */
static int32_t box_world_shift_root(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_world_map_t *map,
    const box_world_frame_t *frame)
{
    const Position *p = ecs_get(world, e, Position);
    const WorldCell *cell = ecs_get(world, e, WorldCell);
    int32_t written = 0;
    int32_t i;

    if (cell) {
        Position placed;
        box_world_cell_position(cell, frame, &placed);
        ecs_set_ptr(world, e, Position, &placed);
        written ++;
        if (map->linear) {
            float s = (float)(1.0 / frame->s);
            box_world_turn_root(world, e, frame->r, true);
            ecs_set(world, e, Scale, {s, s, s});
            written += 2;
        }
    } else if (p) {
        Position moved;
        box_world_map_point(map, &p->x, &moved.x);
        ecs_set_ptr(world, e, Position, &moved);
        written ++;
        if (map->linear) {
            const Scale *s = ecs_get(world, e, Scale);
            float ratio = (float)map->ratio;
            Scale scaled = {
                (s ? s->x : 1.0f) * ratio,
                (s ? s->y : 1.0f) * ratio,
                (s ? s->z : 1.0f) * ratio
            };
            box_world_turn_root(world, e, map->u, false);
            ecs_set_ptr(world, e, Scale, &scaled);
            written += 2;
        }
    }

    const WorldTransform *t = ecs_has_id(world, e, ManualTransform)
        ? ecs_get(world, e, WorldTransform)
        : NULL;
    if (t) {
        WorldTransform moved = *t;
        int32_t col;
        for (col = 0; col < 3; col ++) {
            for (i = 0; i < 3; i ++) {
                double turned = map->u[i][0] * (double)t->m[col][0] +
                    map->u[i][1] * (double)t->m[col][1] + map->u[i][2] * (double)t->m[col][2];
                moved.m[col][i] = (float)(map->ratio * turned);
            }
        }

        box_world_map_point(map, t->m[3], moved.m[3]);
        ecs_set_ptr(world, e, WorldTransform, &moved);
        written ++;
    }

    const PathFollow *follow = ecs_get(world, e, PathFollow);
    if (follow) {
        PathFollow moved = *follow;
        box_world_map_point(map, follow->offset, moved.offset);
        ecs_set_ptr(world, e, PathFollow, &moved);
        written ++;
    }

    return written;
}

/** Map the world heights of the ground planes of the moved first person controllers and
 * particle collisions into the new frame of a shift.
 *
 * @param world The world
 * @param shift The shift
 * @param map The map of the shift
 * @return Components written
 */
static int32_t box_world_shift_heights(
    ecs_world_t *world,
    box_world_shift_t *shift,
    const box_world_map_t *map)
{
    ecs_vec_t controllers, collisions;
    int32_t i;
    ecs_vec_init_t(NULL, &controllers, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &collisions, ecs_entity_t, 0);
    ecs_iter_t it = ecs_each(world, FirstPersonController);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (box_world_shifted(shift, it.entities[i])) {
                *ecs_vec_append_t(NULL, &controllers, ecs_entity_t) = it.entities[i];
            }
        }
    }

    it = ecs_each(world, ParticleCollision);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (box_world_shifted(shift, it.entities[i])) {
                *ecs_vec_append_t(NULL, &collisions, ecs_entity_t) = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&controllers); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&controllers, ecs_entity_t, i);
        FirstPersonController c = *ecs_get(world, e, FirstPersonController);
        c.ground = (float)(map->ratio * map->u[1][1] * (double)c.ground + map->d[1]);
        ecs_set_ptr(world, e, FirstPersonController, &c);
    }

    for (i = 0; i < ecs_vec_count(&collisions); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&collisions, ecs_entity_t, i);
        ParticleCollision c = *ecs_get(world, e, ParticleCollision);
        c.height = (float)(map->ratio * map->u[1][1] * (double)c.height + map->d[1]);
        ecs_set_ptr(world, e, ParticleCollision, &c);
    }

    int32_t written = ecs_vec_count(&controllers) + ecs_vec_count(&collisions);

    ecs_vec_fini_t(NULL, &controllers, ecs_entity_t);
    ecs_vec_fini_t(NULL, &collisions, ecs_entity_t);

    return written;
}

/** Write the frame after a shift to the WorldOrigin singleton, creating a disabled origin when
 * there is none, record the shift in WorldOriginStatus and emit WorldOriginShift.
 *
 * @param world The world
 * @param scene The scene that moved
 * @param from The frame before the shift
 * @param to The frame after it
 * @param map The map of the shift
 * @param reason Why the origin moves, as WorldOriginStatus.reason
 * @param written Components the shift wrote
 * @param tables Tables the shift looked through for roots
 * @param ms Milliseconds the shift took
 */
static void box_world_shift_record(
    ecs_world_t *world,
    ecs_entity_t scene,
    const box_world_frame_t *from,
    const box_world_frame_t *to,
    const box_world_map_t *map,
    char reason,
    int32_t written,
    int32_t tables,
    float ms)
{
    double offset[3] = {
        to->t[0] - from->t[0],
        to->t[1] - from->t[1],
        to->t[2] - from->t[2]
    };
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    if (o) {
        o->x = to->t[0];
        o->y = to->t[1];
        o->z = to->t[2];
        if (map->linear) {
            o->q[0] = to->q[0];
            o->q[1] = to->q[1];
            o->q[2] = to->q[2];
            o->q[3] = to->q[3];
            o->meters_per_unit = to->s;
        }

        ecs_singleton_modified(world, WorldOrigin);
    } else {
        ecs_singleton_set(world, WorldOrigin, {
            .x = to->t[0],
            .y = to->t[1],
            .z = to->t[2],
            .q = { to->q[0], to->q[1], to->q[2], to->q[3] },
            .meters_per_unit = to->s,
            .threshold = BOX_WORLD_ORIGIN_THRESHOLD,
            .step = BOX_WORLD_ORIGIN_STEP,
            .enabled = false
        });
    }

    const WorldOriginStatus *was = ecs_singleton_get(world, WorldOriginStatus);
    WorldOriginStatus status = {0};
    if (was) {
        status = *was;
    }

    status.shifts ++;
    status.x = offset[0];
    status.y = offset[1];
    status.z = offset[2];
    status.reason = reason;
    status.written = written;
    status.tables = tables;
    status.ms = ms;
    ecs_singleton_set_ptr(world, WorldOriginStatus, &status);

    ecs_enqueue(world, &(ecs_event_desc_t){
        .event = ecs_id(WorldOriginShift),
        .ids = &(ecs_type_t){ .array = (ecs_id_t[]){ ecs_id(WorldOrigin) }, .count = 1 },
        .entity = ecs_id(WorldOrigin),
        .const_param = &(WorldOriginShift){
            .x = offset[0],
            .y = offset[1],
            .z = offset[2],
            .scene = scene,
            .frame = map->linear
        }
    });
}

/** Move the frame of the local origin of a scene now and map its content into the new frame,
 * recording why. A change of the rotation or scale leaves the spatial index, particles and
 * baked probes to see the moves of the roots as changes.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param to The frame after the shift
 * @param reason Why the origin moves, as WorldOriginStatus.reason
 */
static void box_world_origin_shift_to(
    ecs_world_t *world,
    ecs_entity_t scene,
    const box_world_frame_t *to,
    char reason)
{
    box_world_shift_t shift = { .world = world };
    box_world_frame_t from;
    box_world_map_t map;
    ecs_time_t start = {0};
    int32_t written = 0;
    int32_t i;

    ecs_time_measure(&start);
    box_world_frame_get(world, &from);
    box_world_map_init(&from, to, &map);
    ecs_map_init(&shift.roots, NULL);
    ecs_vec_init_t(NULL, &shift.list, ecs_entity_t, 0);
    int32_t tables = box_world_shift_collect(world, scene, &shift);

    if (!map.linear) {
        vec3 delta = { (float)map.d[0], (float)map.d[1], (float)map.d[2] };
        box_spatial_shift(world, scene, delta, shift.stays ? box_world_shifted : NULL, &shift);
        box_render_particles_shift(box_render_particles_get(world), scene, delta,
            box_world_particles_shifted, &shift);
        box_render_bake_shift(world, scene, delta);
        box_render_reflect_shift(world, scene, delta);
    }

    box_render_shadows_reset(world);

    written += box_world_shift_heights(world, &shift, &map);
    for (i = 0; i < ecs_vec_count(&shift.list); i ++) {
        ecs_entity_t root = *ecs_vec_get_t(&shift.list, ecs_entity_t, i);
        written += box_world_shift_root(world, root, &map, to);
    }

    float ms = (float)(ecs_time_measure(&start) * 1000.0);
    box_world_shift_record(world, scene, &from, to, &map, reason, written, tables, ms);

    ecs_map_fini(&shift.roots);
    ecs_vec_fini_t(NULL, &shift.list, ecs_entity_t);
}

/** Shift the origin of a scene by an absolute offset, keeping the rotation and scale of the
 * frame, recording why.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param offset Distance the origin moves along each absolute axis
 * @param reason Why the origin moves, as WorldOriginStatus.reason
 */
static void box_world_origin_shift_for(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3],
    char reason)
{
    box_world_frame_t from, to;
    double t[3];
    box_world_frame_get(world, &from);
    t[0] = from.t[0] + offset[0];
    t[1] = from.t[1] + offset[1];
    t[2] = from.t[2] + offset[2];
    box_world_frame_set(&to, t, from.q, from.s);
    box_world_origin_shift_to(world, scene, &to, reason);
}

void box_world_origin_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3])
{
    box_world_origin_shift_for(world, scene, offset, 'M');
}

/** Get the absolute center of a WorldBody: the corner of the cell of a WorldCell root, else
 * its world position in the frame.
 *
 * @param world The world
 * @param e The body
 * @param frame The frame of the local origin
 * @param out Receives the center
 * @return Whether the body has a place in the world
 */
static bool box_world_body_center(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_world_frame_t *frame,
    double out[3])
{
    const WorldCell *cell = ecs_get(world, e, WorldCell);
    vec3 local;
    bool result = true;

    if (cell) {
        out[0] = (double)cell->x * BOX_WORLD_CELL_SIZE;
        out[1] = (double)cell->y * BOX_WORLD_CELL_SIZE;
        out[2] = (double)cell->z * BOX_WORLD_CELL_SIZE;
    } else if (box_world_position(world, e, local)) {
        double l[3] = { local[0], local[1], local[2] };
        box_world_frame_to_absolute(frame, l, out);
    } else {
        result = false;
    }

    return result;
}

/** Find the WorldBody whose surface is nearest to an absolute position, in f64.
 *
 * @param world The world
 * @param frame The frame of the local origin
 * @param at The absolute position
 * @param center Receives the center of the body
 * @param radius Receives its radius
 * @param altitude Receives the altitude of at over its surface
 * @return The body, 0 for none
 */
static ecs_entity_t box_world_body_nearest(
    ecs_world_t *world,
    const box_world_frame_t *frame,
    const double at[3],
    double center[3],
    double *radius,
    double *altitude)
{
    ecs_entity_t result = 0;
    int32_t i;
    ecs_iter_t it = ecs_each(world, WorldBody);
    while (ecs_each_next(&it)) {
        const WorldBody *bodies = ecs_field(&it, WorldBody, 0);
        for (i = 0; i < it.count; i ++) {
            double c[3];
            if (!box_world_body_center(world, it.entities[i], frame, c)) {
                continue;
            }

            double dx = at[0] - c[0];
            double dy = at[1] - c[1];
            double dz = at[2] - c[2];
            double alt = sqrt(dx * dx + dy * dy + dz * dz) - bodies[i].radius;
            if (!result || alt < *altitude) {
                result = it.entities[i];
                *altitude = alt;
                *radius = bodies[i].radius;
                center[0] = c[0];
                center[1] = c[1];
                center[2] = c[2];
            }
        }
    }

    return result;
}

/** Choose the rotation and scale of the frame for the focus at an absolute position, as
 * WorldOrigin.rotate and WorldOrigin.scale ask, and record the nearest body in
 * WorldOriginStatus.
 *
 * @param world The world
 * @param o The origin
 * @param frame The frame now
 * @param at The absolute position of the focus
 * @param q Receives the rotation
 * @param s Receives the scale
 * @return 'R' when the rotation changes, 'S' when only the scale does, else 0
 */
static char box_world_rebase_frame(
    ecs_world_t *world,
    const WorldOrigin *o,
    const box_world_frame_t *frame,
    const double at[3],
    double q[4],
    double *s)
{
    double center[3], radius = 0, altitude = 0;
    ecs_entity_t body = box_world_body_nearest(world, frame, at, center, &radius, &altitude);
    bool rotate = o->rotate;
    bool scale = o->scale;
    double range = o->rotate_range > 0 ? (double)o->rotate_range : 3.0;
    double angle = o->rotate_angle > 0 ? (double)o->rotate_angle : 0.25;
    double base = o->scale_base > 0 ? (double)o->scale_base : 64.0;
    double top = o->scale_max > 0 ? (double)o->scale_max : 44.0;
    char reason = 0;
    int32_t i;

    WorldOriginStatus *status = ecs_singleton_get_mut(world, WorldOriginStatus);
    if (status) {
        status->body = body;
        status->altitude = body ? altitude : 0.0;
    }

    for (i = 0; i < 4; i ++) {
        q[i] = frame->q[i];
    }

    *s = frame->s;

    if (scale && body) {
        double exact = log2(fmax(altitude / base, 1.0));
        if (fabs(exact - log2(*s)) > 0.75) {
            *s = pow(2.0, fmin(fmax(round(exact), 0.0), top));
        }
    } else if (!scale) {
        *s = 1.0;
    }

    bool turned = frame->q[0] != 0 || frame->q[1] != 0 || frame->q[2] != 0;
    if (rotate && body && altitude < radius * range) {
        double up[3] = { at[0] - center[0], at[1] - center[1], at[2] - center[2] };
        double len = sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (len > 0) {
            for (i = 0; i < 3; i ++) {
                up[i] /= len;
            }

            double cosine = up[0] * frame->r[1][0] + up[1] * frame->r[1][1] +
                up[2] * frame->r[1][2];
            if (cosine < cos(angle * GLM_PI / 180.0)) {
                double turn[4];
                box_world_quat_from_to(frame->r[1], up, turn);
                box_world_quat_mul(turn, frame->q, q);
                reason = 'R';
            }
        }
    } else if (!rotate && turned) {
        q[0] = 0;
        q[1] = 0;
        q[2] = 0;
        q[3] = 1;
        reason = 'R';
    }

    if (!reason && *s != frame->s) {
        reason = 'S';
    }

    return reason;
}

/** Follow the first focus: shift the origin when it is beyond the threshold along an axis, by
 * the whole steps nearest to it along each such axis.
 *
 * With every_frame along every axis it is a step or more away; with from_zero back to the
 * absolute origin, and never away from it. With rotate or scale, a change of the frame moves
 * the origin to the focus.
 *
 * @param it The iterator of the system
 */
static void BoxWorldRebase(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    box_world_focus_t focus;
    box_world_frame_t frame;
    double offset[3] = {0, 0, 0};
    bool shift = false;
    bool past = false;
    bool found;
    int32_t i;
    if (!o || !o->enabled) {
        goto done;
    }

    found = box_world_focus(world, &focus, 1) > 0;
    box_world_frame_get(world, &frame);

    if (o->from_zero) {
        double zero[3] = {0, 0, 0};
        double identity[4] = {0, 0, 0, 1};
        box_world_frame_t to;
        box_world_frame_set(&to, zero, identity, 1.0);
        shift = frame.t[0] != 0 || frame.t[1] != 0 || frame.t[2] != 0 ||
            !box_world_frame_is_translation(&frame);
        if (shift) {
            box_world_origin_shift_to(world, found ? focus.scene : 0, &to, 'Z');
        }

        goto done;
    }

    if (!found) {
        goto done;
    }

    if (o->rotate || o->scale || !box_world_frame_is_translation(&frame)) {
        double local[3] = { focus.position[0], focus.position[1], focus.position[2] };
        double at[3], q[4], s;
        box_world_frame_to_absolute(&frame, local, at);
        char reason = box_world_rebase_frame(world, o, &frame, at, q, &s);
        if (reason) {
            box_world_frame_t to;
            box_world_frame_set(&to, at, q, s);
            box_world_origin_shift_to(world, focus.scene, &to, reason);
            goto done;
        }
    }

    if (!(o->threshold > 0) || !(o->step > 0)) {
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        double p = (double)focus.position[i];
        bool axis_past = fabs(p) > (double)o->threshold;
        if (axis_past || o->every_frame) {
            offset[i] = round(p / (double)o->step) * (double)o->step;
            shift = shift || offset[i] != 0;
            past = past || axis_past;
        }
    }

    if (shift) {
        double moved[3];
        box_world_frame_t to;
        if (box_world_frame_is_translation(&frame)) {
            moved[0] = frame.t[0] + offset[0];
            moved[1] = frame.t[1] + offset[1];
            moved[2] = frame.t[2] + offset[2];
        } else {
            box_world_frame_to_absolute(&frame, offset, moved);
        }

        box_world_frame_set(&to, moved, frame.q, frame.s);
        box_world_origin_shift_to(world, focus.scene, &to, past ? 'T' : 'O');
    }

done:
    return;
}

void box_world_origin_import(
    ecs_world_t *world)
{
    ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RebaseStill" }),
        .terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsInOutNone },
            { .id = ecs_id(Position), .inout = EcsInOutNone, .oper = EcsNot },
            {
                .id = ecs_id(WorldTransform),
                .src.id = EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsNot
            }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RebaseRoots" }),
        .terms = {
            { .id = ecs_id(Position), .inout = EcsInOutNone },
            {
                .id = ecs_id(WorldTransform),
                .src.id = EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsNot
            }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Rebase" }),
        .phase = EcsPostUpdate,
        .run = BoxWorldRebase,
        .immediate = true
    });
}
