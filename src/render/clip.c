/**
 * @file render/clip.c
 * @brief Clip areas of the ClipChildren elements, which the 2D draws below them clip to.
 *
 * Every clip has an index into the clip buffer that the 2D instances below it hold; its area is
 * computed every frame from the element and uploaded when one changed.
 */

#include "draw.h"
#include <math.h>

/** Clips the smallest clip buffer holds */
#define BOX_DRAWS_MIN_CLIPS (16)

/** Most ancestors a walk up the hierarchy visits looking for clips */
#define BOX_DRAWS_CLIP_WALK (256)

/** Get the next ancestor whose ClipChildren can clip an entity: its parent, none above a
 * floating element (LayoutFloat), which its ancestors do not clip.
 *
 * @param world The world
 * @param e The entity
 * @return The parent, 0 for a floating entity or a root
 */
static ecs_entity_t box_draws_clip_up(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    if (e && !ecs_has(world, e, LayoutFloat)) {
        result = ecs_get_parent(world, e);
    }

    return result;
}

float box_render_clip_area(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2])
{
    const LayoutBounds *lb = ecs_get(world, e, LayoutBounds);
    const Rect *r = lb ? NULL : ecs_get(world, e, Rect);
    const Border *b = ecs_get(world, e, Border);
    float result = 0;
    half[0] = half[1] = 0;
    if (lb) {
        half[0] = lb->width * 0.5f;
        half[1] = lb->height * 0.5f;
    } else if (r) {
        half[0] = r->width * 0.5f;
        half[1] = r->height * 0.5f;
    }

    half[0] = half[0] > 0 ? half[0] : 0;
    half[1] = half[1] > 0 ? half[1] : 0;
    if (b) {
        result = glm_clamp(b->radius, 0.0f, glm_min(half[0], half[1]));
    }

    return result;
}

/** Compute the record of a clip: the map into the local XY plane of the element and its area.
 *
 * A transform whose X and Y axes span no plane maps every position outside the area.
 *
 * @param world The world
 * @param e The element
 * @param parent Clip index of the clip the element is nested in, 0 for none
 * @param out Receives the record
 */
static void box_draws_clip_record(
    const ecs_world_t *world,
    ecs_entity_t e,
    int32_t parent,
    box_render_clip_t *out)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    mat4 m, inv;
    vec3 normal;
    int32_t k;
    ecs_os_zeromem(out);
    if (t) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), m);
    } else {
        glm_mat4_identity(m);
    }

    glm_vec3_cross(m[0], m[1], normal);
    if (glm_vec3_norm2(m[2]) <= 1e-24f) {
        glm_vec3_normalize_to(normal, m[2]);
    }

    float det = glm_mat4_det(m);
    if (glm_vec3_norm2(normal) > 1e-30f && isfinite(det) && fabsf(det) > 1e-30f) {
        glm_mat4_inv(m, inv);
        for (k = 0; k < 4; k ++) {
            out->x[k] = inv[k][0];
            out->y[k] = inv[k][1];
        }
    } else {
        out->x[3] = 1e30f;
    }

    out->area[2] = box_render_clip_area(world, e, out->area);
    out->area[3] = (float)parent;
}

/** Get the signed distance of a world position to the area of a clip, negative inside.
 *
 * @param c The record of the clip
 * @param p The world position
 * @return The distance in the local units of the element
 */
static float box_draws_clip_distance(
    const box_render_clip_t *c,
    const float p[3])
{
    float x = c->x[0] * p[0] + c->x[1] * p[1] + c->x[2] * p[2] + c->x[3];
    float y = c->y[0] * p[0] + c->y[1] * p[1] + c->y[2] * p[2] + c->y[3];
    float r = c->area[2];
    float qx = fabsf(x) - c->area[0] + r, qy = fabsf(y) - c->area[1] + r;
    float ox = glm_max(qx, 0.0f), oy = glm_max(qy, 0.0f);

    return sqrtf(ox * ox + oy * oy) + glm_min(glm_max(qx, qy), 0.0f) - r;
}

