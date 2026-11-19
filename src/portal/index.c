/**
 * @file portal/index.c
 * @brief Builds the areas, portals and occluders of a world from their components, in world
 * space, and answers which area holds a point.
 *
 * The first query after a VisArea, VisPortal or VisOccluder was set or removed, or a static
 * transform that placed one moved, builds the index again; what a mover carries is only placed
 * where the mover is (portal/mover.c).
 *
 * A VisPortal set with only its open state changed (open, slide, locked, glass) updates its portal
 * in place, so doors that move do not build the index again.
 */

#include "portal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>

/** Difference below which two coordinates of an outline are the same */
#define BOX_PORTAL_OUTLINE_EPSILON (1e-4f)

/** Edges of an outline that cross one slab of its box decomposition at most */
#define BOX_PORTAL_SLAB_EDGES_MAX (32)

/** Distance from the center of a portal at which the areas on its two sides are probed */
#define BOX_PORTAL_PROBE (0.25f)

ECS_COMPONENT_DECLARE(PortalIndex);

/** What the observers and the transform callback of the index share */
typedef struct box_portal_index_ref_t {
    box_portal_index_t *index; /**< The index */
    int32_t refs;              /**< Observers and callbacks that hold it */
} box_portal_index_ref_t;

/** Free the index when the last observer or callback that holds it is deleted.
 *
 * @param ctx The box_portal_index_ref_t
 */
static void box_portal_index_release(
    void *ctx)
{
    box_portal_index_ref_t *ref = ctx;
    ref->refs --;

    if (!ref->refs) {
        box_portal_index_t *index = ref->index;
        ecs_vec_fini_t(NULL, &index->areas, box_portal_area_t);
        ecs_vec_fini_t(NULL, &index->points, vec2);
        ecs_vec_fini_t(NULL, &index->boxes, box_portal_box_t);
        ecs_vec_fini_t(NULL, &index->portals, box_portal_portal_t);
        ecs_vec_fini_t(NULL, &index->links, int32_t);
        ecs_vec_fini_t(NULL, &index->occluders, box_portal_occluder_t);
        ecs_vec_fini_t(NULL, &index->movers, box_portal_mover_t);
        ecs_vec_fini_t(NULL, &index->rest_areas, box_portal_area_t);
        ecs_vec_fini_t(NULL, &index->rest_boxes, box_portal_box_t);
        ecs_vec_fini_t(NULL, &index->rest_portals, box_portal_portal_t);
        ecs_vec_fini_t(NULL, &index->rest_occluders, box_portal_occluder_t);
        ecs_map_fini(&index->frames);
        box_portal_freeze_fini(index);
        ecs_os_free(index);
        ecs_os_free(ref);
    }
}

/** Get the scene of an entity: itself or its nearest ancestor with Scene.
 *
 * @param world The world
 * @param e The entity
 * @return The scene, 0 for the default scene
 */
static ecs_entity_t box_portal_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_get_target_for(world, e, EcsChildOf, Scene);
}

/** Get the world transform that places an entity: its own, else that of its nearest ancestor
 * with one, else the identity. The entity whose transform is used is remembered, so the index
 * is built again when it moves.
 *
 * @param world The world
 * @param index The index
 * @param e The entity to start at
 * @param out Receives the transform
 */
static void box_portal_frame(
    const ecs_world_t *world,
    box_portal_index_t *index,
    ecs_entity_t e,
    mat4 out)
{
    glm_mat4_identity(out);

    while (e) {
        WorldTransform buf;
        const WorldTransform *transform = box_transform_world(world, e, &buf);

        if (transform) {
            glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), out);
            ecs_map_ensure(&index->frames, e);
            break;
        }

        e = ecs_get_parent(world, e);
    }
}

/** Order floats ascending.
 *
 * @param a The first float
 * @param b The second float
 * @return Negative when a is smaller, positive when b is smaller
 */
