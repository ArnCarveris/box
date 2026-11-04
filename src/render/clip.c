/**
 * @file render/clip.c
 * @brief Clip areas of the ClipChildren elements, which the 2D draws below them clip to.
 *
 * Every clip has an index into the clip buffer that the 2D instances below it hold; its area is
 * computed every frame from the element and uploaded when one changed.
 *
<<<<<<< HEAD
 * Slice records (Rects with an ImageSlice) take two clip records and polygon records (Polygon2D)
 * a header and chunks of five per 14 edges, so the 2D shape shader reads them without a binding
 * of its own; a polygon that outgrows its run of records moves to a free run that it fits.
=======
 * The slice records of the Rects with an ImageSlice take two clip records each, so the 2D shape
 * shader reads them without a binding of their own. They are computed when the shape changed or
 * the texture of its image changed size, as when it finished loading or its Surface resized.
>>>>>>> 97d810b441ddafca67d1b540a835256eb63d9107
 */

#include "draw.h"
#include <math.h>

/** Clips the smallest clip buffer holds */
#define BOX_DRAWS_MIN_CLIPS (16)

_Static_assert(sizeof(box_render_slice_t) == 2 * sizeof(box_render_clip_t),
    "a slice record takes two clip records, as fs_slice of shape2d.wgsl reads it");

_Static_assert(sizeof(box_render_polygon_t) == sizeof(box_render_clip_t),
    "the header of a polygon record takes one clip record, as fs_polygon of shape2d.wgsl reads it");

_Static_assert(sizeof(box_render_clip_t) == 3 * sizeof(vec4),
    "a clip record holds three edges of a polygon, or the bounds of a chunk and two edges");

/** Most ancestors a walk up the hierarchy visits looking for clips */
#define BOX_DRAWS_CLIP_WALK (256)

/** Get the next ancestor whose ClipChildren can clip an entity: its parent, none above a
 * floating element (LayoutFloat), which its ancestors do not clip. A clipped overlay (a
 * LayoutFloat with clip) stays clipped by its ancestors, so the walk goes on to its parent.
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
    if (e) {
        const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);
        if (!fl || fl->clip) {
            result = ecs_get_parent(world, e);
        }
    }

    return result;
}

/** Most hide_clipped floats a check of whether a float hides goes through, its anchor inside
 * another one */
#define BOX_DRAWS_FLOAT_NEST (8)

static bool box_draws_float_hidden(
    const ecs_world_t *world,
    ecs_entity_t e,
    int32_t nest);

/** Get the floating element that hides while its anchor is clipped out (LayoutFloat with
 * hide_clipped and without clip) an entity is, else none.
 *
 * @param world The world
 * @param e The entity
 * @return Its LayoutFloat, NULL when it is no such float
 */
static const LayoutFloat* box_draws_float_hiding(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const LayoutFloat *result = e ? ecs_get(world, e, LayoutFloat) : NULL;
    if (result && (result->clip || !result->hide_clipped)) {
        result = NULL;
    }

    return result;
}

/** Get the bounds of an element: half its LayoutBounds, else half its Rect, 0 for neither.
 *
 * @param world The world
 * @param e The element
 * @param half Receives half its width and half its height
 */
static void box_draws_clip_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2])
{
    const LayoutBounds *lb = ecs_get(world, e, LayoutBounds);
    const Rect *r = lb ? NULL : ecs_get(world, e, Rect);
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
}

