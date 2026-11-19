/**
 * @file portal/block.c
 * @brief Packs what a portal traversal saw into the block the cull pass tests instances against,
 * and the same test on the CPU.
 *
 * A block is a run of vec4, indexed from its first: a header, two vec4 per area box, one per
 * area, three rows of the world to rest matrix per mover, then BOX_PORTAL_BLOCK_PLANES planes
 * per entry. The boxes of a mover's areas are at its rest, tested in the frame of its rows.
 */

#include "portal.h"
#include <math.h>

/** Entries of the outdoors in a block at most; seen through more, the outdoors is seen whole */
#define BOX_PORTAL_BLOCK_OUTDOOR_ENTRIES (16)

/** Number of entries that stands for an area seen with the frustum of the camera */
#define BOX_PORTAL_BLOCK_WHOLE (-1.0f)

/** The entries of one area, or of the outdoors, of a traversal */
typedef struct box_portal_seen_t {
    int32_t count;          /**< Entries, at most the number kept */
    bool whole;             /**< It is seen with the frustum of the camera */
    int32_t entries[BOX_PORTAL_BLOCK_OUTDOOR_ENTRIES]; /**< Indices of the entries */
} box_portal_seen_t;

/** Collect the entries of a traversal that see one area.
 *
 * @param vis The traversal
 * @param area Index of the area, 0 for the outdoors
 * @param max Entries to keep at most; an area with more is seen whole
 * @param out Receives the entries
 */
static void box_portal_block_collect(
    const box_portal_vis_t *vis,
    int32_t area,
    int32_t max,
    box_portal_seen_t *out)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    int32_t i, count = ecs_vec_count(&vis->entries);

    out->count = 0;
    out->whole = false;

    for (i = 0; i < count && !out->whole; i ++) {
        const box_portal_entry_t *e = &entries[i];

        if (e->area != area || e->sky_only) {
            continue;
        }

        if (e->whole || out->count == max) {
            out->whole = true;
        } else {
            out->entries[out->count ++] = i;
        }
    }
}

/** Append the planes of the entries of one area and write where they are.
 *
 * @param vis The traversal
 * @param seen The entries of the area
 * @param out The block, appended to
 * @param first Index of the first vec4 of the block in out
 * @param slot Receives the first vec4 of the entries in x and their number in y
 */
static void box_portal_block_entries(
    const box_portal_vis_t *vis,
    const box_portal_seen_t *seen,
    ecs_vec_t *out,
    int32_t first,
    float *slot)
{
    int32_t i, k;

    slot[0] = (float)(ecs_vec_count(out) - first);
    slot[1] = seen->whole ? BOX_PORTAL_BLOCK_WHOLE : (float)seen->count;

    for (i = 0; i < seen->count && !seen->whole; i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(
            &vis->entries, box_portal_entry_t, seen->entries[i]);
        vec4 *planes = ecs_vec_grow_t(NULL, out, vec4, BOX_PORTAL_BLOCK_PLANES);

        for (k = 0; k < BOX_PORTAL_BLOCK_PLANES; k ++) {
            if (k <= e->hull_count) {
                glm_vec4_copy(ECS_CONST_CAST(float*, e->planes[k]), planes[k]);
            } else {
                glm_vec4_copy((vec4){0, 0, 0, 1}, planes[k]);
            }
        }
    }
}

bool box_portal_block_pack(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    ecs_vec_t *out,
    int32_t max)
{
    const box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    const box_portal_box_t *boxes = ecs_vec_first_t(&index->boxes, box_portal_box_t);
    const box_portal_box_t *rest = ecs_vec_first_t(&index->rest_boxes, box_portal_box_t);
    int32_t area_count = ecs_vec_count(&index->areas), box_count = ecs_vec_count(&index->boxes);
    int32_t movers = ecs_vec_count(&index->movers);
    int32_t first = ecs_vec_count(out), local_boxes = 0, local_areas = 0, i;
    int32_t *local = ecs_os_calloc_n(int32_t, area_count);
    int32_t at = first + 1, table = first + 1;
    vec4 header = {0, 0, 0, 0};
    box_portal_seen_t seen;
    bool result = !vis->truncated;

    for (i = 1; i < area_count; i ++) {
        local[i] = -1;
        if (areas[i].scene == vis->scene) {
            local[i] = local_areas ++;
            local_boxes += areas[i].box_count;
        }
    }

    result = result && local_areas > 0 &&
        1 + 2 * local_boxes + local_areas + 3 * movers <= max;
    if (!result) {
        goto done;
    }

    ecs_vec_grow_t(NULL, out, vec4, 1 + 2 * local_boxes + local_areas + 3 * movers);
    table += 2 * local_boxes;

    for (i = 0; i < box_count; i ++) {
        int32_t mover = areas[boxes[i].area].mover;
        const box_portal_box_t *b = mover >= 0 ? &rest[i] : &boxes[i];

        if (local[b->area] < 0) {
            continue;
        }

        float *lo = *ecs_vec_get_t(out, vec4, at ++);
        float *hi = *ecs_vec_get_t(out, vec4, at ++);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->min), lo);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->max), hi);
        lo[3] = (float)local[b->area];
        hi[3] = (b->exact ? 1.0f : 0.0f) + 2.0f * (float)(mover + 1);
    }

    for (i = 0; i < movers; i ++) {
        box_portal_mover_rows(index, i, ecs_vec_get_t(out, vec4, table + local_areas + 3 * i));
    }

    for (i = 1; i < area_count; i ++) {
        vec4 slot = {0, 0, 0, 0};
        if (local[i] < 0) {
            continue;
        }

        box_portal_block_collect(vis, i, BOX_PORTAL_BLOCK_AREA_ENTRIES, &seen);
        box_portal_block_entries(vis, &seen, out, first, slot);
        glm_vec4_copy(slot, *ecs_vec_get_t(out, vec4, table + local[i]));
    }

    header[0] = (float)local_boxes;
    header[1] = (float)local_areas;
    box_portal_block_collect(vis, 0, BOX_PORTAL_BLOCK_OUTDOOR_ENTRIES, &seen);
    box_portal_block_entries(vis, &seen, out, first, &header[2]);
    glm_vec4_copy(header, *ecs_vec_get_t(out, vec4, first));

    result = ecs_vec_count(out) - first <= max;
    if (!result) {
        ecs_vec_set_count_t(NULL, out, vec4, first);
    }
