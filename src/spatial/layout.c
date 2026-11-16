/**
 * @file spatial/layout.c
 * @brief Spatial layout solver: Fit, Align, Distribute and Array rules resolved to transforms.
 *
 * Every entity with a layout rule, or read by one, has a node that caches its pose, bounds and
 * basis between passes. Changed inputs invalidate the nodes that read from them.
 */

#include "layout.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EPS 0.000001f

ECS_COMPONENT_DECLARE(LayoutOutput);
ECS_TAG_DECLARE(ArrayPrototype);
ECS_COMPONENT_DECLARE(PrototypeSource);

static ECS_COMPONENT_DECLARE(LayoutContext);

/** Singleton that holds the layout context of a world */
typedef struct LayoutContext {
    layout_ctx_t *ptr; /**< The layout context */
} LayoutContext;

/** Record that a node read from another node.
 *
 * Only the last four dependencies are checked for duplicates.
 *
 * @param node The node that read
 * @param dependency The node that was read from
 */
static void depend(
    layout_node_t *node,
    layout_node_t *dependency)
{
    int32_t count = ecs_vec_count(&node->deps);
    layout_node_t **deps = ecs_vec_first(&node->deps);
    bool found = false;
    for (int32_t i = count - 1; !found && i >= 0 && i >= count - 4; i--) {
        found = deps[i] == dependency;
    }

    if (!found) {
        *ecs_vec_append_t(NULL, &node->deps, layout_node_t *) = dependency;
    }
}

/** Add a node to the nodes that are solved and published in the current pass.
 *
 * @param ctx The layout context
 * @param n The node
 */
static void frame_add(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->frame != ctx->epoch) {
        n->frame = ctx->epoch;
        *ecs_vec_append_t(NULL, &ctx->frame, layout_node_t *) = n;
    }
}

/** Test whether an entity has a layout rule or layout output of its own.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the node of the entity is solved and published
 */
static bool wants(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_owns(world, e, Fit) || ecs_owns(world, e, SpatialAlign) ||
           ecs_owns(world, e, Distribute) || ecs_owns(world, e, LayoutOutput) ||
           ecs_owns(world, e, ArrayInstance) ||
           (ecs_owns(world, e, Array) && !ecs_has_id(world, e, EcsPrefab));
}

/** Clear the results of a node so that it is solved again in the current pass.
 *
 * The array error, the position in the node list and the pass marks are kept.
 *
 * @param ctx The layout context
 * @param n The node
 */
static void node_reset(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    ecs_world_t *world = ctx->world;
    layout_node_t keep = *n;
    ecs_vec_clear(&keep.deps);
    if (!ecs_owns(world, keep.entity, Array) || ecs_has_id(world, keep.entity, EcsPrefab)) {
        keep.array_error = (SpatialDiagnostic){0};
    }

    *n = (layout_node_t){.entity = keep.entity,
                         .error = keep.array_error,
                         .array_error = keep.array_error,
                         .index = keep.index,
                         .epoch = ctx->epoch,
                         .frame = keep.frame,
                         .mark = keep.mark,
                         .array_epoch = keep.array_epoch,
                         .deps = keep.deps};
    glm_vec3_one(n->fit);
    n->requested = wants(world, n->entity);
    frame_add(ctx, n);
    if (box_transform_dynamic(world, ecs_get_table(world, n->entity))) {
        *ecs_vec_append_t(NULL, &ctx->dynamic, ecs_entity_t) = n->entity;
    }
}

layout_node_t *box_spatial_layout_node_get(
    layout_ctx_t *ctx,
    ecs_entity_t e)
{
    layout_node_t *n = ecs_map_get_deref(&ctx->nodes, layout_node_t, e);
    if (!n) {
        n = ecs_os_calloc_t(layout_node_t);
        n->entity = e;
        n->index = ecs_vec_count(&ctx->list);
        ecs_vec_init_t(NULL, &n->deps, layout_node_t *, 0);
        *ecs_vec_append_t(NULL, &ctx->list, layout_node_t *) = n;
        ecs_map_insert_ptr(&ctx->nodes, e, n);
        node_reset(ctx, n);
    }

    return n;
}

/** Free a node.
 *
 * @param n The node
 */
static void node_free(
    layout_node_t *n)
{
    ecs_vec_fini_t(NULL, &n->deps, layout_node_t *);
    ecs_os_free(n);
}

/** Remove a node from the context and free it.
 *
 * @param ctx The layout context
 * @param n The node
 */
static void node_remove(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    ecs_map_remove(&ctx->nodes, n->entity);
    layout_node_t **list = ecs_vec_first(&ctx->list);
    int32_t last = ecs_vec_count(&ctx->list) - 1;
    list[n->index] = list[last];
    list[n->index]->index = n->index;
    ecs_vec_remove_last(&ctx->list);
    if (n->frame == ctx->epoch) {
        layout_node_t **frame = ecs_vec_first(&ctx->frame);
        int32_t count = ecs_vec_count(&ctx->frame);
        for (int32_t i = 0; i < count; i++) {
            if (frame[i] == n) {
                frame[i] = frame[count - 1];
                ecs_vec_remove_last(&ctx->frame);
                break;
            }
        }
    }

    node_free(n);
}

/** Record that the node being computed reads from the node of an entity.
 *
 * Used for entities that are not visited through pose, bounds, basis, arrange or
 * intrinsic_bounds.
 *
 * @param ctx The layout context
 * @param e The entity that is read from
 */
static void use(
    layout_ctx_t *ctx,
    ecs_entity_t e)
{
    if (ctx->current && e && ecs_is_alive(ctx->world, e)) {
        layout_node_t *n = box_spatial_layout_node_get(ctx, e);
        if (n != ctx->current) {
            depend(ctx->current, n);
        }
    }
}

/** Make a node the node being computed, recording it as a dependency of the previous one.
 *
 * @param ctx The layout context
 * @param n The node
 * @return The node that was being computed before
 */
static layout_node_t *enter(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    layout_node_t *previous = ctx->current;
    if (previous && previous != n) {
        depend(previous, n);
    }

    ctx->current = n;

    return previous;
}

/** Report an error on a node, keeping the first error of the node.
 *
 * A node that keeps its results from an earlier pass is published again with the error. The
 * error goes away when the node that reported it is solved again.
 *
 * @param ctx The layout context
 * @param n The node
 * @param code The error
 * @param related The entity the error is about
 * @return Always false
 */
static bool fail(
    layout_ctx_t *ctx,
    layout_node_t *n,
    SpatialError code,
    ecs_entity_t related)
{
    if (!n->error.code) {
        n->error = (SpatialDiagnostic){code, related};
        frame_add(ctx, n);
    }

    if (ctx->current && ctx->current != n) {
        depend(n, ctx->current);
    }

    ctx->ok = false;

    return false;
}

/** Fail a node when a step of a dependency failed, passing on the error of the dependency.
 *
 * @param ctx The layout context
 * @param n The node
 * @param dependency The node the step was computed for
 * @param ok The result of the step
 * @return The value of ok
 */
static bool require(
    layout_ctx_t *ctx,
    layout_node_t *n,
    layout_node_t *dependency,
    bool ok)
{
    bool result = true;
    if (!ok) {
        SpatialError code = dependency->error.code ? dependency->error.code : SpatialMissingBounds;
        result = fail(ctx, n, code, dependency->entity);
    }

    return result;
}