float box_render_clip_area(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2])
{
    const Border *b = ecs_get(world, e, Border);
    float result = 0;
    box_draws_clip_bounds(world, e, half);
    if (b) {
        float width = b->width > 0 ? b->width : 0;
        result = glm_clamp(b->radius, 0.0f, glm_min(half[0], half[1]));
        half[0] = glm_max(half[0] - width, 0.0f);
        half[1] = glm_max(half[1] - width, 0.0f);
        result = glm_clamp(result - width, 0.0f, glm_min(half[0], half[1]));
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
    ecs_entity_t top = e, cur = box_draws_clip_up(world, e);
    int32_t depth = 0, clips = 0;
    bool result = true;
    for (; cur && result && depth < BOX_DRAWS_CLIP_WALK && clips < BOX_RENDER_CLIP_DEPTH;
        top = cur, cur = box_draws_clip_up(world, cur))
    {
        if (ecs_has_id(world, cur, ClipChildren)) {
            box_draws_clip_record(world, cur, 0, &clip);
            result = clip.area[0] > 0 && clip.area[1] > 0 &&
                box_draws_clip_distance(&clip, point) <= 0;
            clips ++;
        }

        depth ++;
    }

    if (result && !cur) {
        result = !box_draws_float_hidden(world, top, 0);
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

/** Get the box of an entity a clip hides it by: its bounds (box_draws_clip_bounds), or the bounds
 * of the outline of a polygon, which need not be centered on its origin.
 *
 * @param world The world
 * @param e The entity
 * @param center Receives the center of the box
 * @param half Receives half its width and half its height
 */
static void box_draws_clip_extent(
    const ecs_world_t *world,
    ecs_entity_t e,
    float center[2],
    float half[2])
{
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    center[0] = center[1] = 0;
    if (shape && shape->kind == BOX_SHAPE2D_POLYGON && !ecs_has(world, e, LayoutBounds)) {
        center[0] = shape->center[0];
        center[1] = shape->center[1];
        half[0] = glm_max(shape->size[0] * 0.5f, 0.0f);
        half[1] = glm_max(shape->size[1] * 0.5f, 0.0f);
    } else {
        box_draws_clip_bounds(world, e, half);
    }
}

/** Check whether the ClipChildren ancestors of an entity clip all of it away, or it is inside a
 * hide_clipped float that hides.
 *
 * @param world The world
 * @param e The entity
 * @param scroll Whether the clips that scroll (with a LayoutScroll) count
 * @param nest hide_clipped floats the check already went through
 * @return Whether no part of it is drawn
 */
static bool box_draws_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e,
    bool scroll,
    int32_t nest)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    box_render_clip_t clip;
    vec3 corners[4];
    float half[2], center[2];
    ecs_entity_t top = e, cur = box_draws_clip_up(world, e);
    int32_t i, depth = 0, clips = 0;
    bool result = false;
    box_draws_clip_extent(world, e, center, half);
    for (i = 0; i < 4; i ++) {
        vec3 local = { center[0] + ((i & 1) ? half[0] : -half[0]),
            center[1] + ((i & 2) ? half[1] : -half[1]), 0 };
        if (t) {
            glm_mat4_mulv3(ECS_CONST_CAST(vec4*, t->m), local, 1.0f, corners[i]);
        } else {
            glm_vec3_copy(local, corners[i]);
        }
    }

    for (; cur && !result && depth < BOX_DRAWS_CLIP_WALK && clips < BOX_RENDER_CLIP_DEPTH;
        top = cur, cur = box_draws_clip_up(world, cur))
    {
        if (ecs_has_id(world, cur, ClipChildren) &&
            (scroll || !ecs_has(world, cur, LayoutScroll)))
        {
            box_draws_clip_record(world, cur, 0, &clip);
            result = box_draws_clip_misses(&clip, ECS_CONST_CAST(const vec3*, corners));
            clips ++;
        }

        depth ++;
    }

    if (!result && !cur) {
        result = box_draws_float_hidden(world, top, nest);
    }

    return result;
}

/** Check whether an entity is a hide_clipped float whose anchor is clipped out.
 *
 * @param world The world
 * @param e The entity
 * @param nest hide_clipped floats the check already went through
 * @return Whether it hides
 */
static bool box_draws_float_hidden(
    const ecs_world_t *world,
    ecs_entity_t e,
    int32_t nest)
{
    const LayoutFloat *fl = nest < BOX_DRAWS_FLOAT_NEST ? box_draws_float_hiding(world, e) : NULL;
    bool result = false;
    if (fl) {
        ecs_entity_t anchor = fl->anchor;
        if (!anchor || anchor == e || !ecs_is_alive(world, anchor)) {
            anchor = ecs_get_parent(world, e);
        }

        result = anchor && box_draws_clip_hides(world, anchor, true, nest + 1);
    }

    return result;
}

bool box_render_float_hidden(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return box_draws_float_hidden(world, e, 0);
}

bool box_render_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return box_draws_clip_hides(world, e, false, 0);
}

