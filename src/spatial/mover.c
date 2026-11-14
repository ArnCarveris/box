/**
 * @file spatial/mover.c
 * @brief Movers: the entities that own DynamicTransform, each one leaf of the dynamic tree.
 *
 * The shapes under a mover keep their bounds in the frame of the mover, from their local
 * transforms, so the mover only places its leaf again when its own world transform changes.
 * A change of a shape or of its place in that frame bounds the shapes of the mover again.
 */

#include "index.h"
#include "../render/render.h"

void box_spatial_bounds_of(
    const vec3 l_min,
    const vec3 l_max,
    const float *m,
    vec3 min,
    vec3 max)
{
    vec3 c, e;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        c[i] = (l_min[i] + l_max[i]) * 0.5f;
        e[i] = (l_max[i] - l_min[i]) * 0.5f;
    }

    for (i = 0; i < 3; i ++) {
        float wc = m[12 + i] + m[i] * c[0] + m[4 + i] * c[1] + m[8 + i] * c[2];
        float we = fabsf(m[i]) * e[0] + fabsf(m[4 + i]) * e[1] + fabsf(m[8 + i]) * e[2];
        min[i] = wc - we;
        max[i] = wc + we;
    }
}

/** Test whether an entity is a mover: it owns DynamicTransform, or has it without inheriting it
 * from DynamicTransformSpread, as an instance of a dynamic prefab does.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the entity is a mover
 */
static bool box_spatial_is_mover(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_owns_id(world, e, DynamicTransform) ||
        (ecs_has_id(world, e, DynamicTransform) &&
            !ecs_has_pair(world, e, EcsIsA, DynamicTransformSpread));
}

ecs_entity_t box_spatial_mover_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    for (; e && !result; e = ecs_get_parent(world, e)) {
        if (box_spatial_is_mover(world, e)) {
            result = e;
        }
    }

    return result;
}

box_spatial_mover_t* box_spatial_mover(
    const box_spatial_t *index,
    int32_t mover)
{
    ecs_assert(mover >= 0 && mover < ecs_vec_count(&index->movers), ECS_INVALID_PARAMETER, NULL);

    return &((box_spatial_mover_t*)index->movers.array)[mover];
}

int32_t box_spatial_mover_slot(
    const box_spatial_t *index,
    ecs_entity_t e)
{
    ecs_map_val_t *v = e ? ecs_map_get(&index->mover_of, e) : NULL;

    return v ? (int32_t)*v : -1;
}

void box_spatial_movers_init(
    box_spatial_t *index)
{
    ecs_vec_init_t(NULL, &index->movers, box_spatial_mover_t, 0);
    ecs_map_init(&index->mover_of, NULL);
    ecs_vec_init_t(NULL, &index->changed, int32_t, 0);
    index->free_mover = -1;
}

void box_spatial_movers_fini(
    box_spatial_t *index)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&index->movers); i ++) {
        ecs_vec_fini_t(NULL, &box_spatial_mover(index, i)->members, box_spatial_member_t);
    }

    ecs_vec_fini_t(NULL, &index->movers, box_spatial_mover_t);
    ecs_map_fini(&index->mover_of);
    ecs_vec_fini_t(NULL, &index->changed, int32_t);
}

/** Add a mover to the movers whose members are bounded again in the next refit.
 *
 * @param index The index
 * @param mover The slot of the mover
 */
static void box_spatial_mover_mark(
    box_spatial_t *index,
    int32_t mover)
{
    box_spatial_mover_t *m = box_spatial_mover(index, mover);
    if (!m->dirty) {
        m->dirty = true;
        *ecs_vec_append_t(NULL, &index->changed, int32_t) = mover;
    }
}

/** Get the slot of a mover, taking a free slot when it has none.
 *
 * @param index The index
 * @param e The mover entity
 * @return The slot
 */