int box_spatial_layout_axis_index(
    SpatialAxis axis)
{
    return axis == SpatialX ? 0 : axis == SpatialY ? 1 : axis == SpatialZ ? 2 : -1;
}

bool box_spatial_layout_finite3(
    const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

int box_spatial_layout_entity_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t x = *(const ecs_entity_t *)a, y = *(const ecs_entity_t *)b;

    return (x > y) - (x < y);
}

ecs_vec_t box_spatial_layout_children(
    ecs_world_t *world,
    ecs_entity_t e,
    bool prototypes)
{
    ecs_vec_t result;
    ecs_vec_init_t(NULL, &result, ecs_entity_t, 0);
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            ecs_entity_t child = it.entities[i];
            if (ecs_has(world, child, AttachmentFrame) || ecs_has_id(world, child, EcsDisabled)) {
                continue;
            }

            if (!prototypes && ecs_has_id(world, child, EcsPrefab)) {
                continue;
            }

            if (prototypes && ecs_has(world, child, ArrayInstance)) {
                continue;
            }

            *ecs_vec_append_t(NULL, &result, ecs_entity_t) = child;
        }
    }

    if (ecs_vec_count(&result) > 1) {
        qsort(ecs_vec_first(&result), (size_t)ecs_vec_count(&result), sizeof(ecs_entity_t),
              box_spatial_layout_entity_compare);
    }

    return result;
}

/** Compute the local transform of an entity from its Position, Rotation and Scale.
 *
 * @param world The world
 * @param e The entity
 * @param local The local transform
 */
static void authored_local(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 local)
{
    const Position *p = ecs_get(world, e, Position);
    const Rotation *r = ecs_get(world, e, Rotation);
    const Scale *s = ecs_get(world, e, Scale);
    glm_mat4_identity(local);
    if (r) {
        glm_euler_yxz((vec3){r->x, r->y, r->z}, local);
    }

    if (s) {
        glm_scale(local, (vec3){s->x, s->y, s->z});
    }

    if (p) {
        glm_vec3_copy((vec3){p->x, p->y, p->z}, local[3]);
    }
}

/** Compute the axis aligned bounds of a box after a transform.
 *
 * @param m The transform
 * @param min Minimum of the box
 * @param max Maximum of the box
 * @param out_min Minimum of the transformed bounds
 * @param out_max Maximum of the transformed bounds
 */
static void transform_bounds(
    mat4 m,
    const vec3 min,
    const vec3 max,
    vec3 out_min,
    vec3 out_max)
{
    glm_vec3_fill(out_min, FLT_MAX);
    glm_vec3_fill(out_max, -FLT_MAX);
    for (int i = 0; i < 8; i++) {
        vec3 p = {i & 1 ? max[0] : min[0], i & 2 ? max[1] : min[1], i & 4 ? max[2] : min[2]};
        glm_mat4_mulv3(m, p, 1, p);
        glm_vec3_minv(out_min, p, out_min);
        glm_vec3_maxv(out_max, p, out_max);
    }
}

static bool pose(layout_ctx_t *ctx, layout_node_t *n);
static bool bounds(layout_ctx_t *ctx, layout_node_t *n);
static bool arrange(layout_ctx_t *ctx, layout_node_t *n);
static bool basis(layout_ctx_t *ctx, layout_node_t *n, mat4 result);
static bool intrinsic_bounds(layout_ctx_t *ctx, layout_node_t *n);

/** Get the node of the target of a layout rule, failing when the target is not alive.
 *
 * @param ctx The layout context
 * @param n The node of the rule
 * @param target The target entity
 * @param out The node of the target, set when the target is alive
 * @return Whether the target is alive
 */
static bool target_get(
    layout_ctx_t *ctx,
    layout_node_t *n,
    ecs_entity_t target,
    layout_node_t **out)
{
    bool result = true;
    if (!target || !ecs_is_alive(ctx->world, target)) {
        result = fail(ctx, n, SpatialInvalidTarget, target);
    } else {
        *out = box_spatial_layout_node_get(ctx, target);
    }

    return result;
}

/** Compute the bounds of the shape of a node, scaled by the scale of the shape.
 *
 * Adds a Position to a shape without one, so that the layout can place it.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the entity has a shape with finite bounds
 */
static bool source_bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (ecs_has(ctx->world, n->entity, ShapeMesh) && !ecs_has(ctx->world, n->entity, Position) &&
        !ecs_has_id(ctx->world, n->entity, EcsPrefab)) {
        ecs_set(ctx->world, n->entity, Position, {0});
    }

    bool result = false;
    const ShapeMesh *s = ecs_get(ctx->world, n->entity, ShapeMesh);
    const box_mesh_t *mesh = s ? box_mesh_cache_get(box_mesh_cache_of(ctx->world), s->mesh) : NULL;
    if (mesh) {
        const LayoutOutput *out = ecs_get(ctx->world, n->entity, LayoutOutput);
        const float *scale = out && out->fitted ? out->source_scale : s->scale;
        for (int i = 0; i < 3; i++) {
            float a = (s->surface.p[0][0] ? s->min[i] : mesh->min[i]) * scale[i];
            float b = (s->surface.p[0][0] ? s->max[i] : mesh->max[i]) * scale[i];
            n->min[i] = fminf(a, b);
            n->max[i] = fmaxf(a, b);
        }

        result = box_spatial_layout_finite3(n->min) && box_spatial_layout_finite3(n->max);
    }

    return result;
}

/** Compute the bounds of a node before fitting, from its shape or else its prototype children.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the node has intrinsic bounds
 */
