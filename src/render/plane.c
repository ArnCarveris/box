/**
 * @file render/plane.c
 * @brief Plane records of the layer groups of the 2D draws: the plane of the anchor of a group,
 * which every layer of the group is drawn on.
 *
 * A layer group is a 2D shape with 2D shapes or texts below it, such as a screen and its labels.
 * Each world transform rounds to f32 on its own, which puts a layer some 1e-7 of its distance
 * from the local origin off its anchor, more than the rank lift of shape2d.wgsl far out.
 *
 * So shape2d.wgsl places every layer of a group on the plane record of its anchor (plane_position)
 * and the rank alone orders them. The records live in the clip buffer, are computed every frame
 * with the clips and freed when the order no longer has the group.
 */

#include "draw.h"

_Static_assert(sizeof(box_render_plane_t) == sizeof(box_render_clip_t),
    "a plane record takes one clip record, as plane_position of shape2d.wgsl reads it");

/** Most ancestors a walk up the hierarchy visits looking for the anchor of a group */
#define BOX_DRAWS_PLANE_WALK (256)

/** Check whether an entity draws a 2D shape.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @return Whether it has a slot in a 2D batch
 */
static bool box_draws_plane_shape(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const DrawSlot *s = ecs_get(world, e, DrawSlot);
    bool result = false;
    if (s && s->batch >= 0 && s->batch < ecs_vec_count(&d->batches)) {
        result = box_draws_batch(d, s->batch)->key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
    }

    return result;
}

ecs_entity_t box_draws_plane_anchor(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    ecs_entity_t result = 0, cur = e;
    int32_t depth = 0;
    for (; cur && depth < BOX_DRAWS_PLANE_WALK; cur = ecs_get_parent(world, cur)) {
        if (box_draws_plane_shape(world, d, cur)) {
            result = cur;
        }

        depth ++;
    }

    return result;
}

int32_t box_draws_plane_of(
    box_render_draws_t *d,
    ecs_entity_t anchor)
{
    return box_draws_clip_index(&d->clips, &d->clips.planes, anchor);
}

void box_draws_planes_keep(
    box_render_draws_t *d,
    const ecs_map_t *anchors)
{
    box_draw_clips_t *c = &d->clips;
    ecs_vec_t gone;
    ecs_vec_init_t(NULL, &gone, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&c->planes);
    while (ecs_map_next(&it)) {
        ecs_entity_t anchor = (ecs_entity_t)ecs_map_key(&it);
        if (!ecs_map_get(anchors, anchor)) {
            *ecs_vec_append_t(NULL, &gone, ecs_entity_t) = anchor;
        }
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&gone); i ++) {
        box_draws_clip_free(c, &c->planes, *ecs_vec_get_t(&gone, ecs_entity_t, i));
    }

    ecs_vec_fini_t(NULL, &gone, ecs_entity_t);
}

void box_draws_plane_record(
    const ecs_world_t *world,
    ecs_entity_t anchor,
    box_render_clip_t *out)
{
    WorldTransform t_buf;
    const WorldTransform *t = box_transform_world(world, anchor, &t_buf);
    box_render_plane_t record;
    vec3 normal;
    ecs_os_zeromem(&record);
    if (t) {
        glm_vec3_cross(ECS_CONST_CAST(float*, t->m[0]), ECS_CONST_CAST(float*, t->m[1]), normal);
        record.point[0] = t->m[3][0];
        record.point[1] = t->m[3][1];
        record.point[2] = t->m[3][2];
        if (glm_vec3_norm2(normal) > 1e-30f) {
            glm_vec3_normalize(normal);
            record.normal[0] = normal[0];
            record.normal[1] = normal[1];
            record.normal[2] = normal[2];
        }
    }

    ecs_os_memcpy_t(out, &record, box_render_clip_t);
}
