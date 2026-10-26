#include "layout.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EPS 0.000001f

typedef struct LayoutOutput {
    vec3 source_scale;
    bool fitted;
    bool manual;
} LayoutOutput;

static ECS_COMPONENT_DECLARE(LayoutOutput);
static ECS_TAG_DECLARE(ArrayPrototype);
typedef struct PrototypeSource {
    ecs_entity_t entity;
} PrototypeSource;
static ECS_COMPONENT_DECLARE(PrototypeSource);

typedef struct layout_node_t {
    ecs_entity_t entity;
    int pose_state, bounds_state, arrange_state, intrinsic_state, basis_state;
    mat4 pose, basis;
    vec3 min, max, intrinsic_min, intrinsic_max, fit;
    vec3 placement;
    bool has_bounds, has_intrinsic, controlled, placed, requested, array_done;
    SpatialDiagnostic error;
} layout_node_t;

typedef struct layout_ctx_t {
    ecs_world_t *world;
    ecs_map_t nodes;
    bool writing;
    bool arrays_active;
    int refs;
    bool ok;
} layout_ctx_t;

static ECS_COMPONENT_DECLARE(LayoutContext);
typedef struct LayoutContext {
    layout_ctx_t *ptr;
} LayoutContext;

static layout_node_t *node_get(
    layout_ctx_t *ctx,
    ecs_entity_t e)
{
    layout_node_t *n = ecs_map_get_deref(&ctx->nodes, layout_node_t, e);
    if (!n) {
        n = ecs_os_calloc_t(layout_node_t);
        n->entity = e;
        glm_vec3_one(n->fit);
        ecs_map_insert_ptr(&ctx->nodes, e, n);
    }

    return n;
}

static bool fail(
    layout_ctx_t *ctx,
    layout_node_t *n,
    SpatialError code,
    ecs_entity_t related)
{
    if (!n->error.code) {
        n->error = (SpatialDiagnostic){code, related};
    }

    ctx->ok = false;

    return false;
}

static bool require(
    layout_ctx_t *ctx,
    layout_node_t *n,
    layout_node_t *dependency,
    bool ok)
{
    if (!ok) {

        return fail(ctx, n, dependency->error.code ? dependency->error.code : SpatialMissingBounds,
                    dependency->entity);
    }

    return true;
}

static int axis_index(
    SpatialAxis axis)
{
    return axis == SpatialX ? 0 : axis == SpatialY ? 1 : axis == SpatialZ ? 2 : -1;
}

static bool finite3(
    const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int entity_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t x = *(const ecs_entity_t *)a, y = *(const ecs_entity_t *)b;

    return (x > y) - (x < y);
}

static ecs_vec_t children(
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
              entity_compare);
    }

    return result;
}

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

static bool target_get(layout_ctx_t *ctx, layout_node_t *n, ecs_entity_t target,
                       layout_node_t **out)
{
    if (!target || !ecs_is_alive(ctx->world, target)) {
        return fail(ctx, n, SpatialInvalidTarget, target);
    }
    *out = node_get(ctx, target);
    return true;
}

static bool source_bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (ecs_has(ctx->world, n->entity, ShapeMesh) && !ecs_has(ctx->world, n->entity, Position) &&
        !ecs_has_id(ctx->world, n->entity, EcsPrefab)) {
        ecs_set(ctx->world, n->entity, Position, {0});
    }

    const ShapeMesh *s = ecs_get(ctx->world, n->entity, ShapeMesh);
    const box_mesh_t *mesh = s ? box_mesh_cache_get(box_mesh_cache_of(ctx->world), s->mesh) : NULL;
    if (!mesh) {

        return false;
    }

    const LayoutOutput *out = ecs_get(ctx->world, n->entity, LayoutOutput);
    const float *scale = out && out->fitted ? out->source_scale : s->scale;
    for (int i = 0; i < 3; i++) {
        float a = (s->surface.p[0][0] ? s->min[i] : mesh->min[i]) * scale[i];
        float b = (s->surface.p[0][0] ? s->max[i] : mesh->max[i]) * scale[i];
        n->min[i] = fminf(a, b);
        n->max[i] = fmaxf(a, b);
    }

    return finite3(n->min) && finite3(n->max);
}