static bool intrinsic_bounds_solve(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    bool result = false;
    if (n->intrinsic_state == 2) {
        result = n->has_intrinsic;
    } else if (n->intrinsic_state != 1) {
        n->intrinsic_state = 1;
        layout_node_t source = {.entity = n->entity};
        if (source_bounds(ctx, &source)) {
            glm_vec3_copy(source.min, n->intrinsic_min);
            glm_vec3_copy(source.max, n->intrinsic_max);
            n->has_intrinsic = true;
        } else {
            glm_vec3_fill(n->intrinsic_min, FLT_MAX);
            glm_vec3_fill(n->intrinsic_max, -FLT_MAX);
            ecs_vec_t list = box_spatial_layout_children(ctx->world, n->entity, true);
            for (int i = 0; i < ecs_vec_count(&list); i++) {
                layout_node_t *c = box_spatial_layout_node_get(
                    ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
                if (!intrinsic_bounds(ctx, c)) {
                    continue;
                }

                mat4 local;
                vec3 min, max;
                authored_local(ctx->world, c->entity, local);
                transform_bounds(local, c->intrinsic_min, c->intrinsic_max, min, max);
                glm_vec3_minv(n->intrinsic_min, min, n->intrinsic_min);
                glm_vec3_maxv(n->intrinsic_max, max, n->intrinsic_max);
                n->has_intrinsic = true;
            }

            ecs_vec_fini_t(NULL, &list, ecs_entity_t);
        }

        n->intrinsic_state = 2;
        result = n->has_intrinsic;
    }

    return result;
}

/** Compute the local transform of a node, with the step of its array and its placement.
 *
 * @param ctx The layout context
 * @param n The node
 * @param local The local transform
 * @param place Whether to add the placement of a parent Distribute
 * @return Whether the local transform could be computed
 */
static bool local_pose(
    layout_ctx_t *ctx,
    layout_node_t *n,
    mat4 local,
    bool place)
{
    ecs_world_t *world = ctx->world;
    authored_local(world, n->entity, local);
    const ArrayInstance *instance = ecs_get(world, n->entity, ArrayInstance);
    if (instance) {
        use(ctx, instance->owner);
        const Array *a = ecs_get(world, instance->owner, Array);
        if (a) {
            int axis = box_spatial_layout_axis_index(a->axis);
            if (axis < 0) {
                fail(ctx, n, SpatialImpossible, instance->owner);
                goto error;
            }

            mat4 step;
            vec3 angles, scale;
            glm_vec3_scale(ECS_CONST_CAST(float *, a->rotation_step), (float)instance->index,
                           angles);
            glm_euler_yxz(angles, step);
            glm_mat4_mul(local, step, local);
            for (int i = 0; i < 3; i++) {
                scale[i] = 1 + a->scale_step[i] * instance->index;
            }

            if (scale[0] <= EPS || scale[1] <= EPS || scale[2] <= EPS) {
                fail(ctx, n, SpatialImpossible, instance->owner);
                goto error;
            }

            glm_scale(local, scale);
            local[3][axis] += a->spacing * instance->index;
            n->controlled = true;
        }
    }

    ecs_entity_t parent = ecs_get_parent(world, n->entity);
    if (place && parent && ecs_has(world, parent, Distribute)) {
        layout_node_t *p = box_spatial_layout_node_get(ctx, parent);
        if (!require(ctx, n, p, arrange(ctx, p))) {
            goto error;
        }
    }

    if (place && n->placed) {
        glm_vec3_add(local[3], n->placement, local[3]);
        n->controlled = true;
    }

    return true;
error:
    return false;
}

static bool anchor_frame(
    layout_ctx_t *ctx,
    layout_node_t *n,
    SpatialAnchor a,
    mat4 frame,
    bool position);

static bool orthonormal(
    mat4 input,
    mat4 output);

/** Compute the world orientation and scale of a node, without translation.
 *
 * A SpatialAlign that matches rotation takes the orientation of the anchor of its target.
 *
 * @param ctx The layout context
 * @param n The node
 * @param result The basis of the node
 * @return Whether the basis could be computed without error
 */
static bool basis_solve(
    layout_ctx_t *ctx,
    layout_node_t *n,
    mat4 result)
{
    bool ok = false;
    if (n->basis_state == 2) {
        glm_mat4_copy(n->basis, result);
        ok = !n->error.code;
        goto done;
    }

    if (n->basis_state == 1) {
        fail(ctx, n, SpatialCycle, n->entity);
        goto done;
    }

    n->basis_state = 1;
    mat4 local;
    if (!local_pose(ctx, n, local, false)) {
        goto done;
    }

    ecs_entity_t parent = ecs_get_parent(ctx->world, n->entity);
    if (parent) {
        layout_node_t *p = box_spatial_layout_node_get(ctx, parent);
        mat4 parent_basis;
        if (!require(ctx, n, p, basis(ctx, p, parent_basis))) {
            goto done;
        }

        glm_mat4_mul(parent_basis, local, n->basis);
    } else {
        glm_mat4_copy(local, n->basis);
    }

    const LayoutOutput *out = ecs_get(ctx->world, n->entity, LayoutOutput);
    if (ecs_has_id(ctx->world, n->entity, ManualTransform) && !(out && out->manual)) {
        WorldTransform t_buf;
        const WorldTransform *t = box_transform_world(ctx->world, n->entity, &t_buf);
        if (t) {
            glm_mat4_copy(ECS_CONST_CAST(vec4 *, t->m), n->basis);
        }
    }

    const SpatialAlign *value = ecs_get(ctx->world, n->entity, SpatialAlign);
    if (value) {
        SpatialAlign a = *value;
        mat4 offset;
        glm_euler_yxz(a.rotation_offset, offset);
        if (a.rotation == SpatialMatch) {
            layout_node_t *t;
            mat4 target, self, other, orientation, inverse;
            if (!target_get(ctx, n, a.target, &t) || !require(ctx, n, t, basis(ctx, t, target))) {
                goto done;
            }

            if (!anchor_frame(ctx, n, a.self, self, false) ||
                !require(ctx, n, t, anchor_frame(ctx, t, a.other, other, false))) {
                goto done;
            }

            glm_mat4_mul(target, other, target);
            if (!orthonormal(target, orientation)) {
                fail(ctx, n, SpatialZeroBounds, t->entity);
                goto done;
            }

            glm_mat4_mul(orientation, offset, orientation);
            if (a.self.face || a.self.frame || a.other.face || a.other.frame) {
                glm_vec4_negate(orientation[0]);
                glm_vec4_negate(orientation[2]);
            }

            glm_vec3_zero(self[3]);
            glm_mat4_inv(self, inverse);
            glm_mat4_mul(orientation, inverse, orientation);
            for (int i = 0; i < 3; i++) {
                glm_vec4_scale(orientation[i], glm_vec3_norm(n->basis[i]), orientation[i]);
            }

            glm_mat4_copy(orientation, n->basis);
        } else {
            glm_mat4_mul(n->basis, offset, n->basis);
        }
    }

    glm_vec3_zero(n->basis[3]);
    n->basis_state = 2;
    glm_mat4_copy(n->basis, result);
    ok = !n->error.code;
done:
    return ok;
}

/** Compute the bounds of a node and the scale a Fit applies to it.
 *
 * The bounds come from the shape of the node, or else from the bounds of its children in the
 * space of the node.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the node has bounds and no error
 */
static bool bounds_solve(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    bool ok = false;
    if (n->bounds_state == 2) {
        ok = !n->error.code && n->has_bounds;
        goto done;
    }

    if (n->bounds_state == 1) {
        fail(ctx, n, SpatialCycle, n->entity);
        goto done;
    }

    n->bounds_state = 1;
    intrinsic_bounds(ctx, n);
    ecs_world_t *world = ctx->world;
    n->has_bounds = source_bounds(ctx, n);
    const Fit *fit = ecs_get(world, n->entity, Fit);
    if (!n->has_bounds) {
        if (fit) {
            fail(ctx, n, SpatialMissingBounds, n->entity);
            goto done;
        }

        ecs_vec_t list = box_spatial_layout_children(world, n->entity, false);
        glm_vec3_fill(n->min, FLT_MAX);
        glm_vec3_fill(n->max, -FLT_MAX);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            layout_node_t *c =
                box_spatial_layout_node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
            if (!bounds(ctx, c)) {
                if (c->error.code) {
                    require(ctx, n, c, false);
                }

                continue;
            }

            mat4 relative;
            const LayoutOutput *output = ecs_get(world, c->entity, LayoutOutput);
            bool manual =
                ecs_has_id(world, c->entity, ManualTransform) && !(output && output->manual);
            if (ecs_has(world, c->entity, SpatialAlign) || manual) {
                if (!pose(ctx, n) || !require(ctx, n, c, pose(ctx, c))) {
                    break;
                }

                if (fabsf(glm_mat4_det(n->pose)) < EPS) {
                    fail(ctx, n, SpatialZeroBounds, n->entity);
                    break;
                }

                mat4 inv;
                glm_mat4_inv(n->pose, inv);
                glm_mat4_mul(inv, c->pose, relative);
            } else if (!require(ctx, n, c, local_pose(ctx, c, relative, true))) {
                break;
            }

            vec3 min, max;
            transform_bounds(relative, c->min, c->max, min, max);
            glm_vec3_minv(n->min, min, n->min);
            glm_vec3_maxv(n->max, max, n->max);
            n->has_bounds = true;
        }

        ecs_vec_fini_t(NULL, &list, ecs_entity_t);
    }

    if (fit) {
        Fit f = *fit;
        int axes = f.axes ? f.axes : SpatialXYZ;
        layout_node_t *t;
        if (axes < 1 || axes > 7 || f.mode < SpatialStretch || f.mode > SpatialCover ||
            !box_spatial_layout_finite3(f.padding)) {
            fail(ctx, n, SpatialImpossible, n->entity);
            goto done;
        }

        if (!target_get(ctx, n, f.target, &t) || !require(ctx, n, t, bounds(ctx, t))) {
            goto done;
        }

        mat4 self_basis, target_basis;
        if (!basis(ctx, n, self_basis) || !require(ctx, n, t, basis(ctx, t, target_basis))) {
            goto done;
        }

        float uniform = f.mode == SpatialCover ? 0 : FLT_MAX;
        for (int i = 0; i < 3; i++) {
            if (!(axes & (1 << i))) {
                continue;
            }

            float size = n->max[i] - n->min[i];
            float self_scale = glm_vec3_norm(self_basis[i]);
            float target_scale = glm_vec3_norm(target_basis[i]);
            float target_size = t->max[i] - t->min[i];
            if (target_size <= EPS) {
                fail(ctx, n, SpatialZeroBounds, t->entity);
                goto done;
            }

            float available = (target_size - 2 * f.padding[i]) * target_scale;
            if (self_scale <= EPS || target_scale <= EPS) {
                fail(ctx, n, SpatialZeroBounds, t->entity);
                goto done;
            }

            size *= self_scale;
            if (size <= EPS) {
                fail(ctx, n, SpatialZeroBounds, n->entity);
                goto done;
            }

            if (available <= EPS || f.padding[i] < 0) {
                fail(ctx, n, SpatialImpossible, t->entity);
                goto done;
            }

            n->fit[i] = available / size;
            uniform =
                f.mode == SpatialCover ? fmaxf(uniform, n->fit[i]) : fminf(uniform, n->fit[i]);
        }

        for (int i = 0; i < 3; i++) {
            if ((axes & (1 << i)) && f.mode != SpatialStretch) {
                n->fit[i] = uniform;
            }

            n->min[i] *= n->fit[i];
            n->max[i] *= n->fit[i];
        }
    }

    n->bounds_state = 2;
    ok = !n->error.code && n->has_bounds;
done:
    return ok;
}