void box_draws_clips_init(
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    ecs_map_init(&c->index, NULL);
    ecs_map_init(&c->floats, NULL);
    ecs_vec_init_t(NULL, &c->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &c->records, box_render_clip_t, 0);
    ecs_vec_init_t(NULL, &c->free, int32_t, 0);
    ecs_map_init(&c->slices, NULL);
    ecs_vec_init_t(NULL, &c->slice_entries, box_draw_slice_t, 0);
    ecs_vec_init_t(NULL, &c->slice_free, int32_t, 0);
    ecs_map_init(&c->polygons, NULL);
    ecs_vec_init_t(NULL, &c->polygon_entries, box_draw_polygon_t, 0);
    ecs_vec_init_t(NULL, &c->polygon_free, box_draw_polygon_run_t, 0);
    ecs_vec_init_t(NULL, &c->polygon_edges, vec4, 0);
    *ecs_vec_append_t(NULL, &c->entities, ecs_entity_t) = 0;
    ecs_os_zeromem(ecs_vec_append_t(NULL, &c->records, box_render_clip_t));
}

void box_draws_clips_fini(
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    ecs_map_fini(&c->index);
    ecs_map_fini(&c->floats);
    ecs_vec_fini_t(NULL, &c->entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &c->records, box_render_clip_t);
    ecs_vec_fini_t(NULL, &c->free, int32_t);
    ecs_map_fini(&c->slices);
    ecs_vec_fini_t(NULL, &c->slice_entries, box_draw_slice_t);
    ecs_vec_fini_t(NULL, &c->slice_free, int32_t);
    ecs_map_fini(&c->polygons);
    ecs_vec_fini_t(NULL, &c->polygon_entries, box_draw_polygon_t);
    ecs_vec_fini_t(NULL, &c->polygon_free, box_draw_polygon_run_t);
    ecs_vec_fini_t(NULL, &c->polygon_edges, vec4);
}

/** Get the clip index of an element, giving it one when it has none.
 *
 * @param c The clips
 * @param map The map of the kind of clip: c->index for ClipChildren, c->floats for a
 * hide_clipped float
 * @param e The element
 * @return The clip index
 */
static int32_t box_draws_clip_index(
    box_draw_clips_t *c,
    ecs_map_t *map,
    ecs_entity_t e)
{
    uint64_t *found = ecs_map_get(map, e);
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
    ecs_map_insert(map, e, (uint64_t)result);
    c->dirty = true;
done:
    return result;
}