static bool intrinsic_bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->intrinsic_state == 2) {

        return n->has_intrinsic;
    }

    if (n->intrinsic_state == 1) {

        return false;
    }

    n->intrinsic_state = 1;
    layout_node_t source = {.entity = n->entity};
    if (source_bounds(ctx, &source)) {
        glm_vec3_copy(source.min, n->intrinsic_min);
        glm_vec3_copy(source.max, n->intrinsic_max);
        n->has_intrinsic = true;
    } else {
        glm_vec3_fill(n->intrinsic_min, FLT_MAX);
        glm_vec3_fill(n->intrinsic_max, -FLT_MAX);
        ecs_vec_t list = children(ctx->world, n->entity, true);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            layout_node_t *c = node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
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

    return n->has_intrinsic;
}

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
        const Array *a = ecs_get(world, instance->owner, Array);
        if (a) {
            int axis = axis_index(a->axis);
            if (axis < 0) {

                return fail(ctx, n, SpatialImpossible, instance->owner);
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

                return fail(ctx, n, SpatialImpossible, instance->owner);
            }

            glm_scale(local, scale);
            local[3][axis] += a->spacing * instance->index;
            n->controlled = true;
        }
    }

    ecs_entity_t parent = ecs_get_parent(world, n->entity);
    if (place && parent && ecs_has(world, parent, Distribute)) {
        layout_node_t *p = node_get(ctx, parent);
        if (!require(ctx, n, p, arrange(ctx, p))) {

            return false;
        }
    }

    if (place && n->placed) {
        glm_vec3_add(local[3], n->placement, local[3]);
        n->controlled = true;
    }

    return true;
}

static bool anchor_frame(layout_ctx_t *ctx, layout_node_t *n, SpatialAnchor a, mat4 frame,
                         bool position);
static bool orthonormal(mat4 input, mat4 output);