static int32_t box_spatial_mover_ensure(
    box_spatial_t *index,
    ecs_entity_t e)
{
    int32_t result = box_spatial_mover_slot(index, e);
    box_spatial_mover_t *m;
    if (result >= 0) {
        goto done;
    }

    if (index->free_mover >= 0) {
        result = index->free_mover;
        m = box_spatial_mover(index, result);
        index->free_mover = m->next;
    } else {
        result = ecs_vec_count(&index->movers);
        m = ecs_vec_append_t(NULL, &index->movers, box_spatial_mover_t);
        ecs_os_memset_t(m, 0, box_spatial_mover_t);
        ecs_vec_init_t(NULL, &m->members, box_spatial_member_t, 0);
    }

    m->entity = e;
    m->frame = 0;
    m->scene = 0;
    m->item = -1;
    m->live = 0;
    m->dirty = false;
    ecs_vec_clear(&m->members);
    ecs_map_insert(&index->mover_of, e, (ecs_map_val_t)result);
done:
    return result;
}

void box_spatial_mover_join(
    box_spatial_t *index,
    SpatialProxy *p,
    ecs_entity_t e,
    ecs_entity_t mover,
    const vec3 shape_min,
    const vec3 shape_max,
    bool disabled)
{
    int32_t slot = box_spatial_mover_ensure(index, mover);
    box_spatial_mover_t *m = box_spatial_mover(index, slot);
    box_spatial_member_t *member;
    if (p->mover == slot) {
        member = ecs_vec_get_t(&m->members, box_spatial_member_t, p->member);
    } else {
        p->mover = slot;
        p->member = ecs_vec_count(&m->members);
        member = ecs_vec_append_t(NULL, &m->members, box_spatial_member_t);
        ecs_os_memset_t(member, 0, box_spatial_member_t);
        member->entity = e;
        m->live ++;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, shape_min), member->shape_min);
    glm_vec3_copy(ECS_CONST_CAST(float*, shape_max), member->shape_max);
    member->disabled = disabled;
    box_spatial_mover_mark(index, slot);
}

/** Take a mover out of its scene and free its slot.
 *
 * @param index The index
 * @param mover The slot of the mover
 */
static void box_spatial_mover_free(
    box_spatial_t *index,
    int32_t mover)
{
    box_spatial_mover_t *m = box_spatial_mover(index, mover);
    box_spatial_scene_t *s = m->item >= 0 ? box_spatial_scene(index, m->scene) : NULL;
    if (s) {
        box_spatial_item_remove(index, s, m->item);
    }

    ecs_map_remove(&index->mover_of, m->entity);
    ecs_vec_clear(&m->members);
    m->entity = 0;
    m->item = -1;
    m->next = index->free_mover;
    index->free_mover = mover;
}

/** Flag the leaf of a mover as disabled when all the shapes under it are, so queries skip it
 * right away.
 *
 * @param index The index
 * @param m The mover
 */
static void box_spatial_mover_enabled(
    box_spatial_t *index,
    const box_spatial_mover_t *m)
{
    const box_spatial_member_t *members = ecs_vec_first(&m->members);
    int32_t i, count = ecs_vec_count(&m->members);
    bool enabled = false;
    for (i = 0; i < count && !enabled; i ++) {
        enabled = members[i].entity && !members[i].disabled;
    }

    box_spatial_scene_t *s = m->item >= 0 ? box_spatial_scene(index, m->scene) : NULL;
    if (s) {
        box_spatial_item(s, m->item)->disabled = !enabled;
    }
}

void box_spatial_mover_leave(
    box_spatial_t *index,
    SpatialProxy *p)
{
    box_spatial_mover_t *m = p->mover >= 0 ? box_spatial_mover(index, p->mover) : NULL;
    if (m && m->entity && p->member < ecs_vec_count(&m->members)) {
        box_spatial_member_t *member = ecs_vec_get_t(
            &m->members, box_spatial_member_t, p->member);
        if (member->entity) {
            member->entity = 0;
            m->live --;
        }

        if (m->live) {
            box_spatial_mover_mark(index, p->mover);
            box_spatial_mover_enabled(index, m);
        } else {
            box_spatial_mover_free(index, p->mover);
        }
    }

    p->mover = -1;
    p->member = -1;
}

void box_spatial_mover_disable(
    box_spatial_t *index,
    const SpatialProxy *p,
    bool disabled)
{
    box_spatial_mover_t *m = box_spatial_mover(index, p->mover);
    ecs_vec_get_t(&m->members, box_spatial_member_t, p->member)->disabled = disabled;
    box_spatial_mover_enabled(index, m);
}