static int box_portal_float_compare(
    const void *a,
    const void *b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;

    return (fa > fb) - (fa < fb);
}

/** Append one box of an area.
 *
 * @param index The index
 * @param area Index of the area
 * @param exact Whether the boxes of the area cover it and nothing else
 * @param x0 Smallest x
 * @param z0 Smallest z
 * @param x1 Largest x
 * @param z1 Largest z
 */
static void box_portal_index_box(
    box_portal_index_t *index,
    int32_t area,
    bool exact,
    float x0,
    float z0,
    float x1,
    float z1)
{
    const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    box_portal_box_t *b = ecs_vec_append_t(NULL, &index->boxes, box_portal_box_t);

    b->min[0] = x0;
    b->min[1] = a->y;
    b->min[2] = z0;
    b->max[0] = x1;
    b->max[1] = a->top;
    b->max[2] = z1;
    b->area = area;
    b->exact = exact;
}

/** Cover an area whose outline has only edges along x and z with boxes: between every two
 * consecutive z of its corners, one box per stretch of x the outline holds there.
 *
 * @param index The index
 * @param area Index of the area
 * @return Whether the boxes were added, false when a slab has too many edges
 */
static bool box_portal_index_slabs(
    box_portal_index_t *index,
    int32_t area)
{
    const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    const vec2 *points = ecs_vec_get_t(&index->points, vec2, a->point_first);
    int32_t count = a->point_count, first = ecs_vec_count(&index->boxes);
    float *levels = ecs_os_malloc_n(float, count);
    bool result = true;
    int32_t i, k;

    for (i = 0; i < count; i ++) {
        levels[i] = points[i][1];
    }

    qsort(levels, (size_t)count, sizeof(float), box_portal_float_compare);

    for (k = 0; k + 1 < count && result; k ++) {
        float z0 = levels[k], z1 = levels[k + 1];
        float xs[BOX_PORTAL_SLAB_EDGES_MAX];
        int32_t found = 0;

        if (z1 - z0 < BOX_PORTAL_OUTLINE_EPSILON) {
            continue;
        }

        float mid = (z0 + z1) * 0.5f;
        for (i = 0; i < count && result; i ++) {
            const float *p = points[i];
            const float *q = points[(i + 1) % count];
            bool crosses = (p[1] < mid) != (q[1] < mid);

            if (crosses && found == BOX_PORTAL_SLAB_EDGES_MAX) {
                result = false;
            } else if (crosses) {
                xs[found ++] = p[0];
            }
        }

        qsort(xs, (size_t)found, sizeof(float), box_portal_float_compare);

        for (i = 0; i + 1 < found && result; i += 2) {
            if (xs[i + 1] - xs[i] >= BOX_PORTAL_OUTLINE_EPSILON) {
                box_portal_index_box(index, area, true, xs[i], z0, xs[i + 1], z1);
            }
        }
    }

    if (!result) {
        ecs_vec_set_count_t(NULL, &index->boxes, box_portal_box_t, first);
    }

    ecs_os_free(levels);

    return result;
}

/** Add the boxes that cover an area: exact ones for an outline with only edges along x and z,
 * else the one box around it, which also holds what is next to the area.
 *
 * @param index The index
 * @param area Index of the area
 */
static void box_portal_index_boxes(
    box_portal_index_t *index,
    int32_t area)
{
    box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    const vec2 *points = ecs_vec_get_t(&index->points, vec2, a->point_first);
    bool straight = a->point_count >= 4;
    int32_t i;

    for (i = 0; i < a->point_count && straight; i ++) {
        const float *p = points[i];
        const float *q = points[(i + 1) % a->point_count];

        straight = fabsf(p[0] - q[0]) < BOX_PORTAL_OUTLINE_EPSILON ||
            fabsf(p[1] - q[1]) < BOX_PORTAL_OUTLINE_EPSILON;
    }

    a->box_first = ecs_vec_count(&index->boxes);
    a->exact = straight && box_portal_index_slabs(index, area);

    if (!a->exact) {
        box_portal_index_box(index, area, false, a->min[0], a->min[1], a->max[0], a->max[1]);
    }

    a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    a->box_count = ecs_vec_count(&index->boxes) - a->box_first;
}