int32_t box_draws_clip_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    ecs_entity_t top = e, cur = d->clips.tagged > 0 ? box_draws_clip_up(world, e) : 0;
    int32_t depth = 0, result = 0;
    for (; cur && depth < BOX_DRAWS_CLIP_WALK; top = cur, cur = box_draws_clip_up(world, cur)) {
        if (ecs_has_id(world, cur, ClipChildren)) {
            result = box_draws_clip_index(&d->clips, &d->clips.index, cur);
            break;
        }

        depth ++;
    }

    if (!result && !cur && d->clips.tagged > 0 && box_draws_float_hiding(world, top)) {
        result = box_draws_clip_index(&d->clips, &d->clips.floats, top);
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

int32_t box_draws_slice_of(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_clips_t *c = &d->clips;
    uint64_t *found = ecs_map_get(&c->slices, e);
    box_draw_slice_t *entry;
    int32_t pos;
    if (found) {
        entry = ecs_vec_get_t(&c->slice_entries, box_draw_slice_t, (int32_t)*found);
        goto done;
    }

    if (ecs_vec_count(&c->slice_free)) {
        pos = *ecs_vec_last_t(&c->slice_free, int32_t);
        ecs_vec_remove_last(&c->slice_free);
        entry = ecs_vec_get_t(&c->slice_entries, box_draw_slice_t, pos);
    } else {
        pos = ecs_vec_count(&c->slice_entries);
        entry = ecs_vec_append_t(NULL, &c->slice_entries, box_draw_slice_t);
        entry->index = ecs_vec_count(&c->entities);
        *ecs_vec_append_t(NULL, &c->entities, ecs_entity_t) = 0;
        *ecs_vec_append_t(NULL, &c->entities, ecs_entity_t) = 0;
        ecs_os_zeromem(ecs_vec_append_t(NULL, &c->records, box_render_clip_t));
        ecs_os_zeromem(ecs_vec_append_t(NULL, &c->records, box_render_clip_t));
    }

    entry->entity = e;
    ecs_map_insert(&c->slices, e, (uint64_t)pos);
done:
    entry->dirty = true;

    return entry->index;
}

void box_draws_slice_release(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_clips_t *c = &d->clips;
    uint64_t *found = ecs_map_get(&c->slices, e);
    if (found) {
        int32_t pos = (int32_t)*found;
        box_draw_slice_t *entry = ecs_vec_get_t(&c->slice_entries, box_draw_slice_t, pos);
        entry->entity = 0;
        *ecs_vec_append_t(NULL, &c->slice_free, int32_t) = pos;
        ecs_map_remove(&c->slices, e);
    }
}

/** Get the size in texels of the image of a sliced 2D shape: its Texture, or the image of the
 * Surface that renders into it (box_surface_texture_size).
 *
 * @param world The world
 * @param texture The texture entity of the Image, 0 for none
 * @param size Receives the width and height, 0 while the texture has no size
 */
static void box_draws_slice_texture(
    const ecs_world_t *world,
    ecs_entity_t texture,
    int32_t size[2])
{
    const Texture *t = texture && ecs_is_alive(world, texture)
        ? ecs_get(world, texture, Texture) : NULL;
    size[0] = t ? t->width : 0;
    size[1] = t ? t->height : 0;
    if (!t && texture && ecs_is_alive(world, texture)) {
        box_surface_texture_size(world, texture, &size[0], &size[1]);
    }
}

bool box_draws_slice_record(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_render_slice_t *out)
{
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    const Image *image = shape && shape->sliced ? ecs_get(world, e, Image) : NULL;
    const ImageSlice *slice = image ? ecs_get(world, e, ImageSlice) : NULL;
    box_image_slice_t parts;
    int32_t i, size[2];
    bool result = slice != NULL;
    ecs_os_zeromem(out);
    if (!result) {
        goto done;
    }

    float w = shape->size[0], h = shape->size[1];
    box_draws_slice_texture(world, image->texture, size);
    box_image_slice_resolve(image, slice, (float)size[0], (float)size[1], w, h, &parts);
    w = w > 0.0f ? w : 1.0f;
    h = h > 0.0f ? h : 1.0f;
    for (i = 0; i < 4; i ++) {
        out->x[i] = parts.x[i] / w;
        out->y[i] = parts.y[i] / h;
        out->u[i] = parts.u[i];
        out->v[i] = parts.v[i];
    }

    out->period[0] = parts.period[0] / w;
    out->period[1] = parts.period[1] / h;
    out->period[2] = parts.period[2] / w;
    out->period[3] = parts.period[3] / h;
    out->flags[0] = parts.hollow ? 1.0f : 0.0f;
done:
    return result;
}

/** Compute the slice records of the shapes that changed and of those whose texture changed
 * size since their record was computed.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_slices_update(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    box_draw_slice_t *entries = ecs_vec_first(&c->slice_entries);
    int32_t i, count = ecs_vec_count(&c->slice_entries), size[2];
    for (i = 0; i < count; i ++) {
        box_draw_slice_t *entry = &entries[i];
        if (!entry->entity) {
            continue;
        }

        if (!ecs_is_alive(world, entry->entity)) {
            box_draws_slice_release(d, entry->entity);
            continue;
        }

        if (!entry->dirty) {
            box_draws_slice_texture(world, entry->texture, size);
            if (size[0] == entry->width && size[1] == entry->height) {
                continue;
            }
        }

        box_render_slice_t slice;
        const Image *image = ecs_get(world, entry->entity, Image);
        entry->texture = image ? image->texture : 0;
        box_draws_slice_texture(world, entry->texture, size);
        entry->width = size[0];
        entry->height = size[1];
        entry->dirty = false;
        box_draws_slice_record(world, entry->entity, &slice);
        box_render_clip_t *cur = ecs_vec_get_t(&c->records, box_render_clip_t, entry->index);
        if (ecs_os_memcmp(cur, &slice, ECS_SIZEOF(slice))) {
            ecs_os_memcpy(cur, &slice, ECS_SIZEOF(slice));
            c->dirty = true;
        }
    }
}

/** Take a run of consecutive clip records for a polygon record: the first free run it fits,
 * else new records at the end of the buffer.
 *
 * @param c The clips
 * @param count Clip records
 * @return The first clip index of the run
 */
static int32_t box_draws_polygon_alloc(
    box_draw_clips_t *c,
    int32_t count)
{
    box_draw_polygon_run_t *runs = ecs_vec_first(&c->polygon_free);
    int32_t i, result = -1, runs_count = ecs_vec_count(&c->polygon_free);
    for (i = 0; i < runs_count; i ++) {
        if (runs[i].count >= count) {
            result = runs[i].index;
            runs[i].index += count;
            runs[i].count -= count;
            if (!runs[i].count) {
                runs[i] = runs[runs_count - 1];
                ecs_vec_remove_last(&c->polygon_free);
            }

            break;
        }
    }

    if (result < 0) {
        result = ecs_vec_count(&c->entities);
        for (i = 0; i < count; i ++) {
            *ecs_vec_append_t(NULL, &c->entities, ecs_entity_t) = 0;
            ecs_os_zeromem(ecs_vec_append_t(NULL, &c->records, box_render_clip_t));
        }
    }

    return result;
}

/** Leave the run of a polygon entry to the polygons that fit it.
 *
 * @param c The clips
 * @param entry The entry
 */
static void box_draws_polygon_free(
    box_draw_clips_t *c,
    box_draw_polygon_t *entry)
{
    if (entry->count) {
        *ecs_vec_append_t(NULL, &c->polygon_free, box_draw_polygon_run_t) =
            (box_draw_polygon_run_t){ .index = entry->index, .count = entry->count };
    }

    entry->index = 0;
    entry->count = 0;
}

int32_t box_draws_polygon_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    bool changed)
{
    box_draw_clips_t *c = &d->clips;
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    int32_t edges = shape ? glm_clamp(shape->edges, 0, BOX_POLYGON2D_MAX_EDGES) : 0;
    int32_t need = BOX_RENDER_POLYGON_RECORDS(edges);
    uint64_t *found = ecs_map_get(&c->polygons, e);
    box_draw_polygon_t *entry;
    if (found) {
        entry = ecs_vec_get_t(&c->polygon_entries, box_draw_polygon_t, (int32_t)*found);
    } else {
        ecs_map_insert(&c->polygons, e, (uint64_t)ecs_vec_count(&c->polygon_entries));
        entry = ecs_vec_append_t(NULL, &c->polygon_entries, box_draw_polygon_t);
        ecs_os_zeromem(entry);
        entry->entity = e;
        changed = true;
    }

    if (entry->count < need) {
        box_draws_polygon_free(c, entry);
        entry->index = box_draws_polygon_alloc(c, need);
        entry->count = need;
        changed = true;
    }

    entry->dirty |= changed;

    return entry->index;
}

