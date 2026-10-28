/**
 * @file render/order.c
 * @brief Draw order of the 2D shapes and glyphs: the order of the hierarchy, not view depth.
 *
 * The entities with 2D slots are sorted by the sibling keys (Position z, then rank) of their
 * ancestors and themselves, floating elements (LayoutFloat) and their descendants after the
 * rest; their slots are drawn per view as runs of consecutive slots.
 */

#include "draw.h"
#include <stdlib.h>

/** Most slots one 2D draw holds, so the ordered cull pass splits long runs over workgroups */
#define BOX_DRAWS_RUN_MAX (512)

void box_draws_order_init(
    box_render_draws_t *d)
{
    ecs_vec_init_t(NULL, &d->order, box_draw_order_t, 0);
    ecs_vec_init_t(NULL, &d->order_keys, uint64_t, 0);
    ecs_vec_init_t(NULL, &d->runs, box_draw_run_t, 0);
    ecs_vec_init_t(NULL, &d->ordered_parents, box_draw_parent_t, 0);
    ecs_vec_init_t(NULL, &d->ordered_children, ecs_entity_t, 0);
    ecs_map_init(&d->ordered, NULL);
    ecs_map_init(&d->paths, NULL);
    ecs_map_init(&d->ranks, NULL);
    ecs_map_init(&d->parents, NULL);
    ecs_map_init(&d->order_z, NULL);
}

void box_draws_order_fini(
    box_render_draws_t *d)
{
    ecs_vec_fini_t(NULL, &d->order, box_draw_order_t);
    ecs_vec_fini_t(NULL, &d->order_keys, uint64_t);
    ecs_vec_fini_t(NULL, &d->runs, box_draw_run_t);
    ecs_vec_fini_t(NULL, &d->ordered_parents, box_draw_parent_t);
    ecs_vec_fini_t(NULL, &d->ordered_children, ecs_entity_t);
    ecs_map_fini(&d->ordered);
    ecs_map_fini(&d->paths);
    ecs_map_fini(&d->ranks);
    ecs_map_fini(&d->parents);
    ecs_map_fini(&d->order_z);
}

void box_draws_order_slot(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    d->runs_dirty = true;
    if (e && !d->order_dirty && !ecs_map_get(&d->ordered, e)) {
        d->order_dirty = true;
    }
}

/** Check whether an OrderedChildren parent still has the children the order was made from.
 *
 * @param world The world
 * @param d The state
 * @param p The parent as the order read it
 * @return Whether the parent is alive with the same children in the same order
 */
static bool box_draws_parent_same(
    const ecs_world_t *world,
    const box_render_draws_t *d,
    const box_draw_parent_t *p)
{
    const ecs_entity_t *children = ecs_vec_get_t(
        &d->ordered_children, ecs_entity_t, p->first);
    int32_t seen = 0;
    bool result = ecs_is_alive(world, p->parent);
    if (!result) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, p->parent);
    while (ecs_children_next(&it)) {
        if (!result || seen + it.count > p->count ||
            ecs_os_memcmp(&children[seen], it.entities, it.count * ECS_SIZEOF(ecs_entity_t)))
        {
            result = false;
        }

        seen += it.count;
    }

    result = result && seen == p->count;
done:
    return result;
}

/** Give the children of a parent with OrderedChildren their rank, the place in its order.
 *
 * @param world The world
 * @param d The state
 * @param parent The parent
 */
static void box_draws_rank_children(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t parent)
{
    box_draw_parent_t *p = ecs_vec_append_t(NULL, &d->ordered_parents, box_draw_parent_t);
    uint64_t rank = 0;
    p->parent = parent;
    p->first = ecs_vec_count(&d->ordered_children);
    p->count = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        int32_t i;
        ecs_entity_t *dst = ecs_vec_grow_t(NULL, &d->ordered_children, ecs_entity_t, it.count);
        ecs_os_memcpy_n(dst, it.entities, ecs_entity_t, it.count);
        for (i = 0; i < it.count; i ++) {
            *ecs_map_ensure(&d->ranks, it.entities[i]) = ++ rank;
        }

        p->count += it.count;
    }
}

/** Get the rank of an entity among its siblings.
 *
 * @param world The world
 * @param d The state
 * @param parent The parent, 0 for a root
 * @param e The entity
 * @return Its place among the children of an OrderedChildren parent, else its id
 */
static uint32_t box_draws_rank(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t parent,
    ecs_entity_t e)
{
    uint32_t result = (uint32_t)e;
    const ecs_map_val_t *rank;
    if (!parent) {
        goto done;
    }

    if (!ecs_map_get(&d->parents, parent)) {
        bool ordered = ecs_has_id(world, parent, EcsOrderedChildren);
        ecs_map_insert(&d->parents, parent, ordered ? 2 : 1);
        if (ordered) {
            box_draws_rank_children(world, d, parent);
        }
    }

    rank = ecs_map_get(&d->ranks, e);
    if (rank) {
        result = (uint32_t)*rank;
    }

done:
    return result;
}