static bool basis(
    layout_ctx_t *ctx,
    layout_node_t *n,
    mat4 result)
{
    if (n->basis_state == 2) {
        glm_mat4_copy(n->basis, result);

        return !n->error.code;
    }

    if (n->basis_state == 1) {
        return fail(ctx, n, SpatialCycle, n->entity);
    }

    n->basis_state = 1;
    mat4 local;
    if (!local_pose(ctx, n, local, false)) {
        return false;
    }

    ecs_entity_t parent = ecs_get_parent(ctx->world, n->entity);
    if (parent) {
        layout_node_t *p = node_get(ctx, parent);
        mat4 parent_basis;
        if (!require(ctx, n, p, basis(ctx, p, parent_basis))) {
            return false;
        }

        glm_mat4_mul(parent_basis, local, n->basis);
    } else {
        glm_mat4_copy(local, n->basis);
    }

    const LayoutOutput *out = ecs_get(ctx->world, n->entity, LayoutOutput);
    if (ecs_has_id(ctx->world, n->entity, ManualTransform) && !(out && out->manual)) {
        const WorldTransform *t = ecs_get(ctx->world, n->entity, WorldTransform);
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
                return false;
            }

            if (!anchor_frame(ctx, n, a.self, self, false) ||
                !require(ctx, n, t, anchor_frame(ctx, t, a.other, other, false))) {
                return false;
            }

            glm_mat4_mul(target, other, target);
            if (!orthonormal(target, orientation)) {
                return fail(ctx, n, SpatialZeroBounds, t->entity);
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

    return !n->error.code;
}

static bool bounds(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->bounds_state == 2) {

        return !n->error.code && n->has_bounds;
    }

    if (n->bounds_state == 1) {

        return fail(ctx, n, SpatialCycle, n->entity);
    }

    n->bounds_state = 1;
    intrinsic_bounds(ctx, n);
    ecs_world_t *world = ctx->world;
    n->has_bounds = source_bounds(ctx, n);
    const Fit *fit = ecs_get(world, n->entity, Fit);
    if (!n->has_bounds) {
        if (fit) {

            return fail(ctx, n, SpatialMissingBounds, n->entity);
        }

        ecs_vec_t list = children(world, n->entity, false);
        glm_vec3_fill(n->min, FLT_MAX);
        glm_vec3_fill(n->max, -FLT_MAX);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            layout_node_t *c = node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
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
            !finite3(f.padding)) {

            return fail(ctx, n, SpatialImpossible, n->entity);
        }

        if (!target_get(ctx, n, f.target, &t) || !require(ctx, n, t, bounds(ctx, t))) {

            return false;
        }

        mat4 self_basis, target_basis;
        if (!basis(ctx, n, self_basis) || !require(ctx, n, t, basis(ctx, t, target_basis))) {

            return false;
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
                return fail(ctx, n, SpatialZeroBounds, t->entity);
            }

            float available = (target_size - 2 * f.padding[i]) * target_scale;
            if (self_scale <= EPS || target_scale <= EPS) {

                return fail(ctx, n, SpatialZeroBounds, t->entity);
            }

            size *= self_scale;
            if (size <= EPS) {

                return fail(ctx, n, SpatialZeroBounds, n->entity);
            }

            if (available <= EPS || f.padding[i] < 0) {

                return fail(ctx, n, SpatialImpossible, t->entity);
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

    return !n->error.code && n->has_bounds;
}

static bool anchor_frame(layout_ctx_t *ctx, layout_node_t *n, SpatialAnchor a, mat4 frame,
                         bool position)
{
    glm_mat4_identity(frame);
    if (a.frame) {
        if (!ecs_is_alive(ctx->world, a.frame) ||
            ecs_get_parent(ctx->world, a.frame) != n->entity) {

            return fail(ctx, n, SpatialInvalidAnchor, a.frame);
        }
        const AttachmentFrame *attachment = ecs_get(ctx->world, a.frame, AttachmentFrame);
        if (!attachment || !finite3(attachment->position) || !finite3(attachment->rotation)) {

            return fail(ctx, n, SpatialInvalidAnchor, a.frame);
        }
        glm_euler_yxz(ECS_CONST_CAST(float *, attachment->rotation), frame);
        glm_vec3_copy(ECS_CONST_CAST(float *, attachment->position), frame[3]);

        return true;
    }

    if (position && !bounds(ctx, n)) {

        return fail(ctx, n, n->error.code ? n->error.code : SpatialMissingBounds, n->entity);
    }
    AnchorPoint points[] = {a.x, a.y, a.z};
    for (int i = 0; i < 3; i++) {
        if (points[i] < SpatialCenter || points[i] > SpatialMax) {

            return fail(ctx, n, SpatialInvalidAnchor, n->entity);
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
            return fail(ctx, n, SpatialInvalidAnchor, n->entity);
        }

        normal[axis] = sign;
        if (position) {
            frame[3][axis] = sign > 0 ? n->max[axis] : n->min[axis];
        }
        glm_vec3_copy(normal, frame[2]);
        glm_vec3_cross(up, normal, frame[0]);
        glm_vec3_copy(up, frame[1]);
    }

    return true;
}

static bool orthonormal(
    mat4 input,
    mat4 output)
{
    if (fabsf(glm_mat4_det(input)) < EPS) {

        return false;
    }

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

    return true;
}

static bool pose(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->pose_state == 2) {

        return !n->error.code;
    }

    if (n->pose_state == 1) {

        return fail(ctx, n, SpatialCycle, n->entity);
    }

    n->pose_state = 1;
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = n->entity, parent = ecs_get_parent(world, e);
    mat4 base = GLM_MAT4_IDENTITY_INIT, local;
    if (parent) {
        layout_node_t *p = node_get(ctx, parent);
        if (!require(ctx, n, p, pose(ctx, p))) {

            return false;
        }

        glm_mat4_copy(p->pose, base);
        if (ecs_has(world, parent, Distribute)) {
            if (!require(ctx, n, p, arrange(ctx, p))) {

                return false;
            }
        }
    }

    const LayoutOutput *out = ecs_get(world, e, LayoutOutput);
    const SpatialAlign *align = ecs_get(world, e, SpatialAlign);
    const ArrayInstance *instance = ecs_get(world, e, ArrayInstance);
    if (!local_pose(ctx, n, local, true)) {

        return false;
    }

    glm_mat4_mul(base, local, n->pose);
    if (ecs_has_id(world, e, ManualTransform) && !(out && out->manual)) {
        if (align || n->controlled) {

            return fail(ctx, n, SpatialConflictingLayout, e);
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

            return fail(ctx, n, SpatialConflictingLayout, e);
        }

        if (a.rotation < SpatialKeep || a.rotation > SpatialMatch || !finite3(a.offset) ||
            !finite3(a.rotation_offset)) {

            return fail(ctx, n, SpatialImpossible, e);
        }

        if (!target_get(ctx, n, a.target, &t) || !require(ctx, n, t, pose(ctx, t))) {

            return false;
        }

        mat4 self, other, target, frame;
        if (!anchor_frame(ctx, n, a.self, self, true) ||
            !require(ctx, n, t, anchor_frame(ctx, t, a.other, other, true))) {

            return false;
        }

        glm_mat4_mul(t->pose, other, target);
        if (!orthonormal(target, frame)) {

            return fail(ctx, n, SpatialZeroBounds, t->entity);
        }

        if (!basis(ctx, n, n->pose)) {
            return false;
        }

        vec3 desired, point;
        glm_mat4_mulv3(frame, a.offset, 1, desired);
        glm_mat4_mulv3(n->pose, self[3], 0, point);
        glm_vec3_sub(desired, point, n->pose[3]);
    }

    for (int i = 0; i < 4; i++) {
        if (!finite3(n->pose[i])) {

            return fail(ctx, n, SpatialImpossible, e);
        }
    }

    n->pose_state = 2;

    return !n->error.code;
}