void box_draws_polygon_release(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_clips_t *c = &d->clips;
    uint64_t *found = ecs_map_get(&c->polygons, e);
    if (found) {
        int32_t pos = (int32_t)*found, last = ecs_vec_count(&c->polygon_entries) - 1;
        box_draw_polygon_t *entries = ecs_vec_first(&c->polygon_entries);
        box_draws_polygon_free(c, &entries[pos]);
        ecs_map_remove(&c->polygons, e);
        if (pos != last) {
            entries[pos] = entries[last];
            *ecs_map_ensure(&c->polygons, entries[pos].entity) = (uint64_t)pos;
        }

        ecs_vec_remove_last(&c->polygon_entries);
    }
}

/** Compute the bounds of the edges of a chunk of a polygon record.
 *
 * @param edges The edges of the polygon
 * @param first First edge of the chunk
 * @param count Edges of the polygon
 * @param out Receives min x, y and max x, y, zero for a chunk without edges
 */
static void box_draws_polygon_chunk(
    const vec4 *edges,
    int32_t first,
    int32_t count,
    vec4 out)
{
    int32_t i, last = glm_min(first + BOX_RENDER_POLYGON_CHUNK_EDGES, count);
    glm_vec4_zero(out);
    for (i = first; i < last; i ++) {
        const float *e = edges[i];
        float lo[2] = { glm_min(e[0], e[2]), glm_min(e[1], e[3]) };
        float hi[2] = { glm_max(e[0], e[2]), glm_max(e[1], e[3]) };
        out[0] = i == first ? lo[0] : glm_min(out[0], lo[0]);
        out[1] = i == first ? lo[1] : glm_min(out[1], lo[1]);
        out[2] = i == first ? hi[0] : glm_max(out[2], hi[0]);
        out[3] = i == first ? hi[1] : glm_max(out[3], hi[1]);
    }
}