/** Get the z key of an entity: its Position z as bits that sort like the float.
 *
 * @param world The world
 * @param e The entity
 * @return The key, that of 0 without a Position
 */
static uint32_t box_draws_z(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const Position *p = ecs_get(world, e, Position);
    float z = p ? p->z : 0.0f;
    uint32_t bits;
    ecs_os_memcpy(&bits, &z, ECS_SIZEOF(uint32_t));
    if (z == 0.0f) {
        bits = 0;
    }

    return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
}

/** Make the path of an entity once per order: the path of its parent followed by its own key,
 * which holds the z key in its high and the rank among the siblings in its low bits.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @return The offset of the path in the keys of the order in the low and its depth in the high
 * 32 bits
 */
static uint64_t box_draws_path(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const ecs_map_val_t *known = ecs_map_get(&d->paths, e);
    uint64_t result, parent_path = 0;
    int32_t i, first, depth = 0;
    if (known) {
        result = *known;
        goto done;
    }

    ecs_entity_t parent = ecs_get_parent(world, e);
    if (parent) {
        parent_path = box_draws_path(world, d, parent);
        depth = (int32_t)(parent_path >> 32);
    }

    uint32_t z = box_draws_z(world, e);
    uint64_t key = ((uint64_t)z << 32) | box_draws_rank(world, d, parent, e);
    *ecs_map_ensure(&d->order_z, e) = z;
    first = ecs_vec_count(&d->order_keys);
    ecs_vec_grow_t(NULL, &d->order_keys, uint64_t, depth + 1);
    uint64_t *keys = ecs_vec_first_t(&d->order_keys, uint64_t);
    for (i = 0; i < depth; i ++) {
        keys[first + i] = keys[(uint32_t)parent_path + (uint32_t)i];
    }

    keys[first + depth] = key;
    result = ((uint64_t)(depth + 1) << 32) | (uint32_t)first;
    ecs_map_insert(&d->paths, e, result);
done:
    return result;
}

void box_draws_order_position(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const ecs_map_val_t *z = d->order_dirty ? NULL : ecs_map_get(&d->order_z, e);
    if (z && (uint32_t)*z != box_draws_z(world, e)) {
        d->order_dirty = true;
    }
}

/** Check whether an entity or one of its ancestors floats (LayoutFloat).
 *
 * @param world The world
 * @param e The entity
 * @return Whether it floats
 */
static bool box_draws_floating(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    for (; e && !result; e = ecs_get_parent(world, e)) {
        result = ecs_has(world, e, LayoutFloat);
    }

    return result;
}

/** Order two entities: those that do not float first, then by path, a parent before its
 * children, then by id.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative when a draws first
 */
static int box_draws_order_cmp(
    const void *a,
    const void *b)
{
    const box_draw_order_t *x = a, *y = b;
    int32_t i, depth = x->depth < y->depth ? x->depth : y->depth;
    int result = (x->floating > y->floating) - (x->floating < y->floating);
    for (i = 0; !result && i < depth; i ++) {
        result = (x->path[i] > y->path[i]) - (x->path[i] < y->path[i]);
    }

    if (!result) {
        result = (x->depth > y->depth) - (x->depth < y->depth);
    }

    if (!result) {
        result = (x->entity > y->entity) - (x->entity < y->entity);
    }

    return result;
}

/** Collect the entities of the slots of the 2D batches with their paths.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_order_collect(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i, s, count = ecs_vec_count(&d->batches);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        int32_t slots = ecs_vec_count(&b->entities);
        ecs_entity_t last = 0;
        if (!b->alive || b->key.pipeline != BOX_RENDER_SHADER_SHAPE2D) {
            continue;
        }

        for (s = 0; s < slots; s ++) {
            ecs_entity_t e = *ecs_vec_get_t(&b->entities, ecs_entity_t, s);
            if (e == last || ecs_map_get(&d->ordered, e)) {
                continue;
            }

            last = e;
            ecs_map_insert(&d->ordered, e, 1);
            uint64_t path = box_draws_path(world, d, e);
            box_draw_order_t *o = ecs_vec_append_t(NULL, &d->order, box_draw_order_t);
            o->entity = e;
            o->path = (const uint64_t*)(uintptr_t)(uint32_t)path;
            o->depth = (int32_t)(path >> 32);
            o->floating = box_draws_floating(world, e);
        }
    }

    const uint64_t *keys = ecs_vec_first_t(&d->order_keys, uint64_t);
    box_draw_order_t *order = ecs_vec_first_t(&d->order, box_draw_order_t);
    for (i = 0; i < ecs_vec_count(&d->order); i ++) {
        order[i].path = keys + (uintptr_t)order[i].path;
    }
}

/** Add a slot to the runs, extending the last run when the slot follows it.
 *
 * @param d The state
 * @param batch Index of the batch
 * @param slot The slot
 */
