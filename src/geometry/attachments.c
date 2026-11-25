/**
 * @file geometry/attachments.c
 * @brief Places entities with a SurfaceAttachment on the surface of their target shape.
 */

#include "mesh_cache.h"
#include <math.h>

static ECS_TAG_DECLARE(SurfaceOwnsManual);

static bool box_attachment_update(ecs_world_t *world, ecs_entity_t e, int depth, mat4 output);

/** Compute the world transform of an entity without relying on the transform system.
 *
 * @param world The world.
 * @param e The entity.
 * @param m Output matrix.
 * @param depth Current recursion depth, used to break attachment cycles.
 * @return True if the transform could be computed, false otherwise.
 */
static bool box_attachment_transform(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 m,
    int depth)
{
    if (depth > 64 || !ecs_is_alive(world, e)) {
        goto error;
    }

    if (ecs_has(world, e, SurfaceAttachment)) {
        if (!box_attachment_update(world, e, depth + 1, m)) {
            goto error;
        }

        goto done;
    }

    if (ecs_has_id(world, e, ManualTransform)) {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        if (!t) {
            goto error;
        }

        box_transform_mat4(t, NULL, m);
        goto done;
    }

    ecs_entity_t parent = ecs_get_parent(world, e);
    mat4 base, local;
    glm_mat4_identity(base);
    if (parent && !box_attachment_transform(world, parent, base, depth + 1)) {
        goto error;
    }

    const Position *position = ecs_get(world, e, Position);
    const Rotation *rotation = ecs_get(world, e, Rotation);
    const Scale *scale = ecs_get(world, e, Scale);
    glm_mat4_identity(local);
    if (rotation) {
        glm_euler_yxz((vec3){rotation->x, rotation->y, rotation->z}, local);
    }

    if (scale) {
        glm_scale(local, (vec3){scale->x, scale->y, scale->z});
    }

    if (position) {
        glm_vec3_copy((vec3){position->x, position->y, position->z}, local[3]);
    }

    glm_mat4_mul(base, local, m);

done:
    return true;
error:
    return false;
}

/** Compute and store the world transform of an attached entity.
 *
 * @param world The world.
 * @param e The attached entity.
 * @param depth Current recursion depth, used to break attachment cycles.
 * @param output Optional output for the computed transform.
 * @return True if the attachment could be resolved, false otherwise.
 */
static bool box_attachment_update(
    ecs_world_t *world,
    ecs_entity_t e,
    int depth,
    mat4 output)
{
    const SurfaceAttachment *value = ecs_get(world, e, SurfaceAttachment);
    if (!value || !value->target || value->target == e || depth > 64 ||
        !ecs_is_alive(world, value->target))
    {
        goto error;
    }

    SurfaceAttachment attachment = *value;
    box_surface_sample_t sample;
    if (!box_surface_evaluate(world, attachment.target, attachment.region,
        attachment.u, attachment.v, &sample))
    {
        goto error;
    }

    mat4 target, inverse, frame, rotation;
    if (!box_attachment_transform(world, attachment.target, target, depth + 1) ||
        fabsf(glm_mat4_det(target)) < 0.00000001f)
    {
        goto error;
    }

    glm_mat4_inv(target, inverse);
    glm_mat4_transpose(inverse);
    glm_mat4_identity(frame);
    vec3 point;
    glm_vec3_muladds(sample.normal, attachment.offset, sample.position);
    glm_mat4_mulv3(target, sample.position, 1, point);
    glm_mat4_mulv3(inverse, sample.normal, 0, frame[2]);
    glm_vec3_normalize(frame[2]);
    glm_mat4_mulv3(target, sample.tangent, 0, frame[0]);
    glm_vec3_muladds(frame[2], -glm_vec3_dot(frame[0], frame[2]), frame[0]);
    glm_vec3_normalize(frame[0]);
    glm_vec3_cross(frame[2], frame[0], frame[1]);
    glm_vec3_copy(point, frame[3]);
    glm_euler_yxz(attachment.rotation, rotation);
    mat4 pose;
    glm_mat4_mul(frame, rotation, pose);
    const Scale *scale = ecs_get(world, e, Scale);
    if (scale) {
        glm_scale(pose, (vec3){scale->x, scale->y, scale->z});
    }

    WorldTransform result = {0};
    box_transform_from_mat4(pose, NULL, &result);
    const WorldTransform *old = ecs_get(world, e, WorldTransform);
    bool changed = !old;
    for (int i = 0; old && i < 3; i ++) {
        changed |= fabs(old->position[i] - result.position[i]) > 0.000001;
        for (int j = 0; j < 3; j ++) {
            changed |= fabsf(old->basis[i][j] - result.basis[i][j]) > 0.000001f;
        }
    }

    if (output) {
        glm_mat4_copy(pose, output);
    }

    if (changed) {
        ecs_set_ptr(world, e, WorldTransform, &result);
    }

    return true;
error:
    return false;
}

/** Observer that takes over ManualTransform for entities that get a SurfaceAttachment.
 *
 * @param it The iterator.
 */
static void box_attachment_set(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!ecs_has_id(it->world, e, ManualTransform)) {
            ecs_add_id(it->stage, e, SurfaceOwnsManual);
            ecs_add_id(it->stage, e, ManualTransform);
        }

        box_attachment_update(it->stage, e, 0, NULL);
    }
}

/** Observer that releases ManualTransform when a SurfaceAttachment is removed.
 *
 * @param it The iterator.
 */
static void box_attachment_remove(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (it->other_table && ecs_has_id(it->world, e, SurfaceOwnsManual)) {
            ecs_remove_id(it->stage, e, SurfaceOwnsManual);
            ecs_remove_id(it->stage, e, ManualTransform);
        }
    }
}

/** System that updates the transforms of all attached entities.
 *
 * @param it The iterator.
 */
static void box_attachments_update(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        box_attachment_update(it->world, it->entities[i], 0, NULL);
    }
}

void box_geometry_attachments_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxTransform);
    ECS_TAG_DEFINE(world, SurfaceOwnsManual);
    ecs_observer(world, {.query.terms = {{.id = ecs_id(SurfaceAttachment)}},
        .events = {EcsOnSet}, .callback = box_attachment_set});
    ecs_observer(world, {.query.terms = {{.id = ecs_id(SurfaceAttachment)}},
        .events = {EcsOnRemove}, .callback = box_attachment_remove});
    ecs_system(world, {.entity = ecs_entity(world, {.name = "SurfacePlacement"}),
        .phase = EcsPostUpdate, .query.terms = {{.id = ecs_id(SurfaceAttachment)}},
        .callback = box_attachments_update});
}
