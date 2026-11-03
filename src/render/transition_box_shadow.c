/**
 * @file render/transition_box_shadow.c
 * @brief Transitions of BoxShadow on the game thread: the drawn entities whose shadow lifts in a
 * frame, the tag that keys their shadow slot dynamic, and the records that patch it.
 *
 * The shadow box shapes both the instance matrix and the shading of the shadow, so a record
 * carries the whole target of the shadow and the resolver writes both again from the BoxShadow
 * its node renders, on the world matrix its shape renders at.
 */

#include "transitions.h"

/** Add the drawn entities that read the BoxShadow of a node with an active BoxShadow
 * transition: the entity and its IsA instances, down every level, but for the tables that own
 * a BoxShadow of their own, which an instance node of its own transitions.
 *
 * Transition is inherited over IsA, so this visits only entities with Transition.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity of the node
 * @param node Its node id
 */
static void box_transition_box_shadow_walk(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e,
    uint32_t node)
{
    int32_t i;
    ecs_vec_clear(&s->instances);
    *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = e;
    while (ecs_vec_count(&s->instances)) {
        ecs_entity_t cur = *ecs_vec_last_t(&s->instances, ecs_entity_t);
        ecs_vec_remove_last(&s->instances);
        if (!ecs_has_id(world, cur, EcsPrefab) && ecs_has(world, cur, DrawSlot) &&
            ecs_has(world, cur, BoxShadowSlot) && !ecs_map_get(&s->box_shadow_index, cur))
        {
            *ecs_vec_append_t(NULL, &s->box_shadows, box_transition_box_shadow_t) =
                (box_transition_box_shadow_t){ .entity = cur, .node = node };
            ecs_map_insert(&s->box_shadow_index, cur,
                (ecs_map_val_t)ecs_vec_count(&s->box_shadows));
            box_transitions_keep(s, &s->tagged_box_shadow, cur);
        }

        ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, cur));
        while (ecs_each_next(&it)) {
            if (ecs_table_has_id(world, it.table, ecs_id(BoxShadow))) {
                continue;
            }

            for (i = 0; i < it.count; i ++) {
                *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = it.entities[i];
            }
        }
    }
}

void box_transitions_box_shadows(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&s->nodes);
    ecs_vec_clear(&s->box_shadows);
    ecs_vec_clear(&s->tag_add);
    ecs_vec_clear(&s->tag_remove);
    if (ecs_map_count(&s->box_shadow_index)) {
        ecs_map_clear(&s->box_shadow_index);
    }

    for (i = 1; p && i < count; i ++) {
        const box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, i);
        if (node->entity && box_render_transitions_active(
                s, (uint32_t)i, BOX_RENDER_TRANSITION_BOX_SHADOW, p->time) &&
            ecs_is_alive(world, node->entity))
        {
            box_transition_box_shadow_walk(world, s, node->entity, (uint32_t)i);
        }
    }

    if (ecs_map_count(&s->tagged_box_shadow)) {
        box_transitions_retag(world, s, &s->tagged_box_shadow, TransitionBoxShadow);
    }
}

box_transition_box_shadow_t* box_transitions_box_shadow_of(
    const box_render_transitions_t *s,
    ecs_entity_t e)
{
    const ecs_map_val_t *found = ecs_map_count(&s->box_shadow_index)
        ? ecs_map_get(&s->box_shadow_index, e) : NULL;

    return found ? ecs_vec_get_t(&s->box_shadows, box_transition_box_shadow_t,
        (int32_t)*found - 1) : NULL;
}

/** Get the packet data offset of the transform of a slot of a dynamic_transform batch that
 * uploaded its run into this packet.
 *
 * @param d The state of the batches
 * @param p The packet
 * @param batch Index of the batch
 * @param slot The slot
 * @param out Receives the offset
 * @return Whether the batch uploaded the transform of the slot
 */
static bool box_transition_transform_data(
    box_render_draws_t *d,
    const box_render_packet_t *p,
    int32_t batch,
    int32_t slot,
    uint32_t *out)
{
    box_draw_batch_t *b = batch >= 0 && batch < ecs_vec_count(&d->batches)
        ? box_draws_batch(d, batch) : NULL;
    bool result = b && b->alive && b->key.dynamic_transform && b->transform_frame == p->frame &&
        slot >= 0 && slot < ecs_vec_count(&b->transforms);
    if (result) {
        *out = b->transform_data + (uint32_t)slot * (uint32_t)sizeof(box_render_transform_t);
    }

    return result;
}

void box_transitions_send_box_shadows(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_draw_transition_box_shadow_t record;
    box_draw_material_t material;
    uint32_t shading, transform, data;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->box_shadows); i ++) {
        const box_transition_box_shadow_t *item = ecs_vec_get_t(
            &s->box_shadows, box_transition_box_shadow_t, i);
        const DrawSlot *ds = ecs_is_alive(world, item->entity)
            ? ecs_get(world, item->entity, DrawSlot) : NULL;
        const BoxShadowSlot *bs = ds && ds->batch >= 0
            ? ecs_get(world, item->entity, BoxShadowSlot) : NULL;
        if (!bs || bs->batch < 0 ||
            !box_transitions_shading_data(d, p, bs->batch, bs->slot, &shading) ||
            !box_transition_transform_data(d, p, bs->batch, bs->slot, &transform))
        {
            continue;
        }

        box_draws_shape_material(world, item->entity, ds, &material);
        if (!box_draws_box_shadow_transition(world, d, item->entity, ds, &material, &record)) {
            continue;
        }

        box_transitions_fade_target(s, item->entity, p->time, &record.material);
        record.material.nodes[BOX_RENDER_TRANSITION_BOX_SHADOW] = item->node;
        record.transform = transform;
        if (item->moving) {
            ecs_os_memcpy(record.rel, item->rel, ECS_SIZEOF(mat4));
        }

        void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(record), &data);
        ecs_os_memcpy(dst, &record, ECS_SIZEOF(record));
        box_render_transition_record_t *r = box_render_packet_transition(
            p, BOX_RENDER_TRANSITION_SHADOW_BOX, item->node);
        r->parent = item->moving;
        r->data = data;
        r->offset = shading;
    }
}
