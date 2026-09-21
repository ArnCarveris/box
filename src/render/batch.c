/**
 * @file render/batch.c
 * @brief Static instance slots of a mesh, grouped into runs of one layer mask.
 *
 * A batch without layers is a plain array of slots; once a slot joins a layer
 * the array is partitioned into contiguous runs so a view draws the runs it
 * shows and skips the rest.
 */

#include "render_internal.h"

/** Index of the run of a layer mask.
 *
 * @param batch The batch
 * @param mask The layer mask
 * @return The index, -1 when the batch has no run of that mask
 */
static int32_t box_batch_of_mask(
    const box_batch_t *batch,
    uint32_t mask)
{
    int32_t result = -1;
    const box_slot_group_t *groups = ecs_vec_first_t(&batch->groups, box_slot_group_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&batch->groups) && result < 0; i ++) {
        if (groups[i].mask == mask) {
            result = i;
        }
    }

    return result;
}

/** Index of the run a slot belongs to.
 *
 * @param batch The batch
 * @param slot The slot
 * @return The index, -1 when no run holds the slot
 */
static int32_t box_batch_of_slot(
    const box_batch_t *batch,
    uint32_t slot)
{
    int32_t result = -1;
    const box_slot_group_t *groups = ecs_vec_first_t(&batch->groups, box_slot_group_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&batch->groups) && result < 0; i ++) {
        if (slot >= groups[i].first && slot < groups[i].first + groups[i].count) {
            result = i;
        }
    }

    return result;
}

/** Give a batch that has no runs one run of mask zero over its slots.
 *
 * @param batch The batch
 */
static void box_batch_start_groups(
    box_batch_t *batch)
{
    if (ecs_vec_count(&batch->groups) || !ecs_vec_count(&batch->entities)) {
        goto done;
    }

    box_slot_group_t *group = ecs_vec_append_t(NULL, &batch->groups, box_slot_group_t);
    group->mask = 0;
    group->first = 0;
    group->count = (uint32_t)ecs_vec_count(&batch->entities);
    batch->groups_dirty = true;
done:
    return;
}

/** Drop a run from the list while the remaining runs keep their order.
 *
 * @param batch The batch
 * @param index Index of the run
 */
static void box_batch_erase_group(
    box_batch_t *batch,
    int32_t index)
{
    int32_t count = ecs_vec_count(&batch->groups);
    box_slot_group_t *groups = ecs_vec_first_t(&batch->groups, box_slot_group_t);
    if (index < count - 1) {
        memmove(groups + index, groups + index + 1,
            (size_t)(count - index - 1) * sizeof(box_slot_group_t));
    }

    ecs_vec_remove_last(&batch->groups);
}

/** Move the entity of one slot to another and queue its instance data again.
 *
 * @param world The world
 * @param batch The batch
 * @param from The slot the entity is in
 * @param to The slot it moves to
 */
static void box_batch_move(
    ecs_world_t *world,
    box_batch_t *batch,
    uint32_t from,
    uint32_t to)
{
    ecs_entity_t *slots = ecs_vec_first_t(&batch->entities, ecs_entity_t);
    ecs_entity_t moved = slots[from];
    slots[to] = moved;
    RenderInstance *inst = ecs_get_mut(world, moved, RenderInstance);
    ecs_assert(inst != NULL, ECS_INTERNAL_ERROR, NULL);
    inst->slot = to;
    box_render_mark_dirty(world, moved);
}

