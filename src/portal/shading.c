/**
 * @file portal/shading.c
 * @brief Packs the shading block of a view: the boxes of the areas, a record per area, the
 * entries the areas are seen through with their fog chains, and the lights through portals.
 *
 * Header: boxes, records, offset of the records, 1. Box: min and area, max and mover slot + 1,
 * at the rest of the mover, whose three world to rest rows follow the boxes. Record: ambient and
 * sun (ambient below 0 keeps the scene's), fog color and density, its entries and its lights.
 *
 * Entry: planes and links, the planes, then plane and fog of each link, nearest first. Light:
 * index (-1 the sun), openings and size; pass; the openings.
 */

#include "shading.h"

/** Ambient of a record that keeps the ambient of the scene: the outdoors */
#define BOX_PORTAL_SHADE_SCENE_AMBIENT (-1.0f)

/** Write the header, the boxes of the areas and their records.
 *
 * @param world The world
 * @param desc What the view sees
 * @param boxed Per area, whether its boxes go into the block
 * @param out The block, its header at first
 * @param first Index of the first vec4 of the block in out
 * @param max vec4 the block may take at most
 * @return Whether they fit
 */
static bool box_portal_shade_areas(
    const ecs_world_t *world,
    const box_portal_shade_desc_t *desc,
    const bool *boxed,
    ecs_vec_t *out,
    int32_t first,
    int32_t max)
{
    const box_portal_index_t *index = desc->index;
    int32_t area_count = ecs_vec_count(&index->areas), box_count = ecs_vec_count(&index->boxes);
    int32_t movers = ecs_vec_count(&index->movers);
    int32_t i, boxes = 0, at, size, records;
    bool result;

    for (i = 0; i < box_count; i ++) {
        boxes += boxed[ecs_vec_get_t(&index->boxes, box_portal_box_t, i)->area] ? 1 : 0;
    }

    records = 1 + 2 * boxes + 3 * movers;
    size = records + BOX_PORTAL_SHADE_RECORD * area_count;
    result = size <= max;
    if (result) {
        ecs_vec_grow_t(NULL, out, vec4, size);
        ecs_os_memset(ecs_vec_get_t(out, vec4, first), 0, size * ECS_SIZEOF(vec4));
        glm_vec4_copy((vec4){ (float)boxes, (float)area_count, (float)records, 1.0f },
            *ecs_vec_get_t(out, vec4, first));
        at = first + 1;

        for (i = 0; i < box_count; i ++) {
            const box_portal_box_t *b = ecs_vec_get_t(&index->boxes, box_portal_box_t, i);
            int32_t mover = ecs_vec_get_t(&index->areas, box_portal_area_t, b->area)->mover;
            float *lo, *hi;

            if (!boxed[b->area]) {
                continue;
            }

            if (mover >= 0) {
                b = ecs_vec_get_t(&index->rest_boxes, box_portal_box_t, i);
            }

            lo = *ecs_vec_get_t(out, vec4, at ++);
            hi = *ecs_vec_get_t(out, vec4, at ++);
            glm_vec3_copy(ECS_CONST_CAST(float*, b->min), lo);
            glm_vec3_copy(ECS_CONST_CAST(float*, b->max), hi);
            lo[3] = (float)b->area;
            hi[3] = (float)(mover + 1);
        }

        for (i = 0; i < movers; i ++) {
            box_portal_mover_rows(index, i, ecs_vec_get_t(out, vec4, at));
            at += 3;
        }

        for (i = 0; i < area_count; i ++) {
            const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, i);
            const VisArea *area = a->entity && ecs_is_alive(world, a->entity)
                ? ecs_get(world, a->entity, VisArea)
                : NULL;
            float *ambient = *ecs_vec_get_t(out, vec4, at + i * BOX_PORTAL_SHADE_RECORD);
            float *fog = *ecs_vec_get_t(out, vec4, at + i * BOX_PORTAL_SHADE_RECORD + 1);

            glm_vec4_copy((vec4){ BOX_PORTAL_SHADE_SCENE_AMBIENT, 0, 0, 0 }, ambient);
            if (area) {
                glm_vec3_copy(ECS_CONST_CAST(float*, area->ambient), ambient);
                ambient[3] = area->sun;
                glm_vec4_copy(ECS_CONST_CAST(float*, area->fog), fog);
            }
        }
    }

    return result;
}