void box_spatial_mover_touch(
    const ecs_world_t *world,
    box_spatial_t *index,
    ecs_entity_t e)
{
    int32_t slot = box_spatial_mover_slot(index, box_spatial_mover_of(world, e));
    if (slot >= 0) {
        box_spatial_mover_mark(index, slot);
    }
}

/** Find the frame of a mover: the mover or its nearest ancestor with a Position, whose world
 * transform places the local transforms below it.
 *
 * @param world The world
 * @param e The mover
 * @return The frame, 0 when neither has a Position
 */
static ecs_entity_t box_spatial_mover_frame(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    for (; e && !result; e = ecs_get_parent(world, e)) {
        if (ecs_has(world, e, Position) || ecs_has_id(world, e, ManualTransform)) {
            result = e;
        }
    }

    return result;
}

/** Get the world transform of the frame of a mover.
 *
 * @param world The world
 * @param m The mover
 * @return The 16 floats of the transform, the identity when the frame has none
 */
static const float* box_spatial_mover_pose(
    const ecs_world_t *world,
    box_spatial_mover_t *m)
{
    static const mat4 identity = GLM_MAT4_IDENTITY_INIT;
    const WorldTransform *t = m->frame ?
        ecs_ref_get(ecs_get_world(world), &m->pose_ref, WorldTransform) : NULL;

    return t ? &t->m[0][0] : &identity[0][0];
}

/** Compute the transform of a shape in the frame of its mover from the local transforms between
 * them, or from the world transforms when a ManualTransform sits between them.
 *
 * @param world The world
 * @param frame The frame of the mover
 * @param inv The inverse of the world transform of the frame
 * @param e The shape
 * @param out Receives the transform
 */
static void box_spatial_member_frame(
    const ecs_world_t *world,
    ecs_entity_t frame,
    const mat4 inv,
    ecs_entity_t e,
    mat4 out)
{
    mat4 acc = GLM_MAT4_IDENTITY_INIT;
    ecs_entity_t a = e;
    bool manual = false;
    for (; a && a != frame && !manual; a = ecs_get_parent(world, a)) {
        const Position *p = ecs_get(world, a, Position);
        manual = ecs_has_id(world, a, ManualTransform);
        if (p && !manual) {
            mat4 local, tmp;
            box_transform_local(p, ecs_get(world, a, Rotation), ecs_get(world, a, Scale), local);
            glm_mat4_mul(local, acc, tmp);
            glm_mat4_copy(tmp, acc);
        }
    }

    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    if ((manual || a != frame) && t) {
        glm_mat4_mul(ECS_CONST_CAST(vec4*, inv), ECS_CONST_CAST(vec4*, t->m), out);
    } else {
        glm_mat4_copy(acc, out);
    }
}

/** Drop the members that left a mover, updating the slots in the proxies of the others.
 *
 * @param world The world
 * @param m The mover
 * @param mover The slot of the mover
 */
static void box_spatial_mover_compact(
    ecs_world_t *world,
    box_spatial_mover_t *m,
    int32_t mover)
{
    int32_t i, kept = 0, count = ecs_vec_count(&m->members);
    box_spatial_member_t *members = ecs_vec_first(&m->members);
    for (i = 0; i < count; i ++) {
        SpatialProxy *p = members[i].entity && ecs_is_alive(world, members[i].entity) ?
            ecs_get_mut(world, members[i].entity, SpatialProxy) : NULL;
        if (p && p->mover == mover) {
            p->member = kept;
            members[kept ++] = members[i];
        }
    }

    ecs_vec_set_count_t(NULL, &m->members, box_spatial_member_t, kept);
    m->live = kept;
}

/** Give a mover the leaf of its bounds in the world, in the scene of the mover.
 *
 * @param index The index
 * @param m The mover
 * @param mover The slot of the mover
 * @param scene The scene of the mover
 * @return true when the leaf changed
 */