static bool arrange(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    if (n->arrange_state == 2) {

        return !n->error.code;
    }

    if (n->arrange_state == 1) {

        return fail(ctx, n, SpatialCycle, n->entity);
    }

    n->arrange_state = 1;
    Distribute d = *ecs_get(ctx->world, n->entity, Distribute);
    int axis = axis_index(d.axis);
    if (axis < 0 || d.span < 0 || d.gap < 0 || !isfinite(d.span) || !isfinite(d.gap) ||
        d.mode < SpatialSpaceBetween || d.mode > SpatialFixedGap ||
        ecs_has(ctx->world, n->entity, Array)) {

        return fail(ctx, n, SpatialImpossible, n->entity);
    }

    ecs_vec_t list = children(ctx->world, n->entity, false);
    int count = ecs_vec_count(&list);
    float *lo = count ? ecs_os_malloc_n(float, count) : NULL;
    float *hi = count ? ecs_os_malloc_n(float, count) : NULL;
    float total = 0;
    for (int i = 0; i < count; i++) {
        layout_node_t *c = node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
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
            layout_node_t *c = node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i));
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

    return !n->error.code;
}

static ecs_vec_t collect(
    ecs_world_t *world,
    ecs_id_t id)
{
    ecs_vec_t result;
    ecs_vec_init_t(NULL, &result, ecs_entity_t, 0);
    ecs_iter_t it = ecs_each_id(world, id);
    while (ecs_each_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            *ecs_vec_append_t(NULL, &result, ecs_entity_t) = it.entities[i];
        }
    }

    return result;
}

static void sync_component(
    ecs_world_t *world,
    ecs_entity_t dst,
    ecs_entity_t src,
    ecs_id_t id)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, id);
    if (!ti) {

        return;
    }

    const void *value = ecs_get_id(world, src, id);
    if (value) {
        const void *old = ecs_get_id(world, dst, id);
        if (!old || memcmp(old, value, (size_t)ti->size)) {
            ecs_set_id(world, dst, id, (size_t)ti->size, value);
        }
    } else if (ecs_owns_id(world, dst, id)) {
        ecs_remove_id(world, dst, id);
    }
}