/** Compute the frame of an anchor in the space of a node.
 *
 * The frame is an AttachmentFrame child, or else a point on the bounds of the node with the
 * orientation of an optional face.
 *
 * @param ctx The layout context
 * @param n The node
 * @param a The anchor
 * @param frame The frame of the anchor
 * @param position Whether to compute the position of the frame, which needs the bounds
 * @return Whether the anchor is valid
 */
static bool anchor_frame(
    layout_ctx_t *ctx,
    layout_node_t *n,
    SpatialAnchor a,
    mat4 frame,
    bool position)
{
    bool ok = false;
    glm_mat4_identity(frame);
    if (a.frame) {
        use(ctx, a.frame);
        if (!ecs_is_alive(ctx->world, a.frame) ||
            ecs_get_parent(ctx->world, a.frame) != n->entity) {
            fail(ctx, n, SpatialInvalidAnchor, a.frame);
            goto done;
        }

        const AttachmentFrame *attachment = ecs_get(ctx->world, a.frame, AttachmentFrame);
        if (!attachment || !box_spatial_layout_finite3(attachment->position) ||
            !box_spatial_layout_finite3(attachment->rotation)) {
            fail(ctx, n, SpatialInvalidAnchor, a.frame);
            goto done;
        }

        glm_euler_yxz(ECS_CONST_CAST(float *, attachment->rotation), frame);
        glm_vec3_copy(ECS_CONST_CAST(float *, attachment->position), frame[3]);
        ok = true;
        goto done;
    }

    if (position && !bounds(ctx, n)) {
        fail(ctx, n, n->error.code ? n->error.code : SpatialMissingBounds, n->entity);
        goto done;
    }

    AnchorPoint points[] = {a.x, a.y, a.z};
    for (int i = 0; i < 3; i++) {
        if (points[i] < SpatialCenter || points[i] > SpatialMax) {
            fail(ctx, n, SpatialInvalidAnchor, n->entity);
            goto done;
        }

        if (position) {
            frame[3][i] = points[i] == SpatialMin   ? n->min[i]
                          : points[i] == SpatialMax ? n->max[i]
                                                    : (n->min[i] + n->max[i]) * 0.5f;
        }
    }

    if (a.face) {
        vec3 normal = {0}, up = {0, 1, 0};
        int axis;
        float sign;
        switch (a.face) {
        case SpatialFront:
            axis = 2;
            sign = 1;
            break;
        case SpatialBack:
            axis = 2;
            sign = -1;
            break;
        case SpatialTop:
            axis = 1;
            sign = 1;
            up[1] = 0;
            up[2] = -1;
            break;
        case SpatialBottom:
            axis = 1;
            sign = -1;
            up[1] = 0;
            up[2] = 1;
            break;
        case SpatialRight:
            axis = 0;
            sign = 1;
            break;
        case SpatialLeft:
            axis = 0;
            sign = -1;
            break;
        default:
            fail(ctx, n, SpatialInvalidAnchor, n->entity);
            goto done;
        }

        normal[axis] = sign;
        if (position) {
            frame[3][axis] = sign > 0 ? n->max[axis] : n->min[axis];
        }

        glm_vec3_copy(normal, frame[2]);
        glm_vec3_cross(up, normal, frame[0]);
        glm_vec3_copy(up, frame[1]);
    }

    ok = true;
done:
    return ok;
}

/** Compute an orthonormal frame from a transform, keeping its Z axis and translation.
 *
 * The Z axis is the normal of the XY plane of the transform, and the X axis is projected onto
 * the plane orthogonal to it.
 *
 * @param input The transform
 * @param output The orthonormal frame
 * @return Whether the transform is not singular
 */
static bool orthonormal(
    mat4 input,
    mat4 output)
{
    bool ok = !(fabsf(glm_mat4_det(input)) < EPS);
    if (ok) {
        mat4 inverse;
        glm_mat4_inv(input, inverse);
        glm_mat4_transpose(inverse);
        glm_mat4_identity(output);
        glm_vec3_copy(inverse[2], output[2]);
        glm_vec3_normalize(output[2]);
        glm_vec3_copy(input[0], output[0]);
        glm_vec3_muladds(output[2], -glm_vec3_dot(output[0], output[2]), output[0]);
        glm_vec3_normalize(output[0]);
        glm_vec3_cross(output[2], output[0], output[1]);
        glm_vec3_copy(input[3], output[3]);
    }

    return ok;
}

