/**
 * @file render/store.c
 * @brief Instance pages shared by every batch and the slot allocator of their runs.
 *
 * A page is one GPU buffer per stream of its layout, holding at most the slots of the page. Its
 * buffers start small and double up to that size, created again when its runs reach past them;
 * the backend copies the old buffers into the new ones, so the runs keep their instances.
 *
 * Free slots below the end of the highest run are kept as holes, so a freed run merges with the
 * free slots next to it; the slots past that end are free as well.
 */

#include "draw.h"

void box_draws_store_init(
    box_draw_store_t *s,
    const box_draw_layout_t *layout,
    uint32_t base,
    uint32_t slots)
{
    ecs_os_memset_t(s, 0, box_draw_store_t);
    s->layout = *layout;
    s->base = base;
    s->slots = slots;
    ecs_vec_init_t(NULL, &s->pages, box_draw_page_t, 0);
}

void box_draws_store_fini(
    box_draw_store_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        ecs_vec_fini_t(NULL, &ecs_vec_get_t(&s->pages, box_draw_page_t, i)->holes,
            box_draw_hole_t);
    }

    ecs_vec_fini_t(NULL, &s->pages, box_draw_page_t);
    s->alive = 0;
}

void box_draws_store_reset(
    box_draw_store_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        box_draw_page_t *pg = ecs_vec_get_t(&s->pages, box_draw_page_t, i);
        ecs_vec_clear(&pg->holes);
        pg->alive = false;
        pg->capacity = 0;
        pg->top = 0;
        pg->runs = 0;
    }

    s->alive = 0;
}

box_draw_page_t* box_draws_store_page(
    box_draw_store_t *s,
    int32_t page)
{
    return ecs_vec_get_t(&s->pages, box_draw_page_t, page);
}

void box_draws_store_memory(
    const box_draw_store_t *s,
    int64_t *slots,
    int64_t bytes[BOX_DRAWS_STREAMS])
{
    int32_t i, j;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        const box_draw_page_t *pg = ecs_vec_get_t(&s->pages, box_draw_page_t, i);
        if (!pg->alive || !pg->capacity) {
            continue;
        }

        *slots += pg->capacity;
        for (j = 0; j < s->layout.streams; j ++) {
            bytes[j] += (int64_t)pg->capacity * s->layout.stride[j];
        }
    }
}

uint32_t box_draws_store_handle(
    const box_draw_store_t *s,
    int32_t page,
    int32_t stream)
{
    return s->base + (uint32_t)page * (uint32_t)s->layout.streams + (uint32_t)stream;
}

/** Take a run out of the free slots of a page, the lowest that fit.
 *
 * @param pg The page
 * @param count Slots of the run
 * @param first Receives the first slot of the run
 * @return Whether the page had room
 */
static bool box_draws_page_take(
    box_draw_page_t *pg,
    uint32_t count,
    uint32_t *first)
{
    int32_t i, holes = ecs_vec_count(&pg->holes);
    box_draw_hole_t *h = ecs_vec_first_t(&pg->holes, box_draw_hole_t);
    bool result = false;
    for (i = 0; i < holes; i ++) {
        if (h[i].count >= count) {
            *first = h[i].first;
            h[i].first += count;
            h[i].count -= count;
            if (!h[i].count) {
                ecs_vec_remove_ordered_t(&pg->holes, box_draw_hole_t, i);
            }

            result = true;
            goto done;
        }
    }

    if ((uint64_t)pg->top + count <= pg->slots) {
        *first = pg->top;
        pg->top += count;
        result = true;
    }

done:
    if (result) {
        pg->runs ++;
    }

    return result;
}