void box_render_take_slot(
    ecs_world_t *world,
    box_batch_t *batch,
    ecs_entity_t e,
    RenderInstance *inst)
{
    uint32_t mask = inst->layer_mask;
    batch->count_dirty = true;
    if (!ecs_vec_count(&batch->groups) && !mask) {
        inst->slot = (uint32_t)ecs_vec_count(&batch->entities);
        ecs_vec_append_t(NULL, &batch->entities, ecs_entity_t)[0] = e;
        goto done;
    }

    box_batch_start_groups(batch);
    int32_t g = box_batch_of_mask(batch, mask);
    if (g < 0) {
        box_slot_group_t *created = ecs_vec_append_t(NULL, &batch->groups, box_slot_group_t);
        created->mask = mask;
        created->first = (uint32_t)ecs_vec_count(&batch->entities);
        created->count = 0;
        g = ecs_vec_count(&batch->groups) - 1;
    }

    ecs_vec_append_t(NULL, &batch->entities, ecs_entity_t)[0] = e;
    uint32_t hole = (uint32_t)ecs_vec_count(&batch->entities) - 1;
    int32_t i;
    for (i = ecs_vec_count(&batch->groups) - 1; i > g; i --) {
        box_slot_group_t *next = ecs_vec_get_t(&batch->groups, box_slot_group_t, i);
        box_batch_move(world, batch, next->first, hole);
        next->first ++;
        hole = next->first - 1;
    }

    ecs_vec_get_t(&batch->entities, ecs_entity_t, (int32_t)hole)[0] = e;
    inst->slot = hole;
    ecs_vec_get_t(&batch->groups, box_slot_group_t, g)->count ++;
    batch->groups_dirty = true;
done:
    return;
}

void box_render_release_slot(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    RenderInstance *inst)
{
    box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)inst->mesh);
    ecs_assert(ecs_vec_get_t(&batch->entities, ecs_entity_t, (int32_t)inst->slot)[0] == e,
        ECS_INTERNAL_ERROR, NULL);
    batch->count_dirty = true;
    batch->removed = true;
    if (!ecs_vec_count(&batch->groups)) {
        int32_t last = ecs_vec_count(&batch->entities) - 1;
        ecs_entity_t *slots = ecs_vec_first_t(&batch->entities, ecs_entity_t);
        if (slots[last] != e) {
            box_batch_move(world, batch, (uint32_t)last, inst->slot);
        }

        ecs_vec_remove_last(&batch->entities);
        goto done;
    }

    int32_t g = box_batch_of_slot(batch, inst->slot);
    ecs_assert(g >= 0, ECS_INTERNAL_ERROR, NULL);
    box_slot_group_t *group = ecs_vec_get_t(&batch->groups, box_slot_group_t, g);
    uint32_t hole = group->first + group->count - 1;
    if (hole != inst->slot) {
        box_batch_move(world, batch, hole, inst->slot);
    }

    group->count --;
    int32_t i;
    for (i = g + 1; i < ecs_vec_count(&batch->groups); i ++) {
        box_slot_group_t *next = ecs_vec_get_t(&batch->groups, box_slot_group_t, i);
        uint32_t tail = next->first + next->count - 1;
        box_batch_move(world, batch, tail, hole);
        next->first --;
        hole = tail;
    }

    ecs_vec_remove_last(&batch->entities);
    batch->groups_dirty = true;
    if (!ecs_vec_get_t(&batch->groups, box_slot_group_t, g)->count) {
        box_batch_erase_group(batch, g);
    }

done:
    return;
}

void box_render_regroup_slot(
    ecs_world_t *world,
    RenderState *state,
    box_batch_t *batch,
    ecs_entity_t e,
    RenderInstance *inst)
{
    if (!ecs_vec_count(&batch->groups)) {
        if (!inst->layer_mask) {
            goto done;
        }
    } else {
        int32_t g = box_batch_of_slot(batch, inst->slot);
        ecs_assert(g >= 0, ECS_INTERNAL_ERROR, NULL);
        if (ecs_vec_get_t(&batch->groups, box_slot_group_t, g)->mask == inst->layer_mask) {
            goto done;
        }
    }

    box_render_release_slot(world, state, e, inst);
    box_render_take_slot(world, batch, e, inst);
done:
    return;
}

void box_render_extract_groups(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->batches); i ++) {
        box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, i);
        if (!batch->groups_dirty) {
            continue;
        }

        int32_t g;
        for (g = 0; g < ecs_vec_count(&batch->groups); g ++) {
            const box_slot_group_t *group = ecs_vec_get_t(&batch->groups, box_slot_group_t, g);
            box_static_group_t *out = box_vec_push(&p->static_groups);
            out->mesh = (uint32_t)i;
            out->mask = group->mask;
            out->first = group->first;
            out->count = group->count;
        }

        if (!ecs_vec_count(&batch->groups)) {
            box_static_group_t *out = box_vec_push(&p->static_groups);
            out->mesh = (uint32_t)i;
            out->mask = 0;
            out->first = 0;
            out->count = 0;
        }

        batch->groups_dirty = false;
    }
}
