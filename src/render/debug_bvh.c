/**
 * @file render/debug_bvh.c
 * @brief Boxes of the spatial index trees the debug pass of a view draws (BvhDebug).
 */

#include "render.h"
#include "debug_portals.h"
#include "../spatial/spatial.h"

/** Walk of one tree for the boxes of a view */
typedef struct box_render_bvh_walk_t {
    box_render_packet_t *p;     /**< The packet the boxes are appended to */
    const BvhDebug *settings;   /**< Settings of the view */
    uint32_t tree;              /**< The tree walked */
    uint32_t pose;              /**< Pose of the boxes in the poses of the view */
    int32_t count;              /**< Boxes of the view so far */
    int32_t poses;              /**< Poses of the view so far */
} box_render_bvh_walk_t;

/** Append a box of a tree to the boxes of a view, unless its depth, its kind or the limit of
 * the view leaves it out.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param depth Depth of the box
 * @param leaf The box is the leaf of an entity
 * @param ctx The walk
 * @return false when the boxes below it are left out
 */
static bool box_render_bvh_box(
    const vec3 min,
    const vec3 max,
    int32_t depth,
    bool leaf,
    void *ctx)
{
    box_render_bvh_walk_t *w = ctx;
    int32_t levels = w->settings->levels;
    bool result = (levels <= 0 || depth + 1 < levels) &&
        w->count < BOX_VIEW_DEBUG_BOXES_MAX;
    if ((levels > 0 && depth >= levels) || w->count >= BOX_VIEW_DEBUG_BOXES_MAX ||
        (leaf && !w->settings->leaves))
    {
        goto done;
    }

    box_render_debug_box_t *b = ecs_vec_append_t(NULL, &w->p->debug_boxes,
        box_render_debug_box_t);
    glm_vec3_copy(ECS_CONST_CAST(float*, min), b->min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), b->max);
    b->kind = w->tree | (leaf ? BOX_RENDER_DEBUG_BOX_LEAF : 0u);
    b->depth = (uint32_t)depth;
    b->pose = w->pose;
    b->style = BOX_RENDER_DEBUG_STYLE_TREE;
    b->padding[0] = b->padding[1] = 0;
    w->count ++;
done:
    return result;
}

/** Append the pose of a mover to the poses of a view before the boxes of its tree, unless the
 * view has no room for more boxes.
 *
 * @param mover The mover
 * @param pose The 16 floats of the world transform of the mover
 * @param ctx The walk
 * @return false when the tree of the mover is left out
 */
static bool box_render_bvh_mover(
    ecs_entity_t mover,
    const float *pose,
    void *ctx)
{
    (void)mover;
    box_render_bvh_walk_t *w = ctx;
    bool result = w->count < BOX_VIEW_DEBUG_BOXES_MAX;
    if (result) {
        float *m = ecs_vec_append_t(NULL, &w->p->debug_poses, mat4)[0][0];
        ecs_os_memcpy(m, pose, ECS_SIZEOF(mat4));
        w->pose = (uint32_t)w->poses;
        w->poses ++;
    }

    return result;
}

void box_render_debug_bvh(
    const ecs_world_t *world,
    box_render_packet_t *p)
{
    int32_t i, t, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const BvhDebug *s = &v->debug.bvh;
        bool trees[BOX_SPATIAL_TREES] = { s->static_tree, s->dynamic_tree, s->ui_tree };
        box_render_bvh_walk_t w = { .p = p, .settings = s };
        v->debug_box_first = ecs_vec_count(&p->debug_boxes);
        v->debug_pose_first = ecs_vec_count(&p->debug_poses);
        glm_mat4_identity(*ecs_vec_append_t(NULL, &p->debug_poses, mat4));
        w.poses = 1;
        for (t = 0; t < BOX_SPATIAL_TREES; t ++) {
            if (trees[t]) {
                w.tree = (uint32_t)t;
                box_spatial_tree_walk(world, v->scene, t, box_render_bvh_box, &w);
            }
        }

        if (s->mover_trees) {
            w.tree = BOX_RENDER_DEBUG_BOX_MOVER;
            box_spatial_mover_walk(world, v->scene, box_render_bvh_mover, box_render_bvh_box,
                &w);
        }

        box_render_debug_portals_append(p, v, &w.count, &w.poses);

        v->debug_box_count = w.count;
        v->debug_pose_count = w.count ? w.poses : 0;
        if (!w.count) {
            ecs_vec_set_count_t(NULL, &p->debug_poses, mat4, v->debug_pose_first);
        }
    }
}
