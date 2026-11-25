/**
 * @file projection/projection.c
 * @brief Moves ProjectedAnchor entities to the projection of their target through a view.
 *
 * Runs in OnStore, after the transforms of the frame and before the render extract, and
 * writes the world transforms of every anchored subtree so the extract sees them this frame.
 *
 * Position is written without OnSet: the world transforms are already final, and an OnSet
 * would make the static transforms compute the anchor again in the next frame. A LayoutItem
 * resized to a ProjectedBox is laid out after the merge by BoxProjectionLayout.
 */

#define BOX_PROJECTION_IMPL
#include <box.h>
#include "../transform/transform.h"
#include <float.h>

ECS_TAG_DECLARE(ProjectedHidden);

/** State shared by the systems of the module */
typedef struct box_projection_state_t {
    ecs_query_t *surfaces; /**< Surface and an optional SurfaceStatus */
    ecs_vec_t resized;     /**< Anchored entities whose LayoutItem changed this frame */
} box_projection_state_t;

/** Camera of a view, ready to project points for one frame */
typedef struct box_projection_lens_t {
    ecs_entity_t view; /**< View the lens was made for */
    dvec3 eye;         /**< World position of the camera, the origin of view_proj */
    mat4 view_proj;    /**< Projection times the inverse of the camera transform relative to
                        *   the eye */
    float width;       /**< Width of the surface of the view in points */
    float height;      /**< Height of the surface of the view in points */
    bool valid;        /**< The view exists and its camera has a Camera */
} box_projection_lens_t;

/** Build the lens of a view: the projection of its camera for its surface.
 *
 * @param world The world
 * @param surfaces Query for Surface and an optional SurfaceStatus
 * @param view The view
 * @param lens Receives the lens
 */
static void box_projection_lens(
    ecs_world_t *world,
    ecs_query_t *surfaces,
    ecs_entity_t view,
    box_projection_lens_t *lens)
{
    const View *v = view && ecs_is_alive(world, view) ? ecs_get(world, view, View) : NULL;
    ecs_entity_t camera = v ? v->camera : 0;
    const Camera *c = camera && ecs_is_alive(world, camera) ?
        ecs_get(world, camera, Camera) : NULL;
    lens->view = view;
    lens->valid = c != NULL;
    if (c) {
        WorldTransform t_buf;
        const WorldTransform *t = box_transform_world(world, camera, &t_buf);
        mat4 transform, inverse, proj;
        if (t) {
            box_dvec3_copy(t->position, lens->eye);
            box_transform_mat4(t, t->position, transform);
        } else {
            lens->eye[0] = lens->eye[1] = lens->eye[2] = 0.0;
            glm_mat4_identity(transform);
        }

        glm_mat4_inv(transform, inverse);
        box_surface_view_size(world, surfaces, view, &lens->width, &lens->height);
        box_camera_projection(c, lens->width, lens->height, proj);
        glm_mat4_mul(proj, inverse, lens->view_proj);
    }
}

/** Project the target of an anchor through a lens.
 *
 * @param world The world
 * @param anchor The anchor
 * @param lens The lens of the view of the anchor
 * @param x Receives x in points of the surface, origin at the center
 * @param y Receives y in pixels of the surface, y up
 * @return Whether the point is in front of the camera and inside the view
 */
static bool box_projection_point(
    ecs_world_t *world,
    const ProjectedAnchor *anchor,
    const box_projection_lens_t *lens,
    float *x,
    float *y)
{
    bool result = false;
    WorldTransform t_buf;
    const WorldTransform *t = anchor->target && ecs_is_alive(world, anchor->target) ?
        box_transform_world(world, anchor->target, &t_buf) : NULL;
    if (!lens->valid || !t) {
        goto done;
    }

    vec4 point = {
        (float)(t->position[0] - lens->eye[0]) + anchor->offset[0],
        (float)(t->position[1] - lens->eye[1]) + anchor->offset[1],
        (float)(t->position[2] - lens->eye[2]) + anchor->offset[2],
        1.0f
    };
    vec4 clip;
    glm_mat4_mulv(ECS_CONST_CAST(vec4*, lens->view_proj), point, clip);
    if (clip[3] <= 1e-6f) {
        goto done;
    }

    float nx = clip[0] / clip[3];
    float ny = clip[1] / clip[3];
    float nz = clip[2] / clip[3];
    if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f || nz < 0.0f || nz > 1.0f) {
        goto done;
    }

    *x = nx * lens->width * 0.5f;
    *y = ny * lens->height * 0.5f;
    result = true;