/** Compute the world transform of a node.
 *
 * The transform of the parent is combined with the local pose, unless a ManualTransform or a
 * SpatialAlign decides it.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the pose could be computed without error
 */
static bool pose_solve(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    bool ok = false;
    if (n->pose_state == 2) {
        ok = !n->error.code;
        goto done;
    }

    if (n->pose_state == 1) {
        fail(ctx, n, SpatialCycle, n->entity);
        goto done;
    }

    n->pose_state = 1;
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = n->entity, parent = ecs_get_parent(world, e);
    mat4 base = GLM_MAT4_IDENTITY_INIT, local;
    if (parent) {
        layout_node_t *p = box_spatial_layout_node_get(ctx, parent);
        if (!require(ctx, n, p, pose(ctx, p))) {
            goto done;
        }

        glm_mat4_copy(p->pose, base);
        if (ecs_has(world, parent, Distribute)) {
            if (!require(ctx, n, p, arrange(ctx, p))) {
                goto done;
            }
        }
    }

    const LayoutOutput *out = ecs_get(world, e, LayoutOutput);
    const SpatialAlign *align = ecs_get(world, e, SpatialAlign);
    const ArrayInstance *instance = ecs_get(world, e, ArrayInstance);
    if (!local_pose(ctx, n, local, true)) {
        goto done;
    }

    glm_mat4_mul(base, local, n->pose);
    if (ecs_has_id(world, e, ManualTransform) && !(out && out->manual)) {
        if (align || n->controlled) {
            fail(ctx, n, SpatialConflictingLayout, e);
            goto done;
        }

        const WorldTransform *manual = ecs_get(world, e, WorldTransform);
        if (manual) {
            glm_mat4_copy(ECS_CONST_CAST(vec4 *, manual->m), n->pose);
        }
    }

    if (align) {
        SpatialAlign a = *align;
        layout_node_t *t;
        n->controlled = true;
        if (n->placed || instance) {
            fail(ctx, n, SpatialConflictingLayout, e);
            goto done;
        }

        if (a.rotation < SpatialKeep || a.rotation > SpatialMatch ||
            !box_spatial_layout_finite3(a.offset) ||
            !box_spatial_layout_finite3(a.rotation_offset)) {
            fail(ctx, n, SpatialImpossible, e);
            goto done;
        }

        if (!target_get(ctx, n, a.target, &t) || !require(ctx, n, t, pose(ctx, t))) {
            goto done;
        }

        mat4 self, other, target, frame;
        if (!anchor_frame(ctx, n, a.self, self, true) ||
            !require(ctx, n, t, anchor_frame(ctx, t, a.other, other, true))) {
            goto done;
        }

        glm_mat4_mul(t->pose, other, target);
        if (!orthonormal(target, frame)) {
            fail(ctx, n, SpatialZeroBounds, t->entity);
            goto done;
        }

        if (!basis(ctx, n, n->pose)) {
            goto done;
        }

        vec3 desired, point;
        glm_mat4_mulv3(frame, a.offset, 1, desired);
        glm_mat4_mulv3(n->pose, self[3], 0, point);
        glm_vec3_sub(desired, point, n->pose[3]);
    }

    for (int i = 0; i < 4; i++) {
        if (!box_spatial_layout_finite3(n->pose[i])) {
            fail(ctx, n, SpatialImpossible, e);
            goto done;
        }
    }

    n->pose_state = 2;
    ok = !n->error.code;
done:
    return ok;
}

/** Place the children of a Distribute node along its axis.
 *
 * @param ctx The layout context
 * @param n The node of the Distribute entity
 * @return Whether the children could be placed without error
 */
static bool arrange_solve(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    bool ok = false;
    if (n->arrange_state == 2) {
        ok = !n->error.code;
        goto done;
    }

    if (n->arrange_state == 1) {
        fail(ctx, n, SpatialCycle, n->entity);
        goto done;
    }

    n->arrange_state = 1;
    n->arranged = true;
    Distribute d = *ecs_get(ctx->world, n->entity, Distribute);
    int axis = box_spatial_layout_axis_index(d.axis);
    if (axis < 0 || d.span < 0 || d.gap < 0 || !isfinite(d.span) || !isfinite(d.gap) ||
        d.mode < SpatialSpaceBetween || d.mode > SpatialFixedGap ||
        ecs_has(ctx->world, n->entity, Array)) {
        fail(ctx, n, SpatialImpossible, n->entity);
        goto done;
    }

    ecs_vec_t list = box_spatial_layout_children(ctx->world, n->entity, false);
    int count = ecs_vec_count(&list);
    float *lo = count ? ecs_os_malloc_n(float, count) : NULL;
    float *hi = count ? ecs_os_malloc_n(float, count) : NULL;
    float total = 0;
    for (int i = 0; i < count; i++) {
        layout_node_t *c =
            box_spatial_layout_node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
        if (ecs_has(ctx->world, c->entity, SpatialAlign) || !require(ctx, n, c, bounds(ctx, c))) {
            fail(ctx, n, c->error.code ? c->error.code : SpatialConflictingLayout, c->entity);
            break;
        }

        mat4 local;
        vec3 min, max;
        authored_local(ctx->world, c->entity, local);
        transform_bounds(local, c->min, c->max, min, max);
        lo[i] = min[axis];
        hi[i] = max[axis];
        total += hi[i] - lo[i];
    }

    float free_space = d.span - total, gap = d.gap, cursor = -d.span * 0.5f;
    if (!n->error.code && count) {
        if (d.mode == SpatialFixedGap) {
            float used = total + gap * (count - 1);
            if (d.span > 0 && used > d.span + EPS) {
                fail(ctx, n, SpatialImpossible, n->entity);
            }

            cursor = -used * 0.5f;
        } else if (free_space < -EPS) {
            fail(ctx, n, SpatialImpossible, n->entity);
        } else if (d.mode == SpatialSpaceBetween) {
            gap = count > 1 ? free_space / (count - 1) : 0;
            if (count == 1) {
                cursor = -total * 0.5f;
            }
        } else if (d.mode == SpatialSpaceAround) {
            gap = free_space / count;
            cursor += gap * 0.5f;
        } else {
            gap = free_space / (count + 1);
            cursor += gap;
        }

        for (int i = 0; !n->error.code && i < count; i++) {
            layout_node_t *c =
                box_spatial_layout_node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
            c->placement[axis] = cursor - lo[i];
            c->placed = true;
            c->requested = true;
            cursor += hi[i] - lo[i] + gap;
        }
    }

    ecs_os_free(lo);
    ecs_os_free(hi);
    ecs_vec_fini_t(NULL, &list, ecs_entity_t);
    n->arrange_state = 2;
    ok = !n->error.code;
done:
    return ok;
}

/** Solve the pose of a node, recording the calling node as a user of it.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the pose could be computed without error
 */
static bool pose(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    layout_node_t *previous = enter(ctx, n);
    bool ok = pose_solve(ctx, n);
    ctx->current = previous;

    return ok;
}

/** Solve the bounds of a node, recording the calling node as a user of it.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the node has bounds and no error
 */
static bool bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    layout_node_t *previous = enter(ctx, n);
    bool ok = bounds_solve(ctx, n);
    ctx->current = previous;

    return ok;
}

/** Arrange the children of a node, recording the calling node as a user of it.
 *
 * @param ctx The layout context
 * @param n The node of the Distribute entity
 * @return Whether the children could be placed without error
 */