/** Get the fog of an area.
 *
 * @param world The world
 * @param index The index
 * @param area Index of the area
 * @param out Receives the fog color and density, zero for the outdoors
 */
static void box_portal_shade_fog(
    const ecs_world_t *world,
    const box_portal_index_t *index,
    int32_t area,
    float *out)
{
    const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    const VisArea *v = a->entity && ecs_is_alive(world, a->entity)
        ? ecs_get(world, a->entity, VisArea)
        : NULL;

    glm_vec4_zero(out);
    if (v) {
        glm_vec4_copy(ECS_CONST_CAST(float*, v->fog), out);
    }
}

/** Append an entry: its planes and the chain of portals it is seen through, nearest first,
 * each with the fog of the area in front of it.
 *
 * @param world The world
 * @param desc What the view sees
 * @param entry Index of the entry
 * @param out The block, appended to
 * @param end Index in out the block may not reach
 * @return Whether it fit
 */
static bool box_portal_shade_entry(
    const ecs_world_t *world,
    const box_portal_shade_desc_t *desc,
    int32_t entry,
    ecs_vec_t *out,
    int32_t end)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&desc->vis->entries, box_portal_entry_t);
    const box_portal_entry_t *e = &entries[entry];
    int32_t chain[BOX_PORTAL_DEPTH_MAX + 1], depth = 0, links, planes, i, at, k;
    bool result;

    for (i = entry; entries[i].parent >= 0 && depth <= BOX_PORTAL_DEPTH_MAX;
        i = entries[i].parent)
    {
        chain[depth ++] = i;
    }

    planes = e->whole ? 0 : glm_min(e->hull_count + 1, e->plane_count);
    links = glm_min(depth, BOX_PORTAL_SHADE_LINKS);
    result = ecs_vec_count(out) + 1 + planes + 2 * links <= end;

    if (result) {
        at = ecs_vec_count(out);
        ecs_vec_grow_t(NULL, out, vec4, 1 + planes + 2 * links);
        glm_vec4_copy((vec4){ (float)planes, (float)links, 0, 0 },
            *ecs_vec_get_t(out, vec4, at ++));

        for (k = 0; k < planes; k ++) {
            glm_vec4_copy(ECS_CONST_CAST(float*, e->planes[k]), *ecs_vec_get_t(out, vec4, at ++));
        }

        for (k = 0; k < links; k ++) {
            const box_portal_entry_t *link = &entries[chain[depth - 1 - k]];
            const box_portal_portal_t *p = ecs_vec_get_t(
                &desc->index->portals, box_portal_portal_t, link->via);
            float *plane = *ecs_vec_get_t(out, vec4, at ++);

            glm_vec3_copy(ECS_CONST_CAST(float*, p->normal), plane);
            plane[3] = p->d;
            box_portal_shade_fog(world, desc->index, entries[link->parent].area,
                *ecs_vec_get_t(out, vec4, at ++));
        }
    }

    return result;
}

/** Append the entries of an area and write where they are into its record.
 *
 * @param world The world
 * @param desc What the view sees
 * @param area Index of the area
 * @param record The third vec4 of its record
 * @param out The block, appended to
 * @param first Index of the first vec4 of the block in out
 * @param end Index in out the block may not reach
 */
static void box_portal_shade_entries(
    const ecs_world_t *world,
    const box_portal_shade_desc_t *desc,
    int32_t area,
    int32_t record,
    ecs_vec_t *out,
    int32_t first,
    int32_t end)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&desc->vis->entries, box_portal_entry_t);
    int32_t i, count = ecs_vec_count(&desc->vis->entries), kept = 0, start = ecs_vec_count(out);
    int32_t max = area ? BOX_PORTAL_SHADE_AREA_ENTRIES : BOX_PORTAL_SHADE_OUTDOOR_ENTRIES;
    bool fits = true;

    for (i = 0; i < count && kept < max && fits; i ++) {
        if (entries[i].area == area && !entries[i].sky_only) {
            fits = box_portal_shade_entry(world, desc, i, out, end);
            kept += fits ? 1 : 0;
        }
    }

    float *slot = *ecs_vec_get_t(out, vec4, record);
    slot[0] = (float)(start - first);
    slot[1] = (float)kept;
}