/** Add the areas of the world, after the outdoors.
 *
 * @param world The world
 * @param index The index
 */
static void box_portal_index_areas(
    ecs_world_t *world,
    box_portal_index_t *index)
{
    box_portal_area_t *outdoors = ecs_vec_append_t(NULL, &index->areas, box_portal_area_t);
    ecs_os_memset_t(outdoors, 0, box_portal_area_t);
    outdoors->mover = -1;

    ecs_iter_t it = ecs_query_iter(world, index->area_query);
    while (ecs_query_next(&it)) {
        const VisArea *areas = ecs_field(&it, VisArea, 0);
        int32_t i, k;

        for (i = 0; i < it.count; i ++) {
            const VisArea *src = &areas[i];
            int32_t count = ecs_vec_count(&src->shape);
            const vec2 *shape = ecs_vec_first_t(&src->shape, vec2);
            mat4 frame;

            if (count < 3 || src->height <= 0) {
                continue;
            }

            box_portal_frame(world, index, ecs_get_parent(world, it.entities[i]), frame);

            int32_t area = ecs_vec_count(&index->areas);
            box_portal_area_t *a = ecs_vec_append_t(NULL, &index->areas, box_portal_area_t);
            ecs_os_memset_t(a, 0, box_portal_area_t);
            a->entity = it.entities[i];
            a->scene = box_portal_scene_of(world, it.entities[i]);
            a->mover = box_portal_mover_slot(world, index, it.entities[i]);
            a->point_first = ecs_vec_count(&index->points);
            a->point_count = count;
            a->min[0] = a->min[1] = FLT_MAX;
            a->max[0] = a->max[1] = -FLT_MAX;

            for (k = 0; k < count; k ++) {
                vec3 local = { shape[k][0], src->y, shape[k][1] };
                vec3 at;
                glm_mat4_mulv3(frame, local, 1.0f, at);

                float *p = *ecs_vec_append_t(NULL, &index->points, vec2);
                p[0] = at[0];
                p[1] = at[2];
                a->y = at[1];
                a->min[0] = glm_min(a->min[0], at[0]);
                a->min[1] = glm_min(a->min[1], at[2]);
                a->max[0] = glm_max(a->max[0], at[0]);
                a->max[1] = glm_max(a->max[1], at[2]);
            }

            a->top = a->y + src->height * glm_vec3_norm(frame[1]);
            box_portal_index_boxes(index, area);
        }
    }
}

/** Add the portals of the world and probe the areas on their two sides.
 *
 * @param world The world
 * @param index The index
 */