static bool box_spatial_mover_leaf(
    box_spatial_t *index,
    box_spatial_mover_t *m,
    int32_t mover,
    ecs_entity_t scene)
{
    vec3 min, max;
    bool result = true;
    box_spatial_bounds_of(m->min, m->max, &m->pose[0][0], min, max);
    if (m->item >= 0 && scene != m->scene) {
        box_spatial_scene_t *old = box_spatial_scene(index, m->scene);
        if (old) {
            box_spatial_item_remove(index, old, m->item);
        }

        m->item = -1;
    }

    box_spatial_scene_t *s = box_spatial_scene_ensure(index, scene);
    m->scene = scene;
    if (m->item < 0) {
        m->item = box_spatial_item_add(s, m->entity, BOX_SPATIAL_DYNAMIC, min, max, index->frame);
        box_spatial_item(s, m->item)->mover = mover;
    } else {
        result = box_spatial_item_set(s, m->item, BOX_SPATIAL_DYNAMIC, min, max, index->frame);
    }

    return result;
}

/** Bound the members of a mover in its frame and give it its leaf.
 *
 * @param world The world
 * @param index The index
 * @param mover The slot of the mover
 */
static void box_spatial_mover_refit(
    ecs_world_t *world,
    box_spatial_t *index,
    int32_t mover)
{
    box_spatial_mover_t *m = box_spatial_mover(index, mover);
    int32_t i, enabled = 0;
    mat4 inv;
    m->dirty = false;
    if (!m->entity) {
        goto done;
    }

    box_spatial_mover_compact(world, m, mover);
    if (!m->live || !ecs_is_alive(world, m->entity)) {
        box_spatial_mover_free(index, mover);
        goto done;
    }

    m->frame = box_spatial_mover_frame(world, m->entity);
    if (m->frame) {
        m->pose_ref = ecs_ref_init(ecs_get_world(world), m->frame, WorldTransform);
    }

    ecs_os_memcpy(m->pose, box_spatial_mover_pose(world, m), sizeof(mat4));
    glm_mat4_inv(m->pose, inv);
    glm_vec3_fill(m->min, FLT_MAX);
    glm_vec3_fill(m->max, -FLT_MAX);
    box_spatial_member_t *members = ecs_vec_first(&m->members);
    for (i = 0; i < m->live; i ++) {
        mat4 rel;
        box_spatial_member_frame(world, m->frame, inv, members[i].entity, rel);
        box_spatial_bounds_of(members[i].shape_min, members[i].shape_max, &rel[0][0],
            members[i].min, members[i].max);
        glm_vec3_minv(m->min, members[i].min, m->min);
        glm_vec3_maxv(m->max, members[i].max, m->max);
        enabled += !members[i].disabled;
    }

    box_spatial_mover_leaf(index, m, mover, box_render_scene_of(world, m->entity));
    box_spatial_item(box_spatial_scene(index, m->scene), m->item)->disabled = !enabled;
done:
    return;
}

void box_spatial_movers_refit(
    ecs_world_t *world,
    box_spatial_t *index)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&index->changed); i ++) {
        box_spatial_mover_refit(world, index, *ecs_vec_get_t(&index->changed, int32_t, i));
    }

    ecs_vec_clear(&index->changed);
}

int32_t box_spatial_movers_place(
    const ecs_world_t *world,
    box_spatial_t *index)
{
    int32_t i, result = 0, count = ecs_vec_count(&index->movers);
    for (i = 0; i < count; i ++) {
        box_spatial_mover_t *m = box_spatial_mover(index, i);
        const float *pose = m->entity && m->item >= 0 ? box_spatial_mover_pose(world, m) : NULL;
        if (pose && ecs_os_memcmp(m->pose, pose, sizeof(mat4))) {
            ecs_os_memcpy(m->pose, pose, sizeof(mat4));
            index->stats.moved += box_spatial_mover_leaf(index, m, i, m->scene);
            result ++;
        }
    }

    return result;
}

void box_spatial_member_bounds(
    const ecs_world_t *world,
    const box_spatial_member_t *m,
    vec3 min,
    vec3 max)
{
    static const mat4 identity = GLM_MAT4_IDENTITY_INIT;
    const WorldTransform *t = ecs_get(world, m->entity, WorldTransform);
    box_spatial_bounds_of(m->shape_min, m->shape_max, t ? &t->m[0][0] : &identity[0][0],
        min, max);
}