static bool arrange(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    layout_node_t *previous = enter(ctx, n);
    bool ok = arrange_solve(ctx, n);
    ctx->current = previous;

    return ok;
}

/** Solve the intrinsic bounds of a node, recording the calling node as a user of it.
 *
 * @param ctx The layout context
 * @param n The node
 * @return Whether the node has intrinsic bounds
 */
static bool intrinsic_bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    layout_node_t *previous = enter(ctx, n);
    bool ok = intrinsic_bounds_solve(ctx, n);
    ctx->current = previous;

    return ok;
}

/** Solve the basis of a node, recording the calling node as a user of it.
 *
 * @param ctx The layout context
 * @param n The node
 * @param result The basis of the node
 * @return Whether the basis could be computed without error
 */
static bool basis(
    layout_ctx_t *ctx,
    layout_node_t *n,
    mat4 result)
{
    layout_node_t *previous = enter(ctx, n);
    bool ok = basis_solve(ctx, n, result);
    ctx->current = previous;

    return ok;
}

void box_spatial_layout_set_changed(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id,
    size_t size,
    const void *value)
{
    const void *old = ecs_get_id(world, e, id);
    if (!old || memcmp(old, value, size)) {
        ecs_set_id(world, e, id, size, value);
    }
}

/** Write the results of a solved node to its entity.
 *
 * Writes the fitted scale, the transform, the resolved bounds and the diagnostic, and removes
 * what the layout wrote before when the entity is no longer managed. The entity must be alive.
 *
 * @param ctx The layout context
 * @param n The node
 */
static void publish(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = n->entity;
    bool managed = ecs_has(world, e, Fit) || ecs_has(world, e, SpatialAlign) ||
                   ecs_has(world, e, Array) || ecs_has(world, e, Distribute) ||
                   ecs_has(world, e, ArrayInstance) || n->placed;
    bool fitted = !n->error.code && ecs_has(world, e, Fit) && n->bounds_state == 2;
    bool controlled = !n->error.code && n->controlled && n->pose_state == 2;
    const LayoutOutput *previous = ecs_get(world, e, LayoutOutput);
    LayoutOutput out = previous ? *previous : (LayoutOutput){0};
    if (fitted || out.fitted) {
        ShapeMesh *shape = ecs_get_mut(world, e, ShapeMesh);
        if (shape) {
            if (!out.fitted) {
                glm_vec3_copy(shape->scale, out.source_scale);
            }

            vec3 scale;
            glm_vec3_copy(out.source_scale, scale);
            if (fitted) {
                glm_vec3_mul(scale, n->fit, scale);
            }

            if (memcmp(scale, shape->scale, sizeof(vec3))) {
                glm_vec3_copy(scale, shape->scale);
                ecs_modified(world, e, ShapeMesh);
            }
        }

        out.fitted = fitted;
    }

    if (controlled) {
        if (!ecs_has_id(world, e, ManualTransform)) {
            ecs_add_id(world, e, ManualTransform);
            out.manual = true;
        }

        WorldTransform transform;
        glm_mat4_copy(n->pose, transform.m);
        box_spatial_layout_set_changed(
            world, e, ecs_id(WorldTransform), sizeof(transform), &transform);
    } else if (out.manual) {
        ecs_remove_id(world, e, ManualTransform);
        if (ecs_has(world, e, Position)) {
            ecs_modified(world, e, Position);
        } else {
            ecs_remove(world, e, WorldTransform);
        }

        out.manual = false;
    }

    if (managed) {
        box_spatial_layout_set_changed(world, e, ecs_id(LayoutOutput), sizeof(out), &out);
    } else {
        ecs_remove(world, e, LayoutOutput);
    }

    if (managed && n->has_bounds && !n->error.code) {
        IntrinsicBounds intrinsic;
        ResolvedBounds resolved;
        glm_vec3_copy(n->intrinsic_min, intrinsic.min);
        glm_vec3_copy(n->intrinsic_max, intrinsic.max);
        glm_vec3_copy(n->min, resolved.min);
        glm_vec3_copy(n->max, resolved.max);
        box_spatial_layout_set_changed(
            world, e, ecs_id(IntrinsicBounds), sizeof(intrinsic), &intrinsic);
        box_spatial_layout_set_changed(
            world, e, ecs_id(ResolvedBounds), sizeof(resolved), &resolved);
    } else {
        ecs_remove(world, e, IntrinsicBounds);
        ecs_remove(world, e, ResolvedBounds);
    }

    const Box *box = ecs_get(world, e, Box);
    if (box && fitted) {
        ResolvedBox result = {box->x * n->fit[0], box->y * n->fit[1], box->z * n->fit[2]};
        box_spatial_layout_set_changed(world, e, ecs_id(ResolvedBox), sizeof(result), &result);
    } else {
        ecs_remove(world, e, ResolvedBox);
    }

    if (n->error.code) {
        const SpatialDiagnostic *old = ecs_get(world, e, SpatialDiagnostic);
        if (!old || old->code != n->error.code || old->related != n->error.related) {
            static const char *messages[] = {"ok",
                                             "invalid target",
                                             "missing geometry bounds",
                                             "zero-sized or singular bounds",
                                             "dependency cycle",
                                             "impossible layout",
                                             "invalid attachment anchor",
                                             "conflicting layout drivers"};
            char *path = ecs_get_path(world, e);
            ecs_err("spatial layout '%s': %s (dependency %llu)", path, messages[n->error.code],
                    (unsigned long long)n->error.related);
            ecs_os_free(path);
            ecs_set_ptr(world, e, SpatialDiagnostic, &n->error);
        }
    } else {
        ecs_remove(world, e, SpatialDiagnostic);
    }
}

/** Add a node to the nodes invalidated in the current pass.
 *
 * @param ctx The layout context
 * @param n The node
 */
static void mark(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->mark != ctx->epoch) {
        n->mark = ctx->epoch;
        *ecs_vec_append_t(NULL, &ctx->queue, layout_node_t *) = n;
    }
}

/** Fill the users of every node, by node index.
 *
 * The users of node i are users[offsets[i + 1]] up to users[offsets[i + 2]]. offsets[i + 1]
 * counts up to the end of the range of node i and is counted down while the range is filled.
 *
 * @param ctx The layout context
 */
static void users_build(
    layout_ctx_t *ctx)
{
    int32_t count = ecs_vec_count(&ctx->list);
    layout_node_t **list = ecs_vec_first(&ctx->list);
    ecs_vec_set_count_t(NULL, &ctx->offsets, int32_t, count + 2);
    int32_t *offsets = ecs_vec_first(&ctx->offsets);
    ecs_os_memset_n(offsets, 0, int32_t, count + 2);
    for (int32_t i = 0; i < count; i++) {
        layout_node_t **deps = ecs_vec_first(&list[i]->deps);
        for (int32_t j = 0; j < ecs_vec_count(&list[i]->deps); j++) {
            offsets[deps[j]->index + 1]++;
        }
    }

    for (int32_t i = 1; i <= count; i++) {
        offsets[i] += offsets[i - 1];
    }

    int32_t total = offsets[count];
    offsets[count + 1] = total;
    ecs_vec_set_count_t(NULL, &ctx->users, int32_t, total);
    int32_t *users = ecs_vec_first(&ctx->users);
    for (int32_t i = 0; i < count; i++) {
        layout_node_t **deps = ecs_vec_first(&list[i]->deps);
        for (int32_t j = 0; j < ecs_vec_count(&list[i]->deps); j++) {
            users[--offsets[deps[j]->index + 1]] = i;
        }
    }
}