/** Compute the polygon record of a polygon into its run: the bounds of its outline and as many
 * of its edges as the run holds, in chunks that start with their bounds. A shape that is no
 * longer a polygon gets no edges.
 *
 * @param world The world
 * @param c The clips
 * @param entry The entry
 */
static void box_draws_polygon_record(
    const ecs_world_t *world,
    box_draw_clips_t *c,
    const box_draw_polygon_t *entry)
{
    const Shape2D *shape = ecs_get(world, entry->entity, Shape2D);
    const Polygon2D *polygon = shape && shape->kind == BOX_SHAPE2D_POLYGON
        ? ecs_get(world, entry->entity, Polygon2D) : NULL;
    box_render_clip_t *records = ecs_vec_get_t(&c->records, box_render_clip_t, entry->index);
    box_render_polygon_t header = {0};
    int32_t i, count = 0, capacity = (entry->count - 1) / BOX_RENDER_POLYGON_CHUNK_RECORDS *
        BOX_RENDER_POLYGON_CHUNK_EDGES;
    bool open = false;
    ecs_vec_clear(&c->polygon_edges);
    if (polygon && box_shape2d_polygon_edges(world, polygon, &c->polygon_edges, &open)) {
        count = glm_min(ecs_vec_count(&c->polygon_edges), capacity);
        header.bounds[0] = shape->center[0];
        header.bounds[1] = shape->center[1];
        header.bounds[2] = shape->size[0] * 0.5f;
        header.bounds[3] = shape->size[1] * 0.5f;
        header.shape[1] = open ? shape->width * 0.5f : 0.0f;
    }

    header.shape[0] = (float)count;
    bool changed = ecs_os_memcmp(&records[0], &header, ECS_SIZEOF(header)) != 0;
    ecs_os_memcpy(&records[0], &header, ECS_SIZEOF(header));

    const vec4 *edges = ecs_vec_first(&c->polygon_edges);
    for (i = 0; i < entry->count - 1; i ++) {
        box_render_clip_t record = {0};
        vec4 *dst = (vec4*)&record;
        int32_t k;
        for (k = 0; k < 3; k ++) {
            int32_t slot = i * 3 + k;
            int32_t chunk = slot / (BOX_RENDER_POLYGON_CHUNK_EDGES + 1);
            int32_t at = slot % (BOX_RENDER_POLYGON_CHUNK_EDGES + 1);
            int32_t edge = chunk * BOX_RENDER_POLYGON_CHUNK_EDGES + at - 1;
            if (!at) {
                box_draws_polygon_chunk(edges, edge + 1, count, dst[k]);
            } else if (edge < count) {
                glm_vec4_copy(ECS_CONST_CAST(float*, edges[edge]), dst[k]);
            }
        }

        if (ecs_os_memcmp_t(&records[i + 1], &record, box_render_clip_t)) {
            records[i + 1] = record;
            changed = true;
        }
    }

    if (changed && c->changed_end > c->changed_first) {
        c->changed_first = glm_min(c->changed_first, entry->index);
        c->changed_end = glm_max(c->changed_end, entry->index + entry->count);
    } else if (changed) {
        c->changed_first = entry->index;
        c->changed_end = entry->index + entry->count;
    }
}

