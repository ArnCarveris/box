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

/** Check whether the entities of a table can be roots: the parent their ChildOf pair names, if
 * any, and its ancestors have no WorldTransform. A table of the children of a placed parent is
 * skipped whole, so the collection of a shift does not visit the descendants of a root.
 *
 * @param world The world
 * @param table The table
 * @return Whether its entities can be roots
 */
static bool box_world_table_rooted(
    const ecs_world_t *world,
    const ecs_table_t *table)
{
    ecs_entity_t parent = ecs_table_get_target(world, table, EcsChildOf, 0);
    bool result = true;
    while (parent && result) {
        result = !ecs_has(world, parent, WorldTransform);
        parent = ecs_get_parent(world, parent);
    }

    return result;
}

/** Collect the roots of a scene that a shift moves: the entities with a Position whose
 * ancestors have no WorldTransform and that box_world_stays does not keep.
 *
 * @param world The world
 * @param scene The scene
 * @param shift Receives the roots
 */
static void box_world_shift_collect(
    const ecs_world_t *world,
    ecs_entity_t scene,
    box_world_shift_t *shift)
{
    int32_t i;
    ecs_iter_t it = ecs_each_id(ECS_CONST_CAST(ecs_world_t*, world), ecs_id(Position));
    while (ecs_each_next(&it)) {
        if (ecs_table_has_id(world, it.table, EcsPrefab) ||
            !box_world_table_rooted(world, it.table))
        {
            continue;
        }

        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (!box_world_is_root(world, e) || box_world_stays(world, e) ||
                box_world_scene_of(world, e) != scene)
            {
                continue;
            }

            ecs_map_insert(&shift->roots, e, 1);
            *ecs_vec_append_t(NULL, &shift->list, ecs_entity_t) = e;
        }
    }
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

/** Move a root by minus the offset of a shift: its Position, the WorldTransform of a
 * ManualTransform root and the offset of a PathFollow, whose path is in its world space. A
 * WorldCell root is placed at its cell relative to the new origin instead.
 *
 * @param world The world
 * @param e The root
 * @param offset Distance the origin moves
 * @param origin The absolute position of the local origin after the shift
 * @return Components written
 */
static int32_t box_world_shift_root(
    ecs_world_t *world,
    ecs_entity_t e,
    const double offset[3],
    const double origin[3])
{
    const Position *p = ecs_get(world, e, Position);
    const WorldCell *cell = ecs_get(world, e, WorldCell);
    int32_t written = 0;
    int32_t i;
    if (cell) {
        Position placed;
        box_world_cell_position(cell, origin, &placed);
        ecs_set_ptr(world, e, Position, &placed);
        written ++;
    } else if (p) {
        ecs_set(world, e, Position, {
            (float)((double)p->x - offset[0]),
            (float)((double)p->y - offset[1]),
            (float)((double)p->z - offset[2])
        });
        written ++;
    }

    const WorldTransform *t = ecs_has_id(world, e, ManualTransform)
        ? ecs_get(world, e, WorldTransform)
        : NULL;
    if (t) {
        WorldTransform moved = *t;
        for (i = 0; i < 3; i ++) {
            moved.m[3][i] = (float)((double)moved.m[3][i] - offset[i]);
        }

        ecs_set_ptr(world, e, WorldTransform, &moved);
        written ++;
    }

    const PathFollow *follow = ecs_get(world, e, PathFollow);
    if (follow) {
        PathFollow moved = *follow;
        for (i = 0; i < 3; i ++) {
            moved.offset[i] = (float)((double)moved.offset[i] - offset[i]);
        }

        ecs_set_ptr(world, e, PathFollow, &moved);
        written ++;
    }

    return written;
}

/** Move the world heights of the ground planes of the moved first person controllers and
 * particle collisions by minus the offset of a shift.
 *
 * @param world The world
 * @param shift The shift
 * @param dy Distance the origin moves along Y
 * @return Components written
 */
static int32_t box_world_shift_heights(
    ecs_world_t *world,
    box_world_shift_t *shift,
    double dy)
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
        c.ground = (float)((double)c.ground - dy);
        ecs_set_ptr(world, e, FirstPersonController, &c);
    }

    for (i = 0; i < ecs_vec_count(&collisions); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&collisions, ecs_entity_t, i);
        ParticleCollision c = *ecs_get(world, e, ParticleCollision);
        c.height = (float)((double)c.height - dy);
        ecs_set_ptr(world, e, ParticleCollision, &c);
    }

    int32_t written = ecs_vec_count(&controllers) + ecs_vec_count(&collisions);

    ecs_vec_fini_t(NULL, &controllers, ecs_entity_t);
    ecs_vec_fini_t(NULL, &collisions, ecs_entity_t);

    return written;
}

