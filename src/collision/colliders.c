/**
 * @file collision/colliders.c
 * @brief Derives the collider of an entity from its shape, override or bounds.
 *
 * A shape with a cheap test gets that test: a sphere a ball, a box and a
 * rounded box an oriented box, a capsule a capsule. Anything else falls back on
 * the world bounds the renderer wrote. The result is one Collider in world
 * space with the scale of the entity applied, which every query reads.
 */

#include "collision_internal.h"

/** Collider of an entity before its world transform is applied */
typedef struct box_collider_local_t {
    ColliderKind kind; /**< Shape of the collider */
    vec3 center;       /**< Center in the space of the entity */
    vec3 half;         /**< Half extents, or radius in x and half length in y */
} box_collider_local_t;

/** Derive a collider from the primitive shape of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param kind Receives the shape of the collider
 * @param center Receives the center in the space of the entity
 * @param half Receives the half extents in the space of the entity
 * @return true when the entity has a shape with a cheap collider
 */
static bool box_collider_of_shape(
    const ecs_world_t *world,
    ecs_entity_t e,
    ColliderKind *kind,
    vec3 *center,
    vec3 *half)
{
    bool result = true;
    center[0] = vec3_new(0, 0, 0);
    if (!ecs_id(Sphere)) {
        result = false;
        goto done;
    }

    if (ecs_has(world, e, Sphere)) {
        float r = ecs_get(world, e, Sphere)->radius;
        kind[0] = ColliderSphere;
        half[0] = vec3_new(r, r, r);
        goto done;
    }

    if (ecs_has(world, e, IcoSphere)) {
        float r = ecs_get(world, e, IcoSphere)->radius;
        kind[0] = ColliderSphere;
        half[0] = vec3_new(r, r, r);
        goto done;
    }

    if (ecs_has(world, e, Box)) {
        const Box *b = ecs_get(world, e, Box);
        kind[0] = ColliderBox;
        half[0] = vec3_scale(vec3_new(b->x, b->y, b->z), 0.5f);
        goto done;
    }

    if (ecs_has(world, e, RoundedBox)) {
        const RoundedBox *b = ecs_get(world, e, RoundedBox);
        kind[0] = ColliderBox;
        half[0] = vec3_scale(vec3_new(b->x, b->y, b->z), 0.5f);
        goto done;
    }

    if (ecs_has(world, e, Capsule)) {
        const Capsule *c = ecs_get(world, e, Capsule);
        kind[0] = ColliderCapsule;
        half[0] = vec3_new(c->radius, c->length * 0.5f, c->radius);
        goto done;
    }

    result = false;
done:
    return result;
}

/** Get the shape a collider can be derived from without the bounds.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the collider in the space of the entity
 * @return true when the entity has a shape or an override
 */
static bool box_collider_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_collider_local_t *out)
{
    bool result = true;
    const SphereCollider *sc = ecs_get(world, e, SphereCollider);
    if (sc) {
        out->kind = ColliderSphere;
        out->center = sc->center;
        out->half = vec3_new(sc->radius, sc->radius, sc->radius);
        goto done;
    }

    const BoxCollider *bc = ecs_get(world, e, BoxCollider);
    if (bc) {
        out->kind = ColliderBox;
        out->center = bc->center;
        out->half = bc->half;
        goto done;
    }

    const CapsuleCollider *cc = ecs_get(world, e, CapsuleCollider);
    if (cc) {
        out->kind = ColliderCapsule;
        out->center = cc->center;
        out->half = vec3_new(cc->radius, cc->height * 0.5f, cc->radius);
        goto done;
    }

    result = box_collider_of_shape(world, e, &out->kind, &out->center, &out->half);
done:
    return result;
}

/** Fill a collider from the world bounds of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the collider
 * @return true when the entity has bounds that cover something
 */
static bool box_collider_of_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    Collider *out)
{
    bool result = false;
    const AABB *aabb = ecs_get(world, e, AABB);
    if (!aabb) {
        goto done;
    }

    vec3 half = vec3_scale(vec3_sub(aabb->max, aabb->min), 0.5f);
    if (half.x <= 0.0f && half.y <= 0.0f && half.z <= 0.0f) {
        goto done;
    }

    out->kind = ColliderAabb;
    out->center = vec3_scale(vec3_add(aabb->max, aabb->min), 0.5f);
    out->half = half;
    out->axis_x = vec3_new(1, 0, 0);
    out->axis_y = vec3_new(0, 1, 0);
    out->axis_z = vec3_new(0, 0, 1);
    result = true;
done:
    return result;
}

/** Take one axis of a world matrix apart into a direction and a length.
 *
 * @param m The matrix
 * @param index The column
 * @param scale Receives the length of the column
 * @return The normalized column
 */
static vec3 box_collider_axis(
    const mat4 *m,
    int32_t index,
    float *scale)
{
    vec3 axis = vec3_new(m->v[index * 4], m->v[index * 4 + 1], m->v[index * 4 + 2]);
    float len = vec3_length(axis);
    scale[0] = len;

    return len > 1e-8f ? vec3_scale(axis, 1.0f / len) : vec3_new(
        index == 0, index == 1, index == 2);
}

bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    Collider *out)
{
    bool result = false;
    if (ecs_has_id(world, e, NoCollider)) {
        goto done;
    }

    box_collider_local_t local = {0};
    if (!box_collider_local(world, e, &local)) {
        result = box_collider_of_bounds(world, e, out);
        goto done;
    }

    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    mat4 m = wt ? wt->m : mat4_identity();
    vec3 scale;
    out->axis_x = box_collider_axis(&m, 0, &scale.x);
    out->axis_y = box_collider_axis(&m, 1, &scale.y);
    out->axis_z = box_collider_axis(&m, 2, &scale.z);
    out->center = mat4_transform_point(&m, local.center);
    out->kind = local.kind;
    out->half = vec3_mul(local.half, scale);
    if (local.kind == ColliderSphere) {
        float r = local.half.x * ECS_MAX(scale.x, ECS_MAX(scale.y, scale.z));
        out->half = vec3_new(r, r, r);
    } else if (local.kind == ColliderCapsule) {
        float r = local.half.x * ECS_MAX(scale.x, scale.z);
        out->half = vec3_new(r, local.half.y * scale.y, r);
    }

    result = true;
done:
    return result;
}

void box_collision_touch(
    box_collision_state_t *state,
    ecs_entity_t e)
{
    ecs_map_ensure(&state->colliders, e)[0] = 1;
}

/** Mark the entities of an observer for a new collider.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnCollider(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_collision_touch(state, it->entities[i]);
    }
}

/** Take a collider that went away out of the index and mark its root.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnColliderGone(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!ecs_has(world, e, RootBounds)) {
            box_index_unset(&state->index, e);
        }

        ecs_entity_t root = box_collision_root_of(world, e);
        if (root && root != e) {
            box_collision_mark(state, root, BOX_ROOT_FULL);
        }
    }
}

/** Mark an entity whose bounds changed when it can carry a collider.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnColliderBounds(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has(world, e, Collider) || !ecs_has_id(world, e, NoCollider)) {
            box_collision_touch(state, e);
        }
    }
}

/** Write the collider of one entity and put it in the index where it belongs.
 *
 * @param world The world, never a stage
 * @param state The state
 * @param e The entity
 */
static void box_collision_apply(
    ecs_world_t *world,
    box_collision_state_t *state,
    ecs_entity_t e)
{
    ecs_entity_t root = box_collision_root_of(world, e);
    bool had = ecs_has(world, e, Collider);
    Collider c = {0};
    bool has = box_collider_resolve(world, e, &c);
    if (has) {
        ecs_set_ptr(world, e, Collider, &c);
    } else if (had) {
        ecs_remove(world, e, Collider);
    }

    if (root) {
        if (had != has) {
            box_collision_mark(state, root, BOX_ROOT_FULL);
        }

        goto done;
    }

    if (has && !box_hierarchy_ancestor_has(world, e, DynamicTransform)) {
        vec3 min, max;
        box_collider_bounds(&c, &min, &max);
        box_index_set(&state->index, e, min, max);
        goto done;
    }

    box_index_unset(&state->index, e);
done:
    return;
}

void box_collision_colliders_flush(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    ecs_vec_clear(&state->resolve);

    ecs_map_iter_t mit = ecs_map_iter(&state->colliders);
    while (ecs_map_next(&mit)) {
        ecs_vec_append_t(NULL, &state->resolve, ecs_entity_t)[0] =
            (ecs_entity_t)ecs_map_key(&mit);
    }

    ecs_map_clear(&state->colliders);

    int32_t i, count = ecs_vec_count(&state->resolve);
    ecs_entity_t *resolve = ecs_vec_first_t(&state->resolve, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = resolve[i];
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        box_collision_apply(world, state, e);
    }
}

void box_collision_colliders_init(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    ecs_id_t sources[] = {
        ecs_id(SphereCollider), ecs_id(BoxCollider), ecs_id(CapsuleCollider)
    };

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = sources[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxCollisionOnCollider,
            .ctx = state
        });
    }

    ecs_observer(world, {
        .query.terms = {{ .id = NoCollider }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxCollisionOnCollider,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(WorldTransform) },
            { .id = ecs_id(Collider), .inout = EcsInOutFilter }
        },
        .events = { EcsOnSet },
        .callback = BoxCollisionOnCollider,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(AABB) }},
        .events = { EcsOnSet },
        .callback = BoxCollisionOnColliderBounds,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Collider) }},
        .events = { EcsOnRemove },
        .callback = BoxCollisionOnColliderGone,
        .ctx = state
    });

    box_collision_shapes_init(world, state);
}

void box_collision_shapes_init(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    if (state->shapes || !ecs_id(Sphere)) {
        goto done;
    }

    ecs_id_t shapes[] = {
        ecs_id(Sphere), ecs_id(IcoSphere), ecs_id(Box), ecs_id(RoundedBox),
        ecs_id(Capsule)
    };

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = shapes[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxCollisionOnCollider,
            .ctx = state,
            .yield_existing = true
        });
    }

    state->shapes = true;
done:
    return;
}