static void sync_prototype(
    ecs_world_t *world,
    ecs_entity_t dst,
    ecs_entity_t src)
{
    ecs_id_t ids[] = {ecs_id(Position),   ecs_id(Rotation),       ecs_id(Scale),
                      ecs_id(Fit),        ecs_id(SpatialAlign),   ecs_id(Array),
                      ecs_id(Distribute), ecs_id(AttachmentFrame)};
    for (int i = 0; i < 8; i++) {
        if (ids[i] == ecs_id(Position) && !ecs_has(world, src, Position)) {
            continue;
        }

        sync_component(world, dst, src, ids[i]);
    }

    const ecs_type_t *type = ecs_get_type(world, src);
    ecs_vec_t shapes;
    ecs_vec_init_t(NULL, &shapes, ecs_id_t, 0);
    for (int i = 0; type && i < type->count; i++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) && ecs_has_pair(world, id, EcsWith, ecs_id(ShapeMesh))) {
            *ecs_vec_append_t(NULL, &shapes, ecs_id_t) = id;
        }
    }

    type = ecs_get_type(world, dst);
    ecs_vec_t removed;
    ecs_vec_init_t(NULL, &removed, ecs_id_t, 0);
    for (int i = 0; type && i < type->count; i++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) && ecs_has_pair(world, id, EcsWith, ecs_id(ShapeMesh)) &&
            !ecs_has_id(world, src, id)) {
            *ecs_vec_append_t(NULL, &removed, ecs_id_t) = id;
        }
    }

    for (int i = 0; i < ecs_vec_count(&removed); i++) {
        ecs_remove_id(world, dst, *ecs_vec_get_t(&removed, ecs_id_t, i));
    }

    for (int i = 0; i < ecs_vec_count(&shapes); i++) {
        sync_component(world, dst, src, *ecs_vec_get_t(&shapes, ecs_id_t, i));
    }

    ecs_vec_fini_t(NULL, &shapes, ecs_id_t);
    ecs_vec_fini_t(NULL, &removed, ecs_id_t);
    ecs_vec_t descendants = children(world, src, true);
    for (int i = 0; i < ecs_vec_count(&descendants); i++) {
        ecs_entity_t child = *ecs_vec_get_t(&descendants, ecs_entity_t, i);
        const char *name = ecs_get_name(world, child);
        char generated[64];
        if (!name) {
            ecs_os_snprintf(generated, sizeof(generated), "__prototype_%llu",
                            (unsigned long long)child);
            name = generated;
        }

        ecs_entity_t copy = ecs_lookup_child(world, dst, name);
        if (!copy) {
            copy = ecs_entity(world, {.name = name, .parent = dst});
            ecs_add_pair(world, copy, EcsIsA, child);
        }

        ecs_set(world, copy, PrototypeSource, {child});
        sync_prototype(world, copy, child);
    }

    ecs_vec_fini_t(NULL, &descendants, ecs_entity_t);
    descendants = children(world, dst, true);
    for (int i = 0; i < ecs_vec_count(&descendants); i++) {
        ecs_entity_t copy = *ecs_vec_get_t(&descendants, ecs_entity_t, i);
        const PrototypeSource *source = ecs_get(world, copy, PrototypeSource);
        if (source && (!ecs_is_alive(world, source->entity) ||
                       ecs_get_parent(world, source->entity) != src)) {
            ecs_delete(world, copy);
        }
    }

    ecs_vec_fini_t(NULL, &descendants, ecs_entity_t);
}

static void prepare_prototype(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (!ecs_has_id(world, e, EcsPrefab)) {
        if (ecs_has(world, e, ShapeMesh) && !ecs_has(world, e, Position)) {
            ecs_set(world, e, Position, {0});
        }

        ecs_add_id(world, e, EcsPrefab);
        ecs_add_id(world, e, ArrayPrototype);
    }

    ecs_vec_t list = children(world, e, true);
    for (int i = 0; i < ecs_vec_count(&list); i++) {
        prepare_prototype(world, *ecs_vec_get_t(&list, ecs_entity_t, i));
    }

    ecs_vec_fini_t(NULL, &list, ecs_entity_t);
}