void box_draws_store_alloc(
    box_draw_store_t *s,
    uint32_t count,
    int32_t *page,
    uint32_t *first)
{
    int32_t i, free = -1, pages = ecs_vec_count(&s->pages);
    box_draw_page_t *pg;
    for (i = 0; i < pages; i ++) {
        pg = ecs_vec_get_t(&s->pages, box_draw_page_t, i);
        if (!pg->alive) {
            free = free < 0 ? i : free;
            continue;
        }

        if (box_draws_page_take(pg, count, first)) {
            *page = i;
            goto done;
        }
    }

    if (free < 0) {
        free = pages;
        pg = ecs_vec_append_t(NULL, &s->pages, box_draw_page_t);
        ecs_os_memset_t(pg, 0, box_draw_page_t);
        ecs_vec_init_t(NULL, &pg->holes, box_draw_hole_t, 0);
    } else {
        pg = ecs_vec_get_t(&s->pages, box_draw_page_t, free);
    }

    pg->alive = true;
    pg->slots = s->slots > count ? s->slots : count;
    pg->capacity = 0;
    pg->top = 0;
    pg->runs = 0;
    ecs_vec_clear(&pg->holes);
    s->alive ++;
    box_draws_page_take(pg, count, first);
    *page = free;
done:
    return;
}

/** Release the buffers of a page in the packet.
 *
 * @param s The store
 * @param p The packet
 * @param page Index of the page
 */
static void box_draws_page_release(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page)
{
    box_draw_page_t *pg = ecs_vec_get_t(&s->pages, box_draw_page_t, page);
    int32_t i;
    for (i = 0; pg->capacity && i < s->layout.streams; i ++) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY,
            box_draws_store_handle(s, page, i), 0);
    }

    ecs_vec_clear(&pg->holes);
    pg->alive = false;
    pg->capacity = 0;
    pg->top = 0;
    pg->serial ++;
    s->alive --;
}

void box_draws_store_free(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page,
    uint32_t first,
    uint32_t count)
{
    box_draw_page_t *pg = ecs_vec_get_t(&s->pages, box_draw_page_t, page);
    int32_t i, holes = ecs_vec_count(&pg->holes);
    box_draw_hole_t *h = ecs_vec_first_t(&pg->holes, box_draw_hole_t);
    pg->runs --;
    if (!pg->runs) {
        ecs_vec_clear(&pg->holes);
        pg->top = 0;
        if (page) {
            box_draws_page_release(s, p, page);
        }

        goto done;
    }

    if (first + count == pg->top) {
        pg->top = first;
        if (holes && h[holes - 1].first + h[holes - 1].count == pg->top) {
            pg->top = h[holes - 1].first;
            ecs_vec_remove_last(&pg->holes);
        }

        goto done;
    }

    i = 0;
    while (i < holes && h[i].first < first) {
        i ++;
    }

    bool prev = i > 0 && h[i - 1].first + h[i - 1].count == first;
    bool next = i < holes && first + count == h[i].first;
    if (prev && next) {
        h[i - 1].count += count + h[i].count;
        ecs_vec_remove_ordered_t(&pg->holes, box_draw_hole_t, i);
    } else if (prev) {
        h[i - 1].count += count;
    } else if (next) {
        h[i].first = first;
        h[i].count += count;
    } else {
        ecs_vec_append_t(NULL, &pg->holes, box_draw_hole_t);
        h = ecs_vec_first_t(&pg->holes, box_draw_hole_t);
        ecs_os_memmove_n(&h[i + 1], &h[i], box_draw_hole_t, holes - i);
        h[i] = (box_draw_hole_t){ .first = first, .count = count };
    }

done:
    return;
}

bool box_draws_store_reserve(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page)
{
    box_draw_page_t *pg = ecs_vec_get_t(&s->pages, box_draw_page_t, page);
    uint32_t capacity = pg->capacity ? pg->capacity : BOX_DRAWS_MIN_CAPACITY;
    bool result = false;
    int32_t i;
    if (!pg->alive || (pg->capacity && pg->top <= pg->capacity)) {
        goto done;
    }

    while (capacity < pg->top) {
        capacity *= 2;
    }

    if (capacity > pg->slots) {
        capacity = pg->slots;
    }

    for (i = 0; i < s->layout.streams; i ++) {
        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, box_draws_store_handle(s, page, i),
            sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)capacity * s->layout.stride[i];
        desc->usage = BOX_RENDER_BUFFER_INSTANCE | BOX_RENDER_BUFFER_STORAGE |
            BOX_RENDER_BUFFER_COPY | (pg->capacity ? BOX_RENDER_BUFFER_KEEP : 0);
    }

    pg->capacity = capacity;
    result = true;
done:
    return result;
}