bool box_render_clip_contains(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float point[3])
{
    box_render_clip_t clip;
    ecs_entity_t cur = box_draws_clip_up(world, e);
    int32_t depth = 0, clips = 0;
    bool result = true;
    for (; cur && result && depth < BOX_DRAWS_CLIP_WALK && clips < BOX_RENDER_CLIP_DEPTH;
        cur = box_draws_clip_up(world, cur))
    {
        if (ecs_has_id(world, cur, ClipChildren)) {
            box_draws_clip_record(world, cur, 0, &clip);
            result = clip.area[0] > 0 && clip.area[1] > 0 &&
                box_draws_clip_distance(&clip, point) <= 0;
            clips ++;
        }

        depth ++;
    }

    return result;
}

/** Check whether the bounds of an entity, as corners in world space, miss the area of a clip:
 * their box in the local plane of the clip has no overlap of positive size with the area.
 *
 * @param c The record of the clip
 * @param corners The four corners of the bounds in world space
 * @return Whether the clip hides all of the bounds
 */
static bool box_draws_clip_misses(
    const box_render_clip_t *c,
    const vec3 corners[4])
{
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        const float *p = corners[i];
        float x = c->x[0] * p[0] + c->x[1] * p[1] + c->x[2] * p[2] + c->x[3];
        float y = c->y[0] * p[0] + c->y[1] * p[1] + c->y[2] * p[2] + c->y[3];
        lo[0] = glm_min(lo[0], x);
        lo[1] = glm_min(lo[1], y);
        hi[0] = glm_max(hi[0], x);
        hi[1] = glm_max(hi[1], y);
    }

    return c->area[0] <= 0 || c->area[1] <= 0 || lo[0] >= c->area[0] ||
        hi[0] <= -c->area[0] || lo[1] >= c->area[1] || hi[1] <= -c->area[1];
}

bool box_render_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    box_render_clip_t clip;
    vec3 corners[4];
    float half[2];
    ecs_entity_t cur = box_draws_clip_up(world, e);
    int32_t i, depth = 0, clips = 0;
    bool result = false;
    box_render_clip_area(world, e, half);
    for (i = 0; i < 4; i ++) {
        vec3 local = { (i & 1) ? half[0] : -half[0], (i & 2) ? half[1] : -half[1], 0 };
        if (t) {
            glm_mat4_mulv3(ECS_CONST_CAST(vec4*, t->m), local, 1.0f, corners[i]);
        } else {
            glm_vec3_copy(local, corners[i]);
        }
    }

    for (; cur && !result && depth < BOX_DRAWS_CLIP_WALK && clips < BOX_RENDER_CLIP_DEPTH;
        cur = box_draws_clip_up(world, cur))
    {
        if (ecs_has_id(world, cur, ClipChildren) &&
            !ecs_has(world, cur, LayoutScroll))
        {
            box_draws_clip_record(world, cur, 0, &clip);
            result = box_draws_clip_misses(&clip, ECS_CONST_CAST(const vec3*, corners));
            clips ++;
        }

        depth ++;
    }

    return result;
}

void box_draws_clips_init(
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    ecs_map_init(&c->index, NULL);
    ecs_vec_init_t(NULL, &c->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &c->records, box_render_clip_t, 0);
    ecs_vec_init_t(NULL, &c->free, int32_t, 0);
    *ecs_vec_append_t(NULL, &c->entities, ecs_entity_t) = 0;
    ecs_os_zeromem(ecs_vec_append_t(NULL, &c->records, box_render_clip_t));
}

void box_draws_clips_fini(
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    ecs_map_fini(&c->index);
    ecs_vec_fini_t(NULL, &c->entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &c->records, box_render_clip_t);
    ecs_vec_fini_t(NULL, &c->free, int32_t);
}