done:
    return result;
}

/** Add a point in clip space to the projection of a box.
 *
 * @param clip The point, in front of the near plane
 * @param bounds The rectangle so far in normalized device coordinates
 * @param before_far Set when the point is not past the far plane
 */
static void box_projection_extend(
    const vec4 clip,
    ProjectedBounds *bounds,
    bool *before_far)
{
    float x = clip[0] / clip[3];
    float y = clip[1] / clip[3];
    bounds->left = glm_min(bounds->left, x);
    bounds->right = glm_max(bounds->right, x);
    bounds->bottom = glm_min(bounds->bottom, y);
    bounds->top = glm_max(bounds->top, y);
    *before_far |= clip[2] >= 0.0f;
}

/** Project the box of an anchor through a lens.
 *
 * Corners are indexed by bits: bit 0 picks the max x, bit 1 the max y and bit 2 the max z.
 * The depth is reversed, so a clip space point is in front of the near plane when its z is
 * not above its w; the edges that cross the plane add the point where they cross it.
 *
 * @param world The world
 * @param anchor The anchor
 * @param box The box of the anchor
 * @param lens The lens of the view of the anchor
 * @param out Receives the rectangle in pixels of the surface, origin at the center, y up
 * @return Whether some of the box is in front of the camera, overlaps the view and is at least
 *         min_width wide
 */
static bool box_projection_box(
    ecs_world_t *world,
    const ProjectedAnchor *anchor,
    const ProjectedBox *box,
    const box_projection_lens_t *lens,
    ProjectedBounds *out)
{
    bool result = false, before_far = false;
    WorldTransform t_buf;
    const WorldTransform *t = anchor->target && ecs_is_alive(world, anchor->target) ?
        box_transform_world(world, anchor->target, &t_buf) : NULL;
    vec4 clip[8];
    float front[8];
    int32_t i, bit, count = 0;
    ProjectedBounds b = { FLT_MAX, -FLT_MAX, -FLT_MAX, FLT_MAX };
    if (!lens->valid || !t) {
        goto done;
    }

    for (i = 0; i < 8; i ++) {
        vec4 point = {
            (float)(t->position[0] - lens->eye[0]) + anchor->offset[0] +
                ((i & 1) ? box->max[0] : box->min[0]),
            (float)(t->position[1] - lens->eye[1]) + anchor->offset[1] +
                ((i & 2) ? box->max[1] : box->min[1]),
            (float)(t->position[2] - lens->eye[2]) + anchor->offset[2] +
                ((i & 4) ? box->max[2] : box->min[2]),
            1.0f
        };
        glm_mat4_mulv(ECS_CONST_CAST(vec4*, lens->view_proj), point, clip[i]);
        front[i] = clip[i][3] - clip[i][2];
    }

    for (i = 0; i < 8; i ++) {
        if (front[i] >= 0.0f && clip[i][3] > 1e-6f) {
            box_projection_extend(clip[i], &b, &before_far);
            count ++;
        }

        for (bit = 1; bit < 8; bit <<= 1) {
            int32_t j = i | bit;
            if (j == i || (front[i] >= 0.0f) == (front[j] >= 0.0f)) {
                continue;
            }

            float s = front[i] / (front[i] - front[j]);
            vec4 cross;
            int32_t k;
            for (k = 0; k < 4; k ++) {
                cross[k] = clip[i][k] + (clip[j][k] - clip[i][k]) * s;
            }

            if (cross[3] > 1e-6f) {
                box_projection_extend(cross, &b, &before_far);
                count ++;
            }
        }
    }

    if (!count || !before_far || b.left > 1.0f || b.right < -1.0f || b.bottom > 1.0f ||
        b.top < -1.0f)
    {
        goto done;
    }

    float left = b.left * lens->width * 0.5f;
    float right = b.right * lens->width * 0.5f;
    if (right - left < box->min_width) {
        goto done;
    }

    out->left = left;
    out->right = right;
    out->top = b.top * lens->height * 0.5f;
    out->bottom = b.bottom * lens->height * 0.5f;
    result = true;
done:
    return result;
}

/** Size the LayoutItem of an anchor to the projection of its box.
 *
 * @param world The world
 * @param e The anchored entity
 * @param item The LayoutItem of the entity
 * @param box The box of the anchor
 * @param bounds The projection of the box
 * @return Whether the LayoutItem changed
 */
