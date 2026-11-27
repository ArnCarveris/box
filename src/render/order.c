/**
 * @file render/order.c
 * @brief Draw order of the 2D shapes and glyphs: the order of the hierarchy, not view depth.
 *
 * The entities with 2D slots are sorted by the sibling keys (Position z, then rank) of their
 * ancestors and themselves, floating elements (LayoutFloat) and their descendants after the
 * rest, a clipped overlay (LayoutFloat.clip) after its siblings of the same z.
 *
 * Their slots are drawn per view as runs of consecutive slots. The shapes and glyphs of a tree,
 * the descendants of a topmost entity below the scene roots, are ranked in that order, so the 2D
 * shader lifts the depth of every one over the coplanar elements of its tree drawn before it.
 */

#include "draw.h"
#include <stdlib.h>

/** Most slots one 2D draw holds, so the ordered cull pass splits long runs over workgroups */
#define BOX_DRAWS_RUN_MAX (512)

/** Ranks a shape or glyphs may keep above twice the count of the ranks before it in its tree */
#define BOX_DRAWS_RANK_SLACK (16)

/** Bit of the sibling rank in a path key that puts a clipped overlay after its siblings */
#define BOX_DRAWS_RANK_OVERLAY (0x80000000u)

/** Bits of the sibling rank in a path key below BOX_DRAWS_RANK_OVERLAY */
#define BOX_DRAWS_RANK_MASK (0x7fffffffu)

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
    ecs_map_init(&d->tree_ranks, NULL);
    ecs_map_init(&d->overlays, NULL);
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
    ecs_map_fini(&d->tree_ranks);
    ecs_map_fini(&d->overlays);
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

/** Check whether an entity is a clipped overlay (LayoutFloat with clip).
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is one
 */
static bool box_draws_overlay(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);

    return fl && fl->clip;
}

/** Make the path of an entity once per order: the path of its parent followed by its own key,
 * which holds the z key in its high and the rank among the siblings in its low bits. The top
 * bit of the rank is set for a clipped overlay, which sorts after its siblings of the same z.
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
    uint32_t rank = box_draws_rank(world, d, parent, e) & BOX_DRAWS_RANK_MASK;
    if (box_draws_overlay(world, e)) {
        rank |= BOX_DRAWS_RANK_OVERLAY;
    }

    uint64_t key = ((uint64_t)z << 32) | rank;
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

/** Check whether an entity or one of its ancestors floats (LayoutFloat without clip), and find
 * its tree.
 *
 * @param world The world
 * @param e The entity
 * @param tree Receives the topmost of the entity and its ancestors that is not a scene root, the
 * entity itself when every one is
 * @return Whether it floats
 */
static bool box_draws_floating(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t *tree)
{
    bool result = false;
    *tree = e;
    for (; e; e = ecs_get_parent(world, e)) {
        const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);
        result |= fl && !fl->clip;
        if (!ecs_has(world, e, Scene)) {
            *tree = e;
        }
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
            o->floating = box_draws_floating(world, e, &o->tree);
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

/** Give the next shape or glyphs of a tree a rank: the rank it had while that is above the
 * ranks before it and at most twice their count plus BOX_DRAWS_RANK_SLACK, else the next one.
 * Keeping ranks leaves the shading of the rest of a tree as it is when a slot leaves it.
 *
 * @param tree The ranks of the tree: the next free rank in the low and the count of the ranks
 * given in the high 32 bits
 * @param old The rank the shape or glyphs had
 * @return The rank
 */
static uint32_t box_draws_rank_next(
    ecs_map_val_t *tree,
    uint32_t old)
{
    uint32_t next = (uint32_t)*tree, count = (uint32_t)(*tree >> 32);
    uint32_t result = old >= next && old <= 2 * count + BOX_DRAWS_RANK_SLACK ? old : next;
    *tree = ((uint64_t)(count + 1) << 32) | (result + 1);

    return result;
}

/** Write the rank and the plane record index of a 2D slot into its shading when they changed.
 *
 * @param b The batch
 * @param slot The slot
 * @param rank The rank
 * @param plane The plane record index, 0 for none
 */
static void box_draws_rank_write(
    box_draw_batch_t *b,
    int32_t slot,
    uint32_t rank,
    int32_t plane)
{
    box_render_shading_t *shading = box_draws_slot_shading(b, slot);
    if (shading->ids[1] != rank || shading->plane[0] != (uint32_t)plane) {
        shading->ids[1] = rank;
        shading->plane[0] = (uint32_t)plane;
        box_draws_slot_changed(b, slot, BOX_DRAW_SHADING);
    }
}

/** Find the anchors of the layer groups of the order: the anchors (box_draws_plane_anchor) of
 * the entities that are not their own anchor.
 *
 * @param world The world
 * @param d The state
 * @param anchors Receives the anchors as keys
 */
static void box_draws_order_anchors(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_map_t *anchors)
{
    int32_t i, count = ecs_vec_count(&d->order);
    const box_draw_order_t *order = ecs_vec_first_t(&d->order, box_draw_order_t);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = order[i].entity;
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        ecs_entity_t anchor = box_draws_plane_anchor(world, d, e);
        if (anchor && anchor != e) {
            ecs_map_ensure(anchors, anchor);
        }
    }
}