/** Append the lights that shine into an area through its portals and write where they are
 * into its record.
 *
 * @param desc What the view sees
 * @param area Index of the area
 * @param record The third vec4 of its record
 * @param out The block, appended to
 * @param first Index of the first vec4 of the block in out
 * @param end Index in out the block may not reach
 */
static void box_portal_shade_throughs(
    const box_portal_shade_desc_t *desc,
    int32_t area,
    int32_t record,
    ecs_vec_t *out,
    int32_t first,
    int32_t end)
{
    const box_portal_aperture_t *apertures = ecs_vec_first_t(
        desc->apertures, box_portal_aperture_t);
    box_portal_through_t through[BOX_PORTAL_THROUGH_MAX];
    int32_t count = box_portal_through(desc->index, desc->apertures, area, desc->lights,
        desc->light_count, desc->sun, desc->eye, through, BOX_PORTAL_THROUGH_MAX);
    int32_t start = ecs_vec_count(out), room = (end - start) / BOX_PORTAL_SHADE_THROUGH;
    int32_t i, k;

    count = glm_max(glm_min(count, room), 0);
    if (count) {
        ecs_vec_grow_t(NULL, out, vec4, count * BOX_PORTAL_SHADE_THROUGH);
        ecs_os_memset(ecs_vec_get_t(out, vec4, start), 0,
            count * BOX_PORTAL_SHADE_THROUGH * ECS_SIZEOF(vec4));
    }

    for (i = 0; i < count; i ++) {
        const box_portal_through_t *t = &through[i];
        vec4 *rec = ecs_vec_get_t(out, vec4, start + i * BOX_PORTAL_SHADE_THROUGH);
        float size = t->light < 0
            ? BOX_PORTAL_SUN_SOFT * BOX_PORTAL_SUN_FAR
            : BOX_PORTAL_LIGHT_SIZE;

        glm_vec4_copy((vec4){ (float)t->light, (float)t->count, size, 0 }, rec[0]);
        glm_vec3_copy(ECS_CONST_CAST(float*, t->pass), rec[1]);

        for (k = 0; k < t->count; k ++) {
            const box_portal_aperture_t *a = &apertures[t->portals[k]];
            glm_vec3_copy(ECS_CONST_CAST(float*, a->center), rec[2 + 3 * k]);
            glm_vec3_copy(ECS_CONST_CAST(float*, a->right), rec[3 + 3 * k]);
            glm_vec3_copy(ECS_CONST_CAST(float*, a->up), rec[4 + 3 * k]);
        }
    }

    float *slot = *ecs_vec_get_t(out, vec4, record);
    slot[2] = (float)(start - first);
    slot[3] = (float)count;
}

/** Mark the areas whose boxes go into the block and those that get lights through portals:
 * with a traversal the seen areas and, for the boxes, their neighbours; without one every area
 * of the scene, and the nearest of them for the lights.
 *
 * @param desc What the view sees
 * @param seen Whether the traversal can be used
 * @param boxed Receives per area whether its boxes go into the block
 * @param lit Receives per area whether it gets lights through portals
 */