/** Add the offset of a shift to the WorldOrigin and WorldOriginStatus singletons, creating a
 * disabled origin when there is none, and emit WorldOriginShift.
 *
 * @param world The world
 * @param scene The scene that moved
 * @param offset Distance the origin moves
 * @param reason Why the origin moves, as WorldOriginStatus.reason
 * @param written Components the shift wrote
 * @param ms Milliseconds the shift took
 */
static void box_world_shift_record(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3],
    char reason,
    int32_t written,
    float ms)
{
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    if (o) {
        o->x += offset[0];
        o->y += offset[1];
        o->z += offset[2];
        ecs_singleton_modified(world, WorldOrigin);
    } else {
        ecs_singleton_set(world, WorldOrigin, {
            .x = offset[0], .y = offset[1], .z = offset[2],
            .threshold = BOX_WORLD_ORIGIN_THRESHOLD,
            .step = BOX_WORLD_ORIGIN_STEP,
            .enabled = false
        });
    }

    WorldOriginStatus *status = ecs_singleton_get_mut(world, WorldOriginStatus);
    int32_t shifts = status ? status->shifts + 1 : 1;
    ecs_singleton_set(world, WorldOriginStatus, {
        .shifts = shifts,
        .x = offset[0],
        .y = offset[1],
        .z = offset[2],
        .reason = reason,
        .written = written,
        .ms = ms
    });

    ecs_enqueue(world, &(ecs_event_desc_t){
        .event = ecs_id(WorldOriginShift),
        .ids = &(ecs_type_t){ .array = (ecs_id_t[]){ ecs_id(WorldOrigin) }, .count = 1 },
        .entity = ecs_id(WorldOrigin),
        .const_param = &(WorldOriginShift){
            .x = offset[0], .y = offset[1], .z = offset[2], .scene = scene
        }
    });
}

/** Shift the world origin of a scene now, recording why (box_world_origin_shift).
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param offset Distance the origin moves along each axis
 * @param reason Why the origin moves, as WorldOriginStatus.reason
 */
static void box_world_origin_shift_for(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3],
    char reason)
{
    box_world_shift_t shift = { .world = world };
    ecs_time_t start = {0};
    int32_t written = 0;
    vec3 delta = { (float)-offset[0], (float)-offset[1], (float)-offset[2] };
    int32_t i;
    ecs_time_measure(&start);
    ecs_map_init(&shift.roots, NULL);
    ecs_vec_init_t(NULL, &shift.list, ecs_entity_t, 0);
    box_world_shift_collect(world, scene, &shift);

    box_spatial_shift(world, scene, delta, box_world_shifted, &shift);
    box_render_particles_shift(box_render_particles_get(world), scene, delta,
        box_world_particles_shifted, &shift);
    box_render_bake_shift(world, scene, delta);
    box_render_reflect_shift(world, scene, delta);
    box_render_shadows_reset(world);

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    double origin[3] = {offset[0], offset[1], offset[2]};
    if (o) {
        origin[0] += o->x;
        origin[1] += o->y;
        origin[2] += o->z;
    }

    written += box_world_shift_heights(world, &shift, offset[1]);
    for (i = 0; i < ecs_vec_count(&shift.list); i ++) {
        ecs_entity_t root = *ecs_vec_get_t(&shift.list, ecs_entity_t, i);
        written += box_world_shift_root(world, root, offset, origin);
    }

    float ms = (float)(ecs_time_measure(&start) * 1000.0);
    box_world_shift_record(world, scene, offset, reason, written, ms);

    ecs_map_fini(&shift.roots);
    ecs_vec_fini_t(NULL, &shift.list, ecs_entity_t);
}

void box_world_origin_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3])
{
    box_world_origin_shift_for(world, scene, offset, 'M');
}

/** Shift the origin when the first focus is beyond the threshold along an axis, by the whole
 * steps nearest to the focus along each such axis; with every_frame along every axis it is a
 * step or more away; with from_zero back to the absolute origin, and never away from it.
 *
 * @param it The iterator of the system
 */
static void BoxWorldRebase(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    box_world_focus_t focus;
    double offset[3] = {0, 0, 0};
    bool shift = false;
    bool past = false;
    int32_t i;
    if (!o || !o->enabled) {
        goto done;
    }

    if (o->from_zero) {
        ecs_entity_t scene = box_world_focus(world, &focus, 1) ? focus.scene : 0;
        offset[0] = -o->x;
        offset[1] = -o->y;
        offset[2] = -o->z;
        shift = offset[0] != 0 || offset[1] != 0 || offset[2] != 0;
        if (shift) {
            box_world_origin_shift_for(world, scene, offset, 'Z');
        }

        goto done;
    }

    if (!(o->threshold > 0) || !(o->step > 0) || !box_world_focus(world, &focus, 1)) {
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
        box_world_origin_shift_for(world, focus.scene, offset, past ? 'T' : 'O');
    }

done:
    return;
}

void box_world_origin_import(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Rebase" }),
        .phase = EcsPostUpdate,
        .run = BoxWorldRebase,
        .immediate = true
    });
}