typedef struct array_entry_t {
    ecs_entity_t entity;
    int depth;
} array_entry_t;

static int array_compare(
    const void *a,
    const void *b)
{
    const array_entry_t *x = a, *y = b;

    return x->depth == y->depth ? entity_compare(&x->entity, &y->entity)
                                : (x->depth > y->depth) - (x->depth < y->depth);
}

static ecs_vec_t ordered_arrays(
    ecs_world_t *world)
{
    ecs_vec_t result = collect(world, ecs_id(Array));
    int count = ecs_vec_count(&result);
    array_entry_t *entries = count ? ecs_os_malloc_n(array_entry_t, count) : NULL;
    for (int i = 0; i < count; i++) {
        ecs_entity_t e = *ecs_vec_get_t(&result, ecs_entity_t, i);
        entries[i] = (array_entry_t){.entity = e};
        for (; e; e = ecs_get_parent(world, e)) {
            entries[i].depth++;
        }
    }

    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(array_entry_t), array_compare);
    }

    for (int i = 0; i < count; i++) {
        *ecs_vec_get_t(&result, ecs_entity_t, i) = entries[i].entity;
    }

    ecs_os_free(entries);

    return result;
}

static void arrays(
    layout_ctx_t *ctx)
{
    ecs_world_t *world = ctx->world;
    bool active = ecs_count(world, Array) != 0;
    if (!active && !ctx->arrays_active) {

        return;
    }

    ctx->arrays_active = active;
    ecs_vec_t instances = collect(world, ecs_id(ArrayInstance));
    for (int i = 0; i < ecs_vec_count(&instances); i++) {
        ecs_entity_t e = *ecs_vec_get_t(&instances, ecs_entity_t, i);
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        ArrayInstance instance = *ecs_get(world, e, ArrayInstance);
        const Array *a =
            ecs_is_alive(world, instance.owner) ? ecs_get(world, instance.owner, Array) : NULL;
        if (!a || instance.index >= a->count || !ecs_is_alive(world, instance.prototype) ||
            ecs_get_parent(world, instance.prototype) != instance.owner) {
            ecs_delete(world, e);
        }
    }

    ecs_vec_fini_t(NULL, &instances, ecs_entity_t);
    ecs_vec_t list;
    bool pending = true;
    while (pending) {
        pending = false;
        list = ordered_arrays(world);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            ecs_entity_t e = *ecs_vec_get_t(&list, ecs_entity_t, i);
            if (!ecs_is_alive(world, e) || ecs_has_id(world, e, EcsPrefab)) {
                continue;
            }

            Array a = *ecs_get(world, e, Array);
            layout_node_t *n = node_get(ctx, e);
            if (n->array_done) {
                continue;
            }

            n->array_done = true;
            pending = true;
            n->requested = true;
            if (axis_index(a.axis) < 0 || a.count < 0 || a.count > 100000 || !isfinite(a.spacing) ||
                !finite3(a.rotation_step) || !finite3(a.scale_step) ||
                ecs_has(world, e, Distribute)) {
                fail(ctx, n, SpatialImpossible, e);
                continue;
            }

            ecs_vec_t current = children(world, e, false);
            for (int j = 0; j < ecs_vec_count(&current); j++) {
                ecs_entity_t child = *ecs_vec_get_t(&current, ecs_entity_t, j);
                const ArrayInstance *instance = ecs_get(world, child, ArrayInstance);
                if (instance && instance->owner == e && instance->index >= a.count) {
                    ecs_delete(world, child);
                }
            }

            ecs_vec_fini_t(NULL, &current, ecs_entity_t);
            ecs_vec_t prototypes = children(world, e, true);
            for (int p = 0; p < ecs_vec_count(&prototypes); p++) {
                ecs_entity_t prototype = *ecs_vec_get_t(&prototypes, ecs_entity_t, p);
                prepare_prototype(world, prototype);
                for (int index = 0; index < a.count; index++) {
                    char name[96];
                    ecs_os_snprintf(name, sizeof(name), "__array_%llu_%d",
                                    (unsigned long long)prototype, index);
                    ecs_entity_t instance = ecs_lookup_child(world, e, name);
                    if (!instance) {
                        instance = ecs_entity(world, {.name = name, .parent = e});
                        ecs_add_pair(world, instance, EcsIsA, prototype);
                        ecs_set(world, instance, ArrayInstance, {e, prototype, index});
                    }

                    const ArrayInstance *identity = ecs_get(world, instance, ArrayInstance);
                    if (!identity || identity->owner != e || identity->prototype != prototype ||
                        identity->index != index) {
                        fail(ctx, n, SpatialConflictingLayout, instance);
                        continue;
                    }

                    sync_prototype(world, instance, prototype);
                    node_get(ctx, instance)->requested = true;
                }
            }

            ecs_vec_fini_t(NULL, &prototypes, ecs_entity_t);
        }

        ecs_vec_fini_t(NULL, &list, ecs_entity_t);
    }

    ecs_query_t *query =
        ecs_query(world, {.terms = {{.id = ArrayPrototype}}, .flags = EcsQueryMatchPrefab});
    list = (ecs_vec_t){0};
    ecs_vec_init_t(NULL, &list, ecs_entity_t, 0);
    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            *ecs_vec_append_t(NULL, &list, ecs_entity_t) = it.entities[i];
        }
    }

    ecs_query_fini(query);
    for (int i = 0; i < ecs_vec_count(&list); i++) {
        ecs_entity_t e = *ecs_vec_get_t(&list, ecs_entity_t, i), parent = ecs_get_parent(world, e);
        while (parent && !ecs_has(world, parent, Array)) {
            parent = ecs_get_parent(world, parent);
        }

        if (!parent) {
            ecs_remove_id(world, e, EcsPrefab);
            ecs_remove_id(world, e, ArrayPrototype);
        }
    }

    ecs_vec_fini_t(NULL, &list, ecs_entity_t);
}