/** Get the plane record index of an entity: that of the layer group of its anchor, 0 when its
 * anchor anchors no group.
 *
 * @param world The world
 * @param d The state
 * @param anchors The anchors of the groups
 * @param e The entity
 * @return The plane record index, 0 for none
 */
static int32_t box_draws_order_plane(
    ecs_world_t *world,
    box_render_draws_t *d,
    const ecs_map_t *anchors,
    ecs_entity_t e)
{
    ecs_entity_t anchor = box_draws_plane_anchor(world, d, e);
    int32_t result = 0;
    if (anchor && ecs_map_get(anchors, anchor)) {
        result = box_draws_plane_of(d, anchor);
    }

    return result;
}

/** Make the runs: the shadow slot, the shape slot and then the glyph slots of every entity in
 * order.
 *
 * Rank the shadow, the shape and the glyphs of every entity in the order of its tree
 * (box_draws_rank_next), the floating elements of a tree after the rest of it, give them the
 * plane record of their layer group, and write the ranks and planes that changed.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_order_runs(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i, g, count = ecs_vec_count(&d->order);
    const box_draw_order_t *order = ecs_vec_first_t(&d->order, box_draw_order_t);
    ecs_map_t anchors;
    ecs_map_init(&anchors, NULL);
    box_draws_order_anchors(world, d, &anchors);
    ecs_vec_clear(&d->runs);
    ecs_map_clear(&d->tree_ranks);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = order[i].entity;
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        int32_t plane = box_draws_order_plane(world, d, &anchors, e);
        ecs_map_val_t *tree = ecs_map_ensure(&d->tree_ranks, order[i].tree);
        BoxShadowSlot *bs = d->box_shadows ? ecs_get_mut(world, e, BoxShadowSlot) : NULL;
        if (bs && bs->batch >= 0) {
            bs->rank = box_draws_rank_next(tree, bs->rank);
            bs->plane = plane;
            box_draws_run_add(d, bs->batch, bs->slot);
            box_draws_rank_write(box_draws_batch(d, bs->batch), bs->slot, bs->rank, plane);
        }

        DrawSlot *s = ecs_get_mut(world, e, DrawSlot);
        if (s && s->batch >= 0 &&
            box_draws_batch(d, s->batch)->key.pipeline == BOX_RENDER_SHADER_SHAPE2D)
        {
            s->rank = box_draws_rank_next(tree, s->rank);
            s->plane = plane;
            box_draws_run_add(d, s->batch, s->slot);
            box_draws_rank_write(box_draws_batch(d, s->batch), s->slot, s->rank, plane);
        }

        TextSlots *ts = ecs_get_mut(world, e, TextSlots);
        int32_t glyphs = ts ? ecs_vec_count(&ts->slots) : 0;
        if (glyphs) {
            ts->rank = box_draws_rank_next(tree, ts->rank);
            ts->plane = plane;
        }

        for (g = 0; g < glyphs; g ++) {
            const box_draw_glyph_slot_t *at = ecs_vec_get_t(
                &ts->slots, box_draw_glyph_slot_t, g);
            box_draws_run_add(d, at->batch, at->slot);
            box_draws_rank_write(box_draws_batch(d, at->batch), at->slot, ts->rank, plane);
        }
    }

    box_draws_planes_keep(d, &anchors);
    ecs_map_fini(&anchors);
    d->stats.runs = ecs_vec_count(&d->runs);
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
            out->box_shadow = b->key.box_shadow;
            out->slice = b->key.slice;
            out->polygon = b->key.polygon;
            out->mesh = b->key.mesh;
            box_draws_batch_textures(d, b, out);
            out->instances = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
                BOX_RENDER_STREAM_TRANSFORM);
            out->first_instance = b->first + run->first;
            out->instance_count = run->count;
        }
    }
}