static void box_draws_run_add(
    box_render_draws_t *d,
    int32_t batch,
    int32_t slot)
{
    int32_t count = ecs_vec_count(&d->runs);
    box_draw_run_t *run = count ? ecs_vec_get_t(&d->runs, box_draw_run_t, count - 1) : NULL;
    if (run && run->batch == batch && run->first + run->count == (uint32_t)slot &&
        run->count < BOX_DRAWS_RUN_MAX)
    {
        run->count ++;
        goto done;
    }

    run = ecs_vec_append_t(NULL, &d->runs, box_draw_run_t);
    run->scene = box_draws_batch(d, batch)->key.scene;
    run->batch = batch;
    run->first = (uint32_t)slot;
    run->count = 1;
done:
    return;
}

/** Make the runs: the shape slot and then the glyph slots of every entity in order.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_order_runs(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i, g, count = ecs_vec_count(&d->order);
    const box_draw_order_t *order = ecs_vec_first_t(&d->order, box_draw_order_t);
    ecs_vec_clear(&d->runs);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = order[i].entity;
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        const DrawSlot *s = ecs_get(world, e, DrawSlot);
        if (s && s->batch >= 0 &&
            box_draws_batch(d, s->batch)->key.pipeline == BOX_RENDER_SHADER_SHAPE2D)
        {
            box_draws_run_add(d, s->batch, s->slot);
        }

        const TextSlots *ts = ecs_get(world, e, TextSlots);
        int32_t glyphs = ts ? ecs_vec_count(&ts->slots) : 0;
        for (g = 0; g < glyphs; g ++) {
            const box_draw_glyph_slot_t *at = ecs_vec_get_t(
                &ts->slots, box_draw_glyph_slot_t, g);
            box_draws_run_add(d, at->batch, at->slot);
        }
    }
}

/** Check whether an OrderedChildren parent the order read reordered its children.
 *
 * @param world The world
 * @param d The state
 * @return Whether a parent changed
 */
static bool box_draws_order_moved(
    const ecs_world_t *world,
    const box_render_draws_t *d)
{
    int32_t i, count = ecs_vec_count(&d->ordered_parents);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = !box_draws_parent_same(world, d,
            ecs_vec_get_t(&d->ordered_parents, box_draw_parent_t, i));
    }

    return result;
}

/** Make the order of the entities with 2D slots again.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_order_make(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t count;
    d->order_dirty = false;
    ecs_vec_clear(&d->order);
    ecs_vec_clear(&d->order_keys);
    ecs_vec_clear(&d->ordered_parents);
    ecs_vec_clear(&d->ordered_children);
    ecs_map_clear(&d->ordered);
    ecs_map_clear(&d->order_z);
    box_draws_order_collect(world, d);
    count = ecs_vec_count(&d->order);
    if (count > 1) {
        qsort(ecs_vec_first(&d->order), (size_t)count, sizeof(box_draw_order_t),
            box_draws_order_cmp);
    }

    ecs_map_clear(&d->paths);
    ecs_map_clear(&d->ranks);
    ecs_map_clear(&d->parents);
}

void box_draws_order_update(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    if (d->order_dirty || box_draws_order_moved(world, d)) {
        box_draws_order_make(world, d);
        d->runs_dirty = true;
    }

    if (d->runs_dirty) {
        d->runs_dirty = false;
        box_draws_order_runs(world, d);
    }
}

void box_draws_send_ordered(
    box_render_packet_t *p,
    box_render_draws_t *d)
{
    int32_t v, i, count = ecs_vec_count(&d->runs);
    const box_draw_run_t *runs = ecs_vec_first_t(&d->runs, box_draw_run_t);
    for (v = 0; v < ecs_vec_count(&p->views); v ++) {
        const box_render_view_t *view = ecs_vec_get_t(&p->views, box_render_view_t, v);
        for (i = 0; i < count; i ++) {
            const box_draw_run_t *run = &runs[i];
            box_draw_batch_t *b = box_draws_batch(d, run->batch);
            if (run->scene != view->scene || !b->ready) {
                continue;
            }

            box_render_batch_t *out = box_render_packet_batch(p);
            out->view = v;
            out->scene = b->key.scene;
            out->pipeline = b->key.pipeline;
            out->blend = b->key.blend;
            out->back_face = b->key.back_face;
            out->shadow = false;
            out->mesh = b->key.mesh;
            box_draws_batch_textures(d, b, out);
            out->instances = box_draws_store_handle(&d->store, b->page,
                BOX_RENDER_STREAM_TRANSFORM);
            out->first_instance = b->first + run->first;
            out->instance_count = run->count;
        }
    }
}