static bool box_projection_fit(
    ecs_world_t *world,
    ecs_entity_t e,
    LayoutItem *item,
    const ProjectedBox *box,
    const ProjectedBounds *bounds)
{
    bool changed = false;
    float width = bounds->right - bounds->left;
    float height = bounds->top - bounds->bottom;
    if (box->fit_width &&
        (item->width.mode != SizeModeFixed || item->width.value != width))
    {
        item->width.mode = SizeModeFixed;
        item->width.value = width;
        changed = true;
    }

    if (box->fit_height &&
        (item->height.mode != SizeModeFixed || item->height.value != height))
    {
        item->height.mode = SizeModeFixed;
        item->height.value = height;
        changed = true;
    }

    if (changed) {
        ecs_modified(world, e, LayoutItem);
    }

    return changed;
}

/** Store a world transform and raise OnSet when it changed.
 *
 * @param world The world
 * @param e The entity
 * @param dst The world transform of the entity
 * @param src The new world transform
 */
static void box_projection_store(
    ecs_world_t *world,
    ecs_entity_t e,
    WorldTransform *dst,
    const WorldTransform *src)
{
    if (!box_transform_equal(dst, src)) {
        *dst = *src;
        ecs_modified(world, e, WorldTransform);
    }
}

/** Recompute the world transforms below an anchored entity from its new transform.
 *
 * @param world The world
 * @param e The parent entity
 * @param parent The world transform of the parent
 */
static void box_projection_children(
    ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *parent)
{
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            WorldTransform *t = ecs_get_mut(world, child, WorldTransform);
            const Position *p = ecs_get(world, child, Position);
            if (!t) {
                box_projection_children(world, child, parent);
                continue;
            }

            if (p && !ecs_has(world, child, ManualTransform)) {
                WorldTransform next;
                transformSingle(parent, &next, p, ecs_get(world, child, Rotation),
                    ecs_get(world, child, Scale));
                box_projection_store(world, child, t, &next);
            }

            box_projection_children(world, child, t);
        }
    }
}

/** Add or remove ProjectedHidden when the visibility of an anchor changed.
 *
 * @param world The world
 * @param e The anchored entity
 * @param visible Whether the projection is visible
 */
static void box_projection_show(
    ecs_world_t *world,
    ecs_entity_t e,
    bool visible)
{
    bool hidden = ecs_has_id(world, e, ProjectedHidden);
    if (visible && hidden) {
        ecs_remove_id(world, e, ProjectedHidden);
    } else if (!visible && !hidden) {
        ecs_add_id(world, e, ProjectedHidden);
    }
}

/** Write the world transform of an anchored entity and its subtree.
 *
 * A hidden entity keeps its translation but loses its axes, which collapses the subtree.
 *
 * @param world The world
 * @param e The anchored entity
 * @param t The world transform of the entity
 * @param p The position of the entity
 * @param r The rotation of the entity, NULL for none
 * @param s The scale of the entity, NULL for none
 * @param visible Whether the projection is visible
 * @param subtree Whether to write the subtree too, false when BoxProjectionLayout will
 */
static void box_projection_transform(
    ecs_world_t *world,
    ecs_entity_t e,
    WorldTransform *t,
    const Position *p,
    const Rotation *r,
    const Scale *s,
    bool visible,
    bool subtree)
{
    if (!ecs_has(world, e, ManualTransform)) {
        WorldTransform buf, next;
        ecs_entity_t parent = ecs_get_parent(world, e);
        const WorldTransform *t_parent = parent ?
            transformGetParentWorldTransform(world, parent, &buf) : NULL;
        transformSingle(t_parent, &next, p, r, s);
        if (!visible) {
            glm_mat3_zero(next.basis);
        }

        box_projection_store(world, e, t, &next);
    }

    if (subtree) {
        box_projection_children(world, e, t);
    }
}

/** Move every anchored entity to the projection of its target or of its box.
 *
 * The Position and ProjectedBounds written here raise no OnSet; the WorldTransform of the
 * anchor and its subtree raises one when it changed, which is what draws and the spatial index
 * follow.
 *
 * A LayoutItem resized to a ProjectedBox raises OnSet at the merge, which marks its tree.
 *
 * @param it The system iterator
 */