done:
    ecs_os_free(local);

    return result;
}

/** Check whether one of a run of entries of a block sees a box.
 *
 * @param block The block
 * @param slot The first vec4 of the entries in x and their number in y
 * @param center Center of the box
 * @param extent Half the size of the box
 * @return Whether the run is seen whole or the box is inside every plane of one of its entries
 */
static bool box_portal_block_entries_see(
    const vec4 *block,
    const float *slot,
    const float *center,
    const float *extent)
{
    int32_t first = (int32_t)slot[0], count = (int32_t)slot[1], i, k;
    bool result = slot[1] < 0;

    for (i = 0; i < count && !result; i ++) {
        const vec4 *planes = &block[first + i * BOX_PORTAL_BLOCK_PLANES];
        result = true;

        for (k = 0; k < BOX_PORTAL_BLOCK_PLANES && result; k ++) {
            const float *p = planes[k];
            float reach = fabsf(p[0]) * extent[0] + fabsf(p[1]) * extent[1] +
                fabsf(p[2]) * extent[2];

            result = p[0] * center[0] + p[1] * center[1] + p[2] * center[2] + p[3] + reach >= 0;
        }
    }

    return result;
}

bool box_portal_block_sees(
    const vec4 *block,
    const float *min,
    const float *max)
{
    int32_t box_count = (int32_t)block[0][0], table = 1 + 2 * box_count, i, k;
    int32_t rows = table + (int32_t)block[0][1];
    vec3 center, extent;
    float covered = 0;
    bool result = false;

    for (k = 0; k < 3; k ++) {
        center[k] = (min[k] + max[k]) * 0.5f;
        extent[k] = (max[k] - min[k]) * 0.5f;
    }

    for (i = 0; i < box_count && !result; i ++) {
        const float *bmin = block[1 + 2 * i];
        const float *bmax = block[2 + 2 * i];
        int32_t code = (int32_t)bmax[3], mover = code / 2;
        const vec4 *frame = mover > 0 ? &block[rows + 3 * (mover - 1)] : NULL;
        float share = box_portal_box_share(frame, center, extent, bmin, bmax);

        if (share >= 0) {
            covered += share * (float)(code % 2);
            result = box_portal_block_entries_see(
                block, block[table + (int32_t)bmin[3]], center, extent);
        }
    }

    if (!result && covered < BOX_PORTAL_MEMBER_COVER) {
        result = box_portal_block_entries_see(block, &block[0][2], center, extent);
    }

    return result;
}

float box_portal_box_share(
    const vec4 *rows,
    const float *center,
    const float *extent,
    const float *min,
    const float *max)
{
    vec3 c, e;
    float shared = 1, volume = 1, result = -1;
    bool reaches = true;
    int32_t k;

    if (rows) {
        box_portal_rows_box(rows, center, extent, c, e);
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, center), c);
        glm_vec3_copy(ECS_CONST_CAST(float*, extent), e);
    }

    for (k = 0; k < 3; k ++) {
        float half = glm_max(e[k], BOX_PORTAL_MEMBER_THIN * 0.5f);
        float depth = glm_min(c[k] + half, max[k]) - glm_max(c[k] - half, min[k]);

        reaches = reaches && depth > BOX_PORTAL_MEMBER_DEPTH;
        shared *= depth;
        volume *= 2 * half;
    }

    if (reaches) {
        result = shared / volume;
    }

    return result;
}