/** Reset the nodes of the entities with changed inputs and every node that read from them.
 *
 * Reset nodes are solved and published again, and nodes of deleted entities are removed. The
 * children of a distribution are reset with it, as they do not read from it while placed by an
 * arrange of an earlier pass.
 *
 * @param ctx The layout context
 */
static void invalidate(
    layout_ctx_t *ctx)
{
    ecs_world_t *world = ctx->world;
    ecs_vec_clear(&ctx->queue);
    int32_t seed_count = ecs_vec_count(&ctx->seeds);
    ecs_entity_t *seeds = ecs_vec_first(&ctx->seeds);
    for (int32_t i = 0; i < seed_count; i++) {
        layout_node_t *n = ecs_map_get_deref(&ctx->nodes, layout_node_t, seeds[i]);
        if (n) {
            mark(ctx, n);
        }
    }

    if (ecs_vec_count(&ctx->queue)) {
        users_build(ctx);
    }

    layout_node_t **list = ecs_vec_first(&ctx->list);
    const int32_t *offsets = ecs_vec_first(&ctx->offsets);
    const int32_t *users = ecs_vec_first(&ctx->users);
    for (int32_t q = 0; q < ecs_vec_count(&ctx->queue); q++) {
        layout_node_t *n = *ecs_vec_get_t(&ctx->queue, layout_node_t *, q);
        for (int32_t u = offsets[n->index + 1]; u < offsets[n->index + 2]; u++) {
            mark(ctx, list[users[u]]);
        }

        ecs_entity_t e = n->entity;
        if (ecs_is_alive(world, e) && (n->arranged || ecs_has(world, e, Distribute))) {
            ecs_iter_t it = ecs_children(world, e);
            while (ecs_children_next(&it)) {
                for (int i = 0; i < it.count; i++) {
                    layout_node_t *c =
                        ecs_map_get_deref(&ctx->nodes, layout_node_t, it.entities[i]);
                    if (c) {
                        mark(ctx, c);
                    }
                }
            }
        }
    }

    for (int32_t q = 0; q < ecs_vec_count(&ctx->queue); q++) {
        layout_node_t *n = *ecs_vec_get_t(&ctx->queue, layout_node_t *, q);
        if (!ecs_is_alive(world, n->entity)) {
            node_remove(ctx, n);
        } else if (n->epoch != ctx->epoch) {
            node_reset(ctx, n);
        }
    }

    for (int32_t i = 0; i < seed_count; i++) {
        ecs_entity_t e = seeds[i];
        if (!ecs_map_get(&ctx->nodes, e) && ecs_is_alive(world, e) && wants(world, e)) {
            box_spatial_layout_node_get(ctx, e);
        }
    }

    ecs_vec_clear(&ctx->seeds);
}

/** Test whether a world has layout rules or layout output.
 *
 * @param world The world
 * @return Whether an entity has a layout rule or layout output
 */
static bool layout_used(
    ecs_world_t *world)
{
    return ecs_count(world, Fit) || ecs_count(world, SpatialAlign) || ecs_count(world, Array) ||
           ecs_count(world, Distribute) || ecs_count(world, ArrayInstance) ||
           ecs_count(world, LayoutOutput);
}

bool box_spatial_layout_update(
    ecs_world_t *world)
{
    bool ok = true;
    const LayoutContext *singleton = ecs_singleton_get(world, LayoutContext);
    if (!singleton) {
        goto done;
    }

    layout_ctx_t *ctx = singleton->ptr;
    if (ctx->writing) {
        ok = false;
        goto done;
    }

    ctx->solved = 0;
    ctx->synced = 0;
    if (!ecs_vec_count(&ctx->seeds) && !ecs_vec_count(&ctx->dynamic) && !ctx->arrays_dirty) {
        ok = ctx->ok;
        goto done;
    }

    if (!ecs_map_count(&ctx->nodes) && !ctx->arrays_dirty && !layout_used(world)) {
        ecs_vec_clear(&ctx->seeds);
        ecs_vec_clear(&ctx->dynamic);
        ctx->active = false;
        ctx->ok = true;
        goto done;
    }

    ctx->epoch++;
    ecs_vec_clear(&ctx->frame);
    int32_t seed_count = ecs_vec_count(&ctx->seeds);
    for (int32_t i = 0; i < ecs_vec_count(&ctx->dynamic); i++) {
        *ecs_vec_append_t(NULL, &ctx->seeds, ecs_entity_t) =
            *ecs_vec_get_t(&ctx->dynamic, ecs_entity_t, i);
    }

    ecs_vec_clear(&ctx->dynamic);
    box_spatial_layout_arrays(ctx, seed_count);
    invalidate(ctx);
    if (!ecs_vec_count(&ctx->frame) && !ecs_vec_count(&ctx->queue)) {
        ok = ctx->ok;
        goto done;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int32_t i = 0; i < ecs_vec_count(&ctx->frame); i++) {
            layout_node_t *n = *ecs_vec_get_t(&ctx->frame, layout_node_t *, i);
            if (!n->requested) {
                continue;
            }

            if (!pass && ecs_has(world, n->entity, Distribute)) {
                arrange(ctx, n);
            }

            pose(ctx, n);
            bounds(ctx, n);
        }
    }

    ctx->writing = true;
    for (int32_t i = 0; i < ecs_vec_count(&ctx->frame); i++) {
        layout_node_t *n = *ecs_vec_get_t(&ctx->frame, layout_node_t *, i);
        if (n->requested && ecs_is_alive(world, n->entity)) {
            publish(ctx, n);
        }
    }

    ctx->writing = false;
    ctx->ok = true;
    for (int32_t i = 0; i < ecs_vec_count(&ctx->list); i++) {
        layout_node_t *n = *ecs_vec_get_t(&ctx->list, layout_node_t *, i);
        if (n->requested && n->error.code) {
            ctx->ok = false;
        }

        if (n->epoch == ctx->epoch) {
            ctx->solved++;
        }
    }

    ok = ctx->ok;
done:
    return ok;
}

void box_spatial_layout_stats(
    const ecs_world_t *world,
    box_spatial_layout_stats_t *stats)
{
    *stats = (box_spatial_layout_stats_t){0};
    const LayoutContext *singleton = ecs_singleton_get(world, LayoutContext);
    if (singleton) {
        stats->nodes = ecs_vec_count(&singleton->ptr->list);
        stats->solved = singleton->ptr->solved;
        stats->synced = singleton->ptr->synced;
    }
}

/** Observer that keeps the source scale of a fitted shape when the shape is set by the user.
 *
 * @param it The iterator
 */
static void shape_changed(
    ecs_iter_t *it)
{
    layout_ctx_t *ctx = it->ctx;
    if (!ctx->writing) {
        for (int i = 0; i < it->count; i++) {
            LayoutOutput *out = ecs_get_mut(it->world, it->entities[i], LayoutOutput);
            const ShapeMesh *shape = ecs_get(it->world, it->entities[i], ShapeMesh);
            if (out && shape) {
                glm_vec3_copy(ECS_CONST_CAST(float *, shape->scale), out->source_scale);
            }
        }
    }
}