static void box_portal_index_portals(
    ecs_world_t *world,
    box_portal_index_t *index)
{
    static const float corners[4][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
    ecs_iter_t it = ecs_query_iter(world, index->portal_query);

    while (ecs_query_next(&it)) {
        const VisPortal *portals = ecs_field(&it, VisPortal, 0);
        int32_t i, k;

        for (i = 0; i < it.count; i ++) {
            const VisPortal *src = &portals[i];
            box_portal_portal_t p;
            vec3 probe, x_axis = {1, 0, 0}, y_axis = {0, 1, 0};
            mat4 frame;

            ecs_os_memset_t(&p, 0, box_portal_portal_t);
            box_portal_frame(world, index, it.entities[i], frame);
            glm_mat4_mulv3(frame, ECS_CONST_CAST(float*, src->normal), 0.0f, p.normal);

            if (glm_vec3_norm(p.normal) < 1e-6f || src->size[0] <= 0 || src->size[1] <= 0) {
                continue;
            }

            glm_vec3_normalize(p.normal);
            if (fabsf(p.normal[1]) > 0.9f) {
                glm_mat4_mulv3(frame, x_axis, 0.0f, p.right);
                glm_vec3_normalize(p.right);
                glm_vec3_cross(p.normal, p.right, p.up);
                glm_vec3_normalize(p.up);
            } else {
                glm_vec3_cross(y_axis, p.normal, p.right);
                glm_vec3_normalize(p.right);
                glm_vec3_copy(y_axis, p.up);
            }

            p.entity = it.entities[i];
            p.scene = box_portal_scene_of(world, it.entities[i]);
            p.mover = box_portal_mover_slot(world, index, it.entities[i]);
            glm_vec3_copy(frame[3], p.center);
            p.d = -glm_vec3_dot(p.normal, p.center);
            p.width = src->size[0];
            p.height = src->size[1];
            p.open = src->open;
            p.slide = src->slide;
            p.source = *src;
            p.pass_through = src->pass_through;
            p.double_side = src->double_side;
            p.sky_only = src->sky_only;
            glm_vec3_fill(p.min, FLT_MAX);
            glm_vec3_fill(p.max, -FLT_MAX);

            for (k = 0; k < 4; k ++) {
                glm_vec3_copy(p.center, p.verts[k]);
                glm_vec3_muladds(p.right, corners[k][0] * p.width * 0.5f, p.verts[k]);
                glm_vec3_muladds(p.up, corners[k][1] * p.height * 0.5f, p.verts[k]);
                glm_vec3_minv(p.min, p.verts[k], p.min);
                glm_vec3_maxv(p.max, p.verts[k], p.max);
            }

            glm_vec3_copy(p.center, probe);
            glm_vec3_muladds(p.normal, -BOX_PORTAL_PROBE, probe);
            p.front = box_portal_index_area_at(index, p.scene, probe);
            glm_vec3_copy(p.center, probe);
            glm_vec3_muladds(p.normal, BOX_PORTAL_PROBE, probe);
            p.back = box_portal_index_area_at(index, p.scene, probe);

            if (p.front == p.back) {
                char *path = ecs_get_path(world, p.entity);
                ecs_warn("portal '%s' has the same area on both sides, skipped", path);
                ecs_os_free(path);
                continue;
            }

            *ecs_vec_append_t(NULL, &index->portals, box_portal_portal_t) = p;
        }
    }
}

/** List the portals of every area: each portal is listed by the two areas on its sides.
 *
 * @param index The index
 */
static void box_portal_index_links(
    box_portal_index_t *index)
{
    box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    int32_t area_count = ecs_vec_count(&index->areas);
    int32_t portal_count = ecs_vec_count(&index->portals);
    int32_t i, first = 0;

    for (i = 0; i < portal_count; i ++) {
        areas[portals[i].front].link_count ++;
        areas[portals[i].back].link_count ++;
    }

    for (i = 0; i < area_count; i ++) {
        areas[i].link_first = first;
        first += areas[i].link_count;
        areas[i].link_count = 0;
    }

    ecs_vec_set_count_t(NULL, &index->links, int32_t, first);
    int32_t *links = ecs_vec_first_t(&index->links, int32_t);

    for (i = 0; i < portal_count; i ++) {
        box_portal_area_t *front = &areas[portals[i].front];
        box_portal_area_t *back = &areas[portals[i].back];

        links[front->link_first + front->link_count ++] = i;
        links[back->link_first + back->link_count ++] = i;
    }
}

/** Add the occluders of the world.
 *
 * @param world The world
 * @param index The index
 */
static void box_portal_index_occluders(
    ecs_world_t *world,
    box_portal_index_t *index)
{
    ecs_iter_t it = ecs_query_iter(world, index->occluder_query);

    while (ecs_query_next(&it)) {
        const VisOccluder *occluders = ecs_field(&it, VisOccluder, 0);
        int32_t i;

        for (i = 0; i < it.count; i ++) {
            const VisOccluder *src = &occluders[i];
            box_portal_occluder_t o;
            mat4 frame;

            ecs_os_memset_t(&o, 0, box_portal_occluder_t);
            box_portal_frame(world, index, it.entities[i], frame);
            glm_mat4_mulv3(frame, ECS_CONST_CAST(float*, src->normal), 0.0f, o.normal);

            if (glm_vec3_norm(o.normal) < 1e-6f || src->size[0] <= 0 || src->size[1] <= 0) {
                continue;
            }

            glm_vec3_normalize(o.normal);
            o.entity = it.entities[i];
            o.scene = box_portal_scene_of(world, it.entities[i]);
            o.mover = box_portal_mover_slot(world, index, it.entities[i]);
            glm_vec3_copy(frame[3], o.center);
            glm_vec2_copy(ECS_CONST_CAST(float*, src->size), o.size);
            o.auto_orient_y = src->auto_orient_y;
            o.area = box_portal_index_area_at(index, o.scene, o.center);
            *ecs_vec_append_t(NULL, &index->occluders, box_portal_occluder_t) = o;
        }
    }
}

/** Build the index from the components of the world.
 *
 * @param world The world
 * @param index The index
 */
static void box_portal_index_build(
    ecs_world_t *world,
    box_portal_index_t *index)
{
    ecs_vec_clear(&index->areas);
    ecs_vec_clear(&index->points);
    ecs_vec_clear(&index->boxes);
    ecs_vec_clear(&index->portals);
    ecs_vec_clear(&index->links);
    ecs_vec_clear(&index->occluders);
    ecs_vec_clear(&index->movers);
    ecs_map_clear(&index->frames);

    box_portal_index_areas(world, index);
    box_portal_index_portals(world, index);
    box_portal_index_links(index);
    box_portal_index_occluders(world, index);
    box_portal_movers_rest(index);

    index->dirty = false;
    index->builds ++;
}

const box_portal_index_t* box_portal_index(
    ecs_world_t *world)
{
    const PortalIndex *singleton = ecs_id(PortalIndex)
        ? ecs_singleton_get(world, PortalIndex)
        : NULL;
    box_portal_index_t *result = singleton ? singleton->index : NULL;

    if (result && result->dirty) {
        box_portal_index_build(world, result);
    }

    if (result && ecs_vec_count(&result->movers)) {
        box_portal_movers_place(world, result);
    }

    return result;
}

bool box_portal_index_has(
    const box_portal_index_t *index,
    ecs_entity_t scene)
{
    const box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    int32_t i, count = ecs_vec_count(&index->areas);
    bool result = false;

    for (i = 1; i < count && !result; i ++) {
        result = areas[i].scene == scene;
    }

    return result;
}

int32_t box_portal_index_area_at(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const float *p)
{
    const box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    int32_t i, k, count = ecs_vec_count(&index->areas), result = 0;

    for (i = 1; i < count && !result; i ++) {
        const box_portal_area_t *a = &areas[i];
        bool inside = false;
        vec3 at;

        if (box_portal_mover_to_rest(index, i, p, at)) {
            a = ecs_vec_get_t(&index->rest_areas, box_portal_area_t, i);
        }

        if (a->scene != scene || at[1] < a->y || at[1] >= a->top || at[0] < a->min[0] ||
            at[0] > a->max[0] || at[2] < a->min[1] || at[2] > a->max[1])
        {
            continue;
        }

        const vec2 *points = ecs_vec_get_t(&index->points, vec2, a->point_first);
        for (k = 0; k < a->point_count; k ++) {
            const float *u = points[k];
            const float *v = points[(k + 1) % a->point_count];

            if ((u[1] > at[2]) != (v[1] > at[2]) &&
                at[0] < (v[0] - u[0]) * (at[2] - u[1]) / (v[1] - u[1]) + u[0])
            {
                inside = !inside;
            }
        }

        if (inside) {
            result = i;
        }
    }

    return result;
}

int32_t box_portal_index_portal(
    const box_portal_index_t *index,
    ecs_entity_t portal)
{
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    int32_t i, count = ecs_vec_count(&index->portals), result = -1;

    for (i = 0; i < count && result < 0; i ++) {
        if (portals[i].entity == portal) {
            result = i;
        }
    }

    return result;
}

ecs_entity_t box_portal_area_at(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float *p)
{
    const box_portal_index_t *index = box_portal_index(world);
    ecs_entity_t result = 0;

    if (index) {
        int32_t area = box_portal_index_area_at(index, scene, p);
        result = ecs_vec_get_t(&index->areas, box_portal_area_t, area)->entity;
    }

    return result;
}

void box_portal_members(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float *min,
    const float *max,
    ecs_vec_t *areas,
    bool *outdoors)
{
    const box_portal_index_t *index = box_portal_index(world);
    int32_t i, k, count = index ? ecs_vec_count(&index->boxes) : 0;
    vec3 center, extent;
    vec4 rows[3];
    float covered = 0;

    for (k = 0; k < 3; k ++) {
        center[k] = (min[k] + max[k]) * 0.5f;
        extent[k] = (max[k] - min[k]) * 0.5f;
    }

    for (i = 0; i < count; i ++) {
        const box_portal_box_t *b = ecs_vec_get_t(&index->boxes, box_portal_box_t, i);
        const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, b->area);
        float share = -1;

        if (a->scene != scene) {
            continue;
        }

        if (a->mover >= 0) {
            b = ecs_vec_get_t(&index->rest_boxes, box_portal_box_t, i);
            box_portal_mover_rows(index, a->mover, rows);
        }

        share = box_portal_box_share(a->mover >= 0 ? rows : NULL, center, extent, b->min,
            b->max);
        if (share < 0) {
            continue;
        }

        covered += b->exact ? share : 0.0f;

        const ecs_entity_t *listed = ecs_vec_first_t(areas, ecs_entity_t);
        bool known = false;
        for (k = 0; k < ecs_vec_count(areas) && !known; k ++) {
            known = listed[k] == a->entity;
        }

        if (!known) {
            *ecs_vec_append_t(NULL, areas, ecs_entity_t) = a->entity;
        }
    }

    *outdoors = covered < BOX_PORTAL_MEMBER_COVER;
}