static void set_changed(ecs_world_t *world, ecs_entity_t e, ecs_id_t id, size_t size,
                        const void *value)
{
    const void *old = ecs_get_id(world, e, id);
    if (!old || memcmp(old, value, size)) {
        ecs_set_id(world, e, id, size, value);
    }
}

static void publish(
    layout_ctx_t *ctx,
    layout_node_t *n)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = n->entity;
    if (!ecs_is_alive(world, e)) {

        return;
    }

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
        set_changed(world, e, ecs_id(WorldTransform), sizeof(transform), &transform);
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
        set_changed(world, e, ecs_id(LayoutOutput), sizeof(out), &out);
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
        set_changed(world, e, ecs_id(IntrinsicBounds), sizeof(intrinsic), &intrinsic);
        set_changed(world, e, ecs_id(ResolvedBounds), sizeof(resolved), &resolved);
    } else {
        ecs_remove(world, e, IntrinsicBounds);
        ecs_remove(world, e, ResolvedBounds);
    }

    const Box *box = ecs_get(world, e, Box);
    if (box && fitted) {
        ResolvedBox result = {box->x * n->fit[0], box->y * n->fit[1], box->z * n->fit[2]};
        set_changed(world, e, ecs_id(ResolvedBox), sizeof(result), &result);
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

static void clear_nodes(
    layout_ctx_t *ctx)
{
    ecs_map_iter_t it = ecs_map_iter(&ctx->nodes);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    // ecs_map_clear reallocates the buckets, so a scene without layout nodes skips it and the
    // frame stays free of allocations.
    if (ecs_map_count(&ctx->nodes)) {
        ecs_map_clear(&ctx->nodes);
    }
}

bool box_spatial_layout_update(
    ecs_world_t *world)
{
    const LayoutContext *singleton = ecs_singleton_get(world, LayoutContext);
    if (!singleton) {

        return true;
    }

    layout_ctx_t *ctx = singleton->ptr;
    if (ctx->writing) {

        return false;
    }

    clear_nodes(ctx);
    ctx->ok = true;
    arrays(ctx);
    ecs_id_t ids[] = {ecs_id(Fit), ecs_id(SpatialAlign), ecs_id(Distribute), ecs_id(LayoutOutput)};
    for (int k = 0; k < 4; k++) {
        ecs_vec_t list = collect(world, ids[k]);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            node_get(ctx, *ecs_vec_get_t(&list, ecs_entity_t, i))->requested = true;
        }

        ecs_vec_fini_t(NULL, &list, ecs_entity_t);
    }

    ecs_vec_t roots;
    ecs_vec_init_t(NULL, &roots, layout_node_t *, 0);
    ecs_map_iter_t it = ecs_map_iter(&ctx->nodes);
    while (ecs_map_next(&it)) {
        layout_node_t *n = ecs_map_ptr(&it);
        if (n->requested) {
            *ecs_vec_append_t(NULL, &roots, layout_node_t *) = n;
        }
    }

    for (int i = 0; i < ecs_vec_count(&roots); i++) {
        layout_node_t *n = *ecs_vec_get_t(&roots, layout_node_t *, i);
        if (ecs_has(world, n->entity, Distribute)) {
            arrange(ctx, n);
        }

        pose(ctx, n);
        bounds(ctx, n);
    }

    ecs_vec_clear(&roots);
    it = ecs_map_iter(&ctx->nodes);
    while (ecs_map_next(&it)) {
        layout_node_t *n = ecs_map_ptr(&it);
        if (n->requested) {
            *ecs_vec_append_t(NULL, &roots, layout_node_t *) = n;
        }
    }

    for (int i = 0; i < ecs_vec_count(&roots); i++) {
        layout_node_t *n = *ecs_vec_get_t(&roots, layout_node_t *, i);
        pose(ctx, n);
        bounds(ctx, n);
    }

    ctx->writing = true;
    for (int i = 0; i < ecs_vec_count(&roots); i++) {
        publish(ctx, *ecs_vec_get_t(&roots, layout_node_t *, i));
    }

    ctx->writing = false;
    ecs_vec_fini_t(NULL, &roots, layout_node_t *);

    return ctx->ok;
}