/** Get the parent of an entity while its hierarchy changes.
 *
 * A Parent component is added before its value is assigned, and points to a parent that is
 * being deleted while its children are.
 *
 * @param world The world
 * @param e The entity
 * @return The parent of the entity, 0 when it has none or its parent is not alive
 */
static ecs_entity_t event_parent(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsParent *p = ecs_get(world, e, EcsParent);

    return !p ? ecs_get_parent(world, e) : p->value && ecs_is_alive(world, p->value) ? p->value : 0;
}

/** Observer that records the entities whose layout inputs changed.
 *
 * Adding or removing a component or a pair also changes the children the parent of the entity
 * measures or lays out, and so does setting the Parent component. Until an entity gets a layout
 * rule nothing is solved, and nothing needs to be recorded.
 *
 * @param it The iterator
 */
static void input_changed(
    ecs_iter_t *it)
{
    layout_ctx_t *ctx = it->ctx;
    ecs_id_t id = it->event_id;
    bool structure = it->event != EcsOnSet || id == ecs_id(EcsParent);
    if (id == ecs_id(Array) || id == ecs_id(ArrayInstance)) {
        ctx->arrays_dirty = true;
    }

    ctx->active |= id == ecs_id(Fit) || id == ecs_id(SpatialAlign) || id == ecs_id(Array) ||
                   id == ecs_id(Distribute) || id == ecs_id(ArrayInstance);
    if (ctx->active) {
        for (int i = 0; i < it->count; i++) {
            ecs_entity_t e = it->entities[i];
            *ecs_vec_append_t(NULL, &ctx->seeds, ecs_entity_t) = e;
            ecs_entity_t parent = structure ? event_parent(it->world, e) : 0;
            if (parent) {
                *ecs_vec_append_t(NULL, &ctx->seeds, ecs_entity_t) = parent;
            }
        }
    }
}

/** System that solves the layout.
 *
 * @param it The iterator
 */
static void layout_run(
    ecs_iter_t *it)
{
    box_spatial_layout_update(it->world);
}

/** Release a reference to the layout context, freeing it with the last reference.
 *
 * @param ptr The layout context
 */
static void layout_free(
    void *ptr)
{
    layout_ctx_t *ctx = ptr;
    if (--ctx->refs == 0) {
        ecs_map_iter_t it = ecs_map_iter(&ctx->nodes);
        while (ecs_map_next(&it)) {
            node_free(ecs_map_ptr(&it));
        }

        ecs_map_fini(&ctx->nodes);
        ecs_vec_fini_t(NULL, &ctx->list, layout_node_t *);
        ecs_vec_fini_t(NULL, &ctx->frame, layout_node_t *);
        ecs_vec_fini_t(NULL, &ctx->seeds, ecs_entity_t);
        ecs_vec_fini_t(NULL, &ctx->dynamic, ecs_entity_t);
        ecs_vec_fini_t(NULL, &ctx->queue, layout_node_t *);
        ecs_vec_fini_t(NULL, &ctx->offsets, int32_t);
        ecs_vec_fini_t(NULL, &ctx->users, int32_t);
        ecs_vec_fini_t(NULL, &ctx->roots, ecs_entity_t);
        ecs_os_free(ctx);
    }
}

/** Create an observer that records changes of a layout input.
 *
 * @param world The world
 * @param ctx The layout context
 * @param id The input
 * @param set Whether setting the input is a change, besides adding and removing it
 */
static void observe(
    ecs_world_t *world,
    layout_ctx_t *ctx,
    ecs_id_t id,
    bool set)
{
    ecs_observer_desc_t desc = {.query.terms = {{.id = id}},
                                .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
                                .events = {EcsOnAdd, EcsOnRemove},
                                .callback = input_changed,
                                .ctx = ctx,
                                .ctx_free = layout_free};
    if (set) {
        desc.events[2] = EcsOnSet;
    }

    ecs_observer_init(world, &desc);
    ctx->refs++;
}

void box_spatial_layout_init(
    ecs_world_t *world)
{
    box_spatial_layout_types(world);
    ECS_COMPONENT_DEFINE(world, LayoutOutput);
    ECS_COMPONENT_DEFINE(world, LayoutContext);
    ECS_COMPONENT_DEFINE(world, PrototypeSource);
    ECS_TAG_DEFINE(world, ArrayPrototype);
    ecs_add_pair(world, ecs_id(PrototypeSource), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(LayoutOutput), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ArrayInstance), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ArrayPrototype, EcsOnInstantiate, EcsDontInherit);
    layout_ctx_t *ctx = ecs_os_calloc_t(layout_ctx_t);
    ctx->world = world;
    ctx->ok = true;
    ecs_map_init(&ctx->nodes, NULL);
    ecs_vec_init_t(NULL, &ctx->list, layout_node_t *, 0);
    ecs_vec_init_t(NULL, &ctx->frame, layout_node_t *, 0);
    ecs_vec_init_t(NULL, &ctx->seeds, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ctx->dynamic, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ctx->queue, layout_node_t *, 0);
    ecs_vec_init_t(NULL, &ctx->offsets, int32_t, 0);
    ecs_vec_init_t(NULL, &ctx->users, int32_t, 0);
    ecs_vec_init_t(NULL, &ctx->roots, ecs_entity_t, 0);
    ecs_singleton_set(world, LayoutContext, {ctx});
    ecs_observer(world, {.query.terms = {{.id = ecs_id(ShapeMesh)}},
                         .events = {EcsOnSet},
                         .callback = shape_changed,
                         .ctx = ctx,
                         .ctx_free = layout_free});
    ctx->refs++;
    ecs_id_t components[] = {ecs_id(Fit),        ecs_id(SpatialAlign),    ecs_id(Array),
                             ecs_id(Distribute), ecs_id(AttachmentFrame), ecs_id(ArrayInstance),
                             ecs_id(Position),   ecs_id(Rotation),        ecs_id(Scale),
                             ecs_id(ShapeMesh),  ecs_id(Box),             ecs_id(LayoutOutput),
                             ecs_id(EcsParent),  ecs_pair(ecs_id(EcsIdentifier), EcsName)};
    ecs_id_t tags[] = {ecs_pair(EcsChildOf, EcsWildcard), ecs_pair(EcsIsA, EcsWildcard),
                       EcsDisabled, EcsPrefab, ManualTransform, DynamicTransform};
    for (size_t i = 0; i < sizeof(components) / sizeof(components[0]); i++) {
        observe(world, ctx, components[i], true);
    }

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        observe(world, ctx, tags[i], false);
    }

    ecs_observer(world, {.query.terms = {{.id = ecs_id(WorldTransform)},
                                         {.id = ManualTransform, .inout = EcsInOutFilter}},
                         .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
                         .events = {EcsOnSet},
                         .callback = input_changed,
                         .ctx = ctx,
                         .ctx_free = layout_free});
    ctx->refs++;
    ecs_system(world, {.entity = ecs_entity(world, {.name = "Resolve"}),
                       .phase = EcsPostUpdate,
                       .run = layout_run,
                       .immediate = true,
                       .ctx = ctx,
                       .ctx_free = layout_free});
    ctx->refs++;
}