/** Compute the polygon records of the polygons that changed and free those of the polygons that
 * are gone.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_polygons_update(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draw_clips_t *c = &d->clips;
    int32_t i;
    for (i = ecs_vec_count(&c->polygon_entries) - 1; i >= 0; i --) {
        box_draw_polygon_t *entry = ecs_vec_get_t(&c->polygon_entries, box_draw_polygon_t, i);
        if (!ecs_is_alive(world, entry->entity)) {
            box_draws_polygon_release(d, entry->entity);
            continue;
        }

        if (entry->dirty) {
            entry->dirty = false;
            box_draws_polygon_record(world, c, entry);
        }
    }
}

/** Compute the record of the clip of a hide_clipped float, which its draws and those of its
 * descendants nest in: an empty area while its anchor is clipped out, else one that contains
 * every position.
 *
 * @param world The world
 * @param e The float
 * @param out Receives the record
 */
static void box_draws_float_record(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_render_clip_t *out)
{
    ecs_os_zeromem(out);
    if (!box_draws_float_hidden(world, e, 0)) {
        out->x[0] = 1;
        out->y[1] = 1;
        out->area[0] = out->area[1] = 1e30f;
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

        uint64_t *fl = ecs_map_get(&c->floats, e);
        if (fl && *fl == (uint64_t)i) {
            if (!ecs_is_alive(world, e) || !box_draws_float_hiding(world, e)) {
                *ecs_vec_get_t(&c->entities, ecs_entity_t, i) = 0;
                *ecs_vec_append_t(NULL, &c->free, int32_t) = i;
                ecs_map_remove(&c->floats, e);
                continue;
            }

            box_draws_float_record(world, e, &record);
        } else {
            box_draws_clip_record(world, e, box_draws_clip_of(world, d, e), &record);
        }

        box_render_clip_t *cur = ecs_vec_get_t(&c->records, box_render_clip_t, i);
        if (ecs_os_memcmp_t(cur, &record, box_render_clip_t)) {
            *cur = record;
            c->dirty = true;
        }
    }

    box_draws_slices_update(world, d);
    box_draws_polygons_update(world, d);
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

    uint32_t first = 0;
    bool upload = c->dirty;
    if (!upload && c->changed_end > c->changed_first) {
        first = (uint32_t)c->changed_first;
        count = (uint32_t)c->changed_end;
        upload = true;
    }

    if (upload && c->capacity) {
        uint32_t stride = (uint32_t)sizeof(box_render_clip_t);
        uint32_t size = (count - first) * stride;
        void *dst = box_render_packet_upload(p, BOX_RENDER_BUFFER_CLIPS, first * stride, size);
        ecs_os_memcpy(dst, ecs_vec_get_t(&c->records, box_render_clip_t, (int32_t)first),
            (ecs_size_t)size);
        d->stats.clip_bytes = size;
        c->dirty = false;
    }

    c->changed_first = 0;
    c->changed_end = 0;
}

void box_draws_clips_reset(
    box_render_draws_t *d)
{
    d->clips.capacity = 0;
}