/** Mark the index changed when an area, a portal or an occluder is set or removed.
 *
 * @param it The observer iterator
 */
static void BoxPortalIndexChanged(
    ecs_iter_t *it)
{
    box_portal_index_ref_t *ref = it->ctx;
    ref->index->dirty = true;
}

/** Check whether two values of a VisPortal place the same opening: whether they differ at most
 * in their open state, which the index updates in place.
 *
 * @param a A value
 * @param b The other value
 * @return Whether they place the same opening
 */
static bool box_portal_same_opening(
    const VisPortal *a,
    const VisPortal *b)
{
    return a->kind == b->kind &&
        a->size[0] == b->size[0] &&
        a->size[1] == b->size[1] &&
        a->normal[0] == b->normal[0] &&
        a->normal[1] == b->normal[1] &&
        a->normal[2] == b->normal[2] &&
        a->pass_through == b->pass_through &&
        a->double_side == b->double_side &&
        a->sky_only == b->sky_only;
}

/** Update the open state of the portals of the index when a VisPortal is set, and mark the
 * index changed when one was removed or now places a different opening.
 *
 * @param it The observer iterator
 */
static void BoxPortalIndexPortalChanged(
    ecs_iter_t *it)
{
    box_portal_index_ref_t *ref = it->ctx;
    box_portal_index_t *index = ref->index;
    const VisPortal *portals = ecs_field(it, VisPortal, 0);
    int32_t i;

    for (i = 0; i < it->count && !index->dirty; i ++) {
        int32_t at = it->event == EcsOnSet
            ? box_portal_index_portal(index, it->entities[i])
            : -1;
        box_portal_portal_t *p = at >= 0
            ? ecs_vec_get_t(&index->portals, box_portal_portal_t, at)
            : NULL;

        if (p && box_portal_same_opening(&p->source, &portals[i])) {
            p->open = portals[i].open;
            p->slide = portals[i].slide;
            p->source = portals[i];
        } else {
            index->dirty = true;
        }
    }
}