static void BoxProjectionUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    box_projection_state_t *state = it->ctx;
    const ProjectedAnchor *anchor = ecs_field(it, ProjectedAnchor, 0);
    Position *p = ecs_field(it, Position, 1);
    WorldTransform *t = ecs_field(it, WorldTransform, 2);
    const Rotation *r = ecs_field(it, Rotation, 3);
    const Scale *s = ecs_field(it, Scale, 4);
    const ProjectedBox *box = ecs_field(it, ProjectedBox, 5);
    ProjectedBounds *bounds = ecs_field(it, ProjectedBounds, 6);
    LayoutItem *item = ecs_field(it, LayoutItem, 7);
    box_projection_lens_t lens = {0};
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        float x = 0, y = 0;
        if (!i || lens.view != anchor[i].view) {
            box_projection_lens(world, state->surfaces, anchor[i].view, &lens);
        }

        bool visible, resized = false;
        if (box && bounds) {
            ProjectedBounds b = {0};
            visible = box_projection_box(world, &anchor[i], &box[i], &lens, &b);
            if (visible) {
                bounds[i] = b;
                p[i].x = (b.left + b.right) * 0.5f + box[i].offset[0];
                p[i].y = b.top + box[i].offset[1];
                resized = item && box_projection_fit(world, e, &item[i], &box[i], &b);
                if (resized) {
                    *ecs_vec_append_t(NULL, &state->resized, ecs_entity_t) = e;
                }
            }
        } else {
            visible = box_projection_point(world, &anchor[i], &lens, &x, &y);
            if (visible) {
                p[i].x = x;
                p[i].y = y;
            }
        }

        box_projection_show(world, e, visible);
        box_projection_transform(world, e, &t[i], &p[i], r ? &r[i] : NULL,
            s ? &s[i] : NULL, visible, !resized);
    }
}

/** Lay out the trees of the anchors whose LayoutItem the projection resized this frame, and
 * write the world transforms of their subtrees again from the positions the layout gave.
 *
 * Runs immediate, after the merge of BoxProjectionUpdate, so the layout sees the new sizes.
 *
 * @param it The system iterator
 */
static void BoxProjectionLayout(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_projection_state_t *state = it->ctx;
    int32_t i, count = ecs_vec_count(&state->resized);
    if (!count) {
        goto done;
    }

    box_layout_update(world);
    ecs_entity_t *resized = ecs_vec_first_t(&state->resized, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        WorldTransform t_buf;
        const WorldTransform *t = ecs_is_alive(world, resized[i]) ?
            box_transform_world(world, resized[i], &t_buf) : NULL;
        if (t) {
            box_projection_children(world, resized[i], t);
        }
    }

    ecs_vec_clear(&state->resized);
done:
    return;
}

/** Free the state of the module.
 *
 * @param ctx The state
 */
static void box_projection_state_free(
    void *ctx)
{
    box_projection_state_t *state = ctx;
    ecs_vec_fini_t(NULL, &state->resized, ecs_entity_t);
    ecs_os_free(state);
}

bool box_projection_anchored(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t anchor = ecs_id(ProjectedAnchor);

    return anchor && ecs_is_alive(world, anchor) && (ecs_has_id(world, e, anchor) ||
        ecs_get_target_for_id(world, e, EcsChildOf, anchor) != 0);
}

void BoxProjectionImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxProjection);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxLayout);

    ecs_set_name_prefix(world, "Projected");
    ECS_META_COMPONENT(world, ProjectedAnchor);
    ECS_META_COMPONENT(world, ProjectedBox);
    ECS_META_COMPONENT(world, ProjectedBounds);
    ECS_TAG_DEFINE(world, ProjectedHidden);
    ecs_set_name_prefix(world, NULL);
    ecs_add_pair(world, ecs_id(ProjectedAnchor), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(ProjectedBox), EcsWith, ecs_id(ProjectedBounds));

    box_projection_state_t *state = ecs_os_calloc_t(box_projection_state_t);
    ecs_vec_init_t(NULL, &state->resized, ecs_entity_t, 0);
    state->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Surfaces" }),
        .terms = {
            { .id = ecs_id(Surface), .inout = EcsIn },
            { .id = ecs_id(SurfaceStatus), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .phase = EcsOnStore,
        .query.terms = {
            { .id = ecs_id(ProjectedAnchor), .inout = EcsIn },
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(WorldTransform), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Scale), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(ProjectedBox), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(ProjectedBounds), .inout = EcsInOut, .oper = EcsOptional },
            { .id = ecs_id(LayoutItem), .inout = EcsInOut, .oper = EcsOptional }
        },
        .callback = BoxProjectionUpdate,
        .ctx = state,
        .ctx_free = box_projection_state_free
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Relayout" }),
        .phase = EcsOnStore,
        .run = BoxProjectionLayout,
        .ctx = state,
        .immediate = true
    });
}