/** Get the clip index of an element, giving it one when it has none.
 *
 * @param c The clips
 * @param e The element
 * @return The clip index
 */
static int32_t box_draws_clip_index(
    box_draw_clips_t *c,
    ecs_entity_t e)
{
    uint64_t *found = ecs_map_get(&c->index, e);
    int32_t result;
    if (found) {
        result = (int32_t)*found;
        goto done;
    }

    if (ecs_vec_count(&c->free)) {
        result = *ecs_vec_last_t(&c->free, int32_t);
        ecs_vec_remove_last(&c->free);
    } else {
        result = ecs_vec_count(&c->entities);
        ecs_vec_append_t(NULL, &c->entities, ecs_entity_t);
        ecs_vec_append_t(NULL, &c->records, box_render_clip_t);
    }

    *ecs_vec_get_t(&c->entities, ecs_entity_t, result) = e;
    ecs_os_zeromem(ecs_vec_get_t(&c->records, box_render_clip_t, result));
    ecs_map_insert(&c->index, e, (uint64_t)result);
    c->dirty = true;
done:
    return result;
}

int32_t box_draws_clip_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    ecs_entity_t cur = d->clips.tagged > 0 ? box_draws_clip_up(world, e) : 0;
    int32_t depth = 0, result = 0;
    for (; cur && depth < BOX_DRAWS_CLIP_WALK; cur = box_draws_clip_up(world, cur)) {
        if (ecs_has_id(world, cur, ClipChildren)) {
            result = box_draws_clip_index(&d->clips, cur);
            break;
        }

        depth ++;
    }

    return result;
}

void box_draws_clip_added(
    box_render_draws_t *d)
{
    d->clips.tagged ++;
}

void box_draws_clip_removed(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_clips_t *c = &d->clips;
    uint64_t *found = ecs_map_get(&c->index, e);
    c->tagged --;
    if (found) {
        int32_t index = (int32_t)*found;
        *ecs_vec_get_t(&c->entities, ecs_entity_t, index) = 0;
        *ecs_vec_append_t(NULL, &c->free, int32_t) = index;
        ecs_map_remove(&c->index, e);
    }
}

void box_draws_clips_send(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_draw_clips_t *c = &d->clips;
    box_render_clip_t record;
    int32_t i;
    d->stats.clip_bytes = 0;
    for (i = 1; i < ecs_vec_count(&c->entities); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&c->entities, ecs_entity_t, i);
        if (!e) {
            continue;
        }

        box_draws_clip_record(world, e, box_draws_clip_of(world, d, e), &record);
        box_render_clip_t *cur = ecs_vec_get_t(&c->records, box_render_clip_t, i);
        if (ecs_os_memcmp_t(cur, &record, box_render_clip_t)) {
            *cur = record;
            c->dirty = true;
        }
    }

    d->stats.clips = (int32_t)ecs_map_count(&c->index);
    uint32_t count = (uint32_t)ecs_vec_count(&c->records);
    if (count > 1 && count > c->capacity) {
        uint32_t capacity = c->capacity ? c->capacity : BOX_DRAWS_MIN_CLIPS;
        while (capacity < count) {
            capacity *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_CLIPS, sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)capacity * sizeof(box_render_clip_t);
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        c->capacity = capacity;
        c->dirty = true;
    }

    if (c->dirty && c->capacity) {
        uint32_t size = count * (uint32_t)sizeof(box_render_clip_t);
        void *dst = box_render_packet_upload(p, BOX_RENDER_BUFFER_CLIPS, 0, size);
        ecs_os_memcpy(dst, ecs_vec_first(&c->records), (ecs_size_t)size);
        d->stats.clip_bytes = size;
        c->dirty = false;
    }
}

void box_draws_clips_reset(
    box_render_draws_t *d)
{
    d->clips.capacity = 0;
}