static void box_portal_shade_mark(
    const box_portal_shade_desc_t *desc,
    bool seen,
    bool *boxed,
    bool *lit)
{
    const box_portal_index_t *index = desc->index;
    int32_t i, k, count = ecs_vec_count(&index->areas);
    float *distance = ecs_os_calloc_n(float, count);

    for (i = 1; i < count; i ++) {
        const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, i);
        vec3 center = { (a->min[0] + a->max[0]) * 0.5f, (a->y + a->top) * 0.5f,
            (a->min[1] + a->max[1]) * 0.5f };

        boxed[i] = !seen && a->scene == desc->scene;
        distance[i] = glm_vec3_distance(center, ECS_CONST_CAST(float*, desc->eye));
    }

    lit[0] = true;
    if (seen) {
        const box_portal_entry_t *entries = ecs_vec_first_t(&desc->vis->entries,
            box_portal_entry_t);

        for (i = 0; i < ecs_vec_count(&desc->vis->entries); i ++) {
            const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t,
                entries[i].area);
            const int32_t *links = ecs_vec_first_t(&index->links, int32_t);

            lit[entries[i].area] = true;
            boxed[entries[i].area] = entries[i].area != 0;

            for (k = 0; k < a->link_count; k ++) {
                const box_portal_portal_t *p = ecs_vec_get_t(&index->portals,
                    box_portal_portal_t, links[a->link_first + k]);
                boxed[p->front] = p->front != 0;
                boxed[p->back] = p->back != 0;
            }
        }
    } else {
        for (k = 0; k < BOX_PORTAL_SHADE_NEAR_AREAS; k ++) {
            int32_t best = -1;

            for (i = 1; i < count; i ++) {
                bool candidate = boxed[i] && !lit[i];
                if (candidate && (best < 0 || distance[i] < distance[best])) {
                    best = i;
                }
            }

            if (best >= 0) {
                lit[best] = true;
            }
        }
    }

    ecs_os_free(distance);
}

bool box_portal_shade_pack(
    const ecs_world_t *world,
    const box_portal_shade_desc_t *desc,
    ecs_vec_t *out,
    int32_t max)
{
    const box_portal_index_t *index = desc->index;
    int32_t count = index ? ecs_vec_count(&index->areas) : 0, first = ecs_vec_count(out), i;
    bool *boxed = ecs_os_calloc_n(bool, count);
    bool *lit = ecs_os_calloc_n(bool, count);
    bool seen = desc->vis && !desc->vis->truncated && desc->vis->scene == desc->scene &&
        ecs_vec_count(&desc->vis->entries) > 0;
    bool result = count > 1 && box_portal_index_has(index, desc->scene);

    ecs_assert(!result || ecs_vec_count(desc->apertures) == ecs_vec_count(&index->portals),
        ECS_INVALID_PARAMETER, "the apertures are not those of the index");

    if (result) {
        box_portal_shade_mark(desc, seen, boxed, lit);
        result = box_portal_shade_areas(world, desc, boxed, out, first, max);
    }

    for (i = 0; i < count && result; i ++) {
        const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, i);
        int32_t record = first + (int32_t)(*ecs_vec_get_t(out, vec4, first))[2] +
            i * BOX_PORTAL_SHADE_RECORD + 2;

        if (i && a->scene != desc->scene) {
            continue;
        }

        if (seen) {
            box_portal_shade_entries(world, desc, i, record, out, first, first + max);
        }

        if (lit[i]) {
            box_portal_shade_throughs(desc, i, record, out, first, first + max);
        }
    }

    ecs_os_free(boxed);
    ecs_os_free(lit);

    return result;
}

int32_t box_portal_shade_area(
    const vec4 *block,
    const float *p)
{
    int32_t i, k, boxes = (int32_t)block[0][0], result = 0;
    bool found = false;

    for (i = 0; i < boxes && !found; i ++) {
        const float *lo = block[1 + 2 * i];
        const float *hi = block[2 + 2 * i];
        int32_t mover = (int32_t)hi[3];
        vec3 at, point = {0, 0, 0}, spread;

        glm_vec3_copy(ECS_CONST_CAST(float*, p), at);
        if (mover > 0) {
            box_portal_rows_box(&block[1 + 2 * boxes + 3 * (mover - 1)], p, point, at, spread);
        }

        found = true;
        for (k = 0; k < 3; k ++) {
            found = found && at[k] >= lo[k] && at[k] <= hi[k];
        }

        result = found ? (int32_t)lo[3] : 0;
    }

    return result;
}