/** Mark the index changed when an entity whose world transform placed part of it moved, or a
 * portal or occluder got its transform.
 *
 * @param world The world
 * @param entities The entities whose static transform changed
 * @param count Number of entities
 * @param ctx The box_portal_index_ref_t
 */
static void box_portal_index_moved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    box_portal_index_ref_t *ref = ctx;
    box_portal_index_t *index = ref->index;
    int32_t i;

    for (i = 0; i < count && !index->dirty; i ++) {
        ecs_entity_t e = entities[i];

        if (ecs_map_get(&index->frames, e)) {
            index->dirty = true;
        } else if (ecs_is_alive(world, e) &&
            (ecs_has(world, e, VisPortal) || ecs_has(world, e, VisOccluder)))
        {
            index->dirty = true;
        }
    }
}

void box_portal_index_import(
    ecs_world_t *world)
{
    box_portal_index_t *index = ecs_os_calloc_t(box_portal_index_t);
    box_portal_index_ref_t *ref = ecs_os_calloc_t(box_portal_index_ref_t);
    const char *names[3] = { "AreaChanged", "PortalChanged", "OccluderChanged" };
    ecs_id_t ids[3];
    int32_t i;

    ecs_vec_init_t(NULL, &index->areas, box_portal_area_t, 0);
    ecs_vec_init_t(NULL, &index->points, vec2, 0);
    ecs_vec_init_t(NULL, &index->boxes, box_portal_box_t, 0);
    ecs_vec_init_t(NULL, &index->portals, box_portal_portal_t, 0);
    ecs_vec_init_t(NULL, &index->links, int32_t, 0);
    ecs_vec_init_t(NULL, &index->occluders, box_portal_occluder_t, 0);
    ecs_vec_init_t(NULL, &index->movers, box_portal_mover_t, 0);
    ecs_vec_init_t(NULL, &index->rest_areas, box_portal_area_t, 0);
    ecs_vec_init_t(NULL, &index->rest_boxes, box_portal_box_t, 0);
    ecs_vec_init_t(NULL, &index->rest_portals, box_portal_portal_t, 0);
    ecs_vec_init_t(NULL, &index->rest_occluders, box_portal_occluder_t, 0);
    ecs_map_init(&index->frames, NULL);
    ecs_map_init(&index->frozen, NULL);
    index->dirty = true;
    ref->index = index;
    ref->refs = 4;
    ids[0] = ecs_id(VisArea);
    ids[1] = ecs_id(VisPortal);
    ids[2] = ecs_id(VisOccluder);

    ECS_COMPONENT_DEFINE(world, PortalIndex);
    ecs_set_hooks(world, PortalIndex, { .ctor = flecs_default_ctor });
    ecs_singleton_set(world, PortalIndex, { .index = index });

    index->area_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Areas" }),
        .terms = {{ .id = ecs_id(VisArea), .inout = EcsIn }}
    });
    index->portal_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Portals" }),
        .terms = {{ .id = ecs_id(VisPortal), .inout = EcsIn }}
    });
    index->occluder_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Occluders" }),
        .terms = {{ .id = ecs_id(VisOccluder), .inout = EcsIn }}
    });

    for (i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .entity = ecs_entity(world, { .name = names[i] }),
            .query.terms = {{ .id = ids[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = ids[i] == ecs_id(VisPortal)
                ? BoxPortalIndexPortalChanged
                : BoxPortalIndexChanged,
            .ctx = ref,
            .ctx_free = box_portal_index_release
        });
    }

    box_transform_on_moved(world, box_portal_index_moved, ref, box_portal_index_release);
}