static void shape_changed(
    ecs_iter_t *it)
{
    layout_ctx_t *ctx = it->ctx;
    if (ctx->writing) {

        return;
    }

    for (int i = 0; i < it->count; i++) {
        LayoutOutput *out = ecs_get_mut(it->world, it->entities[i], LayoutOutput);
        const ShapeMesh *shape = ecs_get(it->world, it->entities[i], ShapeMesh);
        if (out && shape) {
            glm_vec3_copy(ECS_CONST_CAST(float *, shape->scale), out->source_scale);
        }
    }
}

static void layout_run(
    ecs_iter_t *it)
{
    box_spatial_layout_update(it->world);
}

static void layout_free(
    void *ptr)
{
    layout_ctx_t *ctx = ptr;
    if (--ctx->refs) {

        return;
    }

    clear_nodes(ctx);
    ecs_map_fini(&ctx->nodes);
    ecs_os_free(ctx);
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
    ctx->refs = 2;
    ecs_map_init(&ctx->nodes, NULL);
    ecs_singleton_set(world, LayoutContext, {ctx});
    ecs_observer(world, {.query.terms = {{.id = ecs_id(ShapeMesh)}},
                         .events = {EcsOnSet},
                         .callback = shape_changed,
                         .ctx = ctx,
                         .ctx_free = layout_free});
    ecs_system(world, {.entity = ecs_entity(world, {.name = "SpatialLayout"}),
                       .phase = EcsPostUpdate,
                       .run = layout_run,
                       .immediate = true,
                       .ctx = ctx,
                       .ctx_free = layout_free});
}
