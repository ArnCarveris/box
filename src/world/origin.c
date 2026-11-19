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
        result = ecs_has_id(world, e, WorldOriginFixed) || ecs_has(world, e, Layout) ||
            ecs_has(world, e, Surface);
        e = ecs_get_parent(world, e);
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
        if (ecs_table_has_id(world, it.table, EcsPrefab)) {
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

/** Move a root by minus the offset of a shift: its Position, the WorldTransform of a
 * ManualTransform root and the offset of a PathFollow, whose path is in its world space.
 *
 * @param world The world
 * @param e The root
 * @param offset Distance the origin moves
 */
static void box_world_shift_root(
    ecs_world_t *world,
    ecs_entity_t e,
    const double offset[3])
{
    const Position *p = ecs_get(world, e, Position);
    int32_t i;
    if (p) {
        ecs_set(world, e, Position, {
            (float)((double)p->x - offset[0]),
            (float)((double)p->y - offset[1]),
            (float)((double)p->z - offset[2])
        });
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
    }

    const PathFollow *follow = ecs_get(world, e, PathFollow);
    if (follow) {
        PathFollow moved = *follow;
        for (i = 0; i < 3; i ++) {
            moved.offset[i] = (float)((double)moved.offset[i] - offset[i]);
        }

        ecs_set_ptr(world, e, PathFollow, &moved);
    }
}

/** Move the world heights of the ground planes of the moved first person controllers and
 * particle collisions by minus the offset of a shift.
 *
 * @param world The world
 * @param shift The shift
 * @param dy Distance the origin moves along Y
 */
static void box_world_shift_heights(
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

    ecs_vec_fini_t(NULL, &controllers, ecs_entity_t);
    ecs_vec_fini_t(NULL, &collisions, ecs_entity_t);
}

/** Add the offset of a shift to the WorldOrigin and WorldOriginStatus singletons, creating a
 * disabled origin when there is none, and emit WorldOriginShift.
 *
 * @param world The world
 * @param scene The scene that moved
 * @param offset Distance the origin moves
 */
static void box_world_shift_record(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3])
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
        .shifts = shifts, .x = offset[0], .y = offset[1], .z = offset[2]
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

void box_world_origin_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3])
{
    box_world_shift_t shift = { .world = world };
    vec3 delta = { (float)-offset[0], (float)-offset[1], (float)-offset[2] };
    int32_t i;
    ecs_map_init(&shift.roots, NULL);
    ecs_vec_init_t(NULL, &shift.list, ecs_entity_t, 0);
    box_world_shift_collect(world, scene, &shift);

    box_spatial_shift(world, scene, delta, box_world_shifted, &shift);
    box_render_particles_shift(box_render_particles_get(world), scene, delta,
        box_world_shifted, &shift);
    box_render_bake_shift(world, scene, delta);
    box_render_reflect_shift(world, scene, delta);
    box_render_shadows_reset(world);

    box_world_shift_heights(world, &shift, offset[1]);
    for (i = 0; i < ecs_vec_count(&shift.list); i ++) {
        box_world_shift_root(world, *ecs_vec_get_t(&shift.list, ecs_entity_t, i), offset);
    }

    box_world_shift_record(world, scene, offset);

    ecs_map_fini(&shift.roots);
    ecs_vec_fini_t(NULL, &shift.list, ecs_entity_t);
}

/** Shift the origin when the first focus is beyond the threshold along an axis, by the whole
 * steps nearest to the focus along each such axis.
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
    int32_t i;
    if (!o || !o->enabled || !(o->threshold > 0) || !(o->step > 0) ||
        !box_world_focus(world, &focus, 1))
    {
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        double p = (double)focus.position[i];
        if (fabs(p) > (double)o->threshold) {
            offset[i] = round(p / (double)o->step) * (double)o->step;
            shift = shift || offset[i] != 0;
        }
    }

    if (shift) {
        box_world_origin_shift(world, focus.scene, offset);
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
