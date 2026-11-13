/**
 * @file render/transition_compare.c
 * @brief Comparison of the transitioned components of a node to the last targets the extract
 * sent: the start records of changes, of an added component that animates in from its neutral
 * value and of a removed one that animates out to it.
 */

#include "transitions.h"

/** Send a start record and remember it as the last one of the slot.
 *
 * @param p The packet
 * @param node The node id
 * @param component The component
 * @param slot The slot of the component, whose value is the target
 * @param spec The transition, NULL to set the value without one
 */
static void box_transition_send(
    box_render_packet_t *p,
    uint32_t node,
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const TransitionSpec *spec)
{
    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_START, node);
    slot->start = p->time;
    slot->delay = spec ? spec->delay : 0.0f;
    slot->duration = spec ? spec->duration : 0.0f;
    slot->timing_function = spec ? spec->timing_function : Linear;
    r->component = component;
    r->timing_function = slot->timing_function;
    r->bezier = slot->timing_function == CubicBezier ? spec->bezier : (TransitionBezier){0};
    r->delay = slot->delay;
    r->duration = slot->duration;
    r->start = slot->start;
    ecs_os_memcpy(r->value, slot->value, BOX_RENDER_TRANSITION_VALUE);
}

/** Set or widen the per field range of the targets of a transform component, a BoxShadow or
 * a light.
 *
 * @param component The component, nothing for one box_render_transition_ranged rejects
 * @param slot The slot of the component
 * @param value A target, in the layout of the component
 * @param reset Whether the range becomes the target alone
 */
static void box_transition_range_add(
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const uint8_t *value,
    bool reset)
{
    const box_render_transition_layout_t *layout;
    int32_t i;
    if (!box_render_transition_ranged(component)) {
        goto done;
    }

    layout = box_render_transition_layout(component);
    for (i = 0; i < BOX_TRANSITION_RANGE && i < layout->count &&
        layout->fields[i].kind == BOX_RENDER_TRANSITION_F32; i ++)
    {
        float v;
        ecs_os_memcpy(&v, ECS_OFFSET(value, layout->fields[i].offset), ECS_SIZEOF(float));
        slot->lo[i] = reset ? v : glm_min(slot->lo[i], v);
        slot->hi[i] = reset ? v : glm_max(slot->hi[i], v);
    }
done:
    return;
}

bool box_transitions_opts(
    const ecs_world_t *world,
    box_render_transition_component_t component,
    const TransitionSpec *spec,
    bool remove)
{
    return spec && (remove ? spec->on_remove : spec->on_add) &&
        box_transition_neutral_supported(world, box_transitions_id(component), remove);
}

/** Start a transition of a slot to a new target, from the value the node renders.
 *
 * @param p The packet
 * @param node The node id
 * @param component The component
 * @param slot The slot of the component
 * @param spec The transition
 * @param packed The target, packed
 */
static void box_transition_change(
    box_render_packet_t *p,
    uint32_t node,
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const TransitionSpec *spec,
    const uint8_t *packed)
{
    if (!box_render_transition_running(slot->start, slot->delay, slot->duration, p->time)) {
        box_transition_range_add(component, slot, slot->value, true);
    }

    box_transition_range_add(component, slot, packed, false);
    box_render_transition_widen(component, spec, slot->lo, slot->hi);
    ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
    box_transition_send(p, node, component, slot, spec);
}

/** Check whether a component is a material lobe that keeps a shape on the extended mesh
 * shader while it leaves (box_render_transitions_leaving).
 *
 * @param component The component
 * @return Whether it is Sheen, ClearCoat, SpecularAnisotropy, DiffuseTransmission or Fresnel
 */
static bool box_transition_lobe(
    box_render_transition_component_t component)
{
    return component == BOX_RENDER_TRANSITION_SHEEN ||
        component == BOX_RENDER_TRANSITION_CLEAR_COAT ||
        component == BOX_RENDER_TRANSITION_ANISOTROPY ||
        component == BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION ||
        component == BOX_RENDER_TRANSITION_FRESNEL;
}

/** Mark the shapes that read the material lobes of an entity as changed: the entity and its
 * IsA instances, down every level.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity of a node
 */
static void box_transition_lobe_left(
    ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e)
{
    int32_t i;
    if (!s->draws || !ecs_is_alive(world, e)) {
        goto done;
    }

    ecs_vec_clear(&s->instances);
    *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = e;
    while (ecs_vec_count(&s->instances)) {
        ecs_entity_t cur = *ecs_vec_last_t(&s->instances, ecs_entity_t);
        ecs_vec_remove_last(&s->instances);
        box_draws_mark(world, s->draws, cur, BOX_DRAW_ALL);
        ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, cur));
        while (ecs_each_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = it.entities[i];
            }
        }
    }
done:
    return;
}

void box_transitions_lobes_left(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_transition_node_t *node)
{
    bool leaving = false;
    int32_t c;
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS && !leaving; c ++) {
        leaving = node->slots[c].leaving &&
            box_transition_lobe((box_render_transition_component_t)c);
    }

    if (leaving) {
        box_transition_lobe_left(world, s, node->entity);
    }
}

void box_transitions_compare(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p,
    ecs_entity_t e,
    uint32_t node,
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const TransitionSpec *spec,
    const void *value)
{
    uint8_t packed[BOX_RENDER_TRANSITION_VALUE];
    bool added = slot->absent, leaving = slot->leaving;
    slot->absent = false;
    if (value) {
        box_render_transition_pack(component, value, packed);
    }

    if (spec && value) {
        if (!slot->present && added && box_transitions_opts(world, component, spec, false)) {
            box_render_transition_neutral(component, packed, slot->value);
            box_transition_range_add(component, slot, slot->value, true);
            slot->present = true;
            box_transition_send(p, node, component, slot, NULL);
            box_transition_change(p, node, component, slot, spec, packed);
        } else if (!slot->present) {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
            box_transition_range_add(component, slot, packed, true);
            slot->present = true;
            box_transition_send(p, node, component, slot, NULL);
        } else if (!box_render_transition_equal(component, slot->value, packed)) {
            box_transition_change(p, node, component, slot, spec, packed);
        } else if (component == BOX_RENDER_TRANSITION_POINT_LIGHT ||
            component == BOX_RENDER_TRANSITION_SPOT_LIGHT)
        {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
        }

        slot->leaving = false;
    } else if (slot->leaving && !value && box_transitions_opts(world, component, spec, true) &&
        !ecs_has_id(world, e, box_transitions_id(component)))
    {
        if (!box_render_transition_running(slot->start, slot->delay, slot->duration, p->time)) {
            slot->present = false;
            slot->leaving = false;
        }
    } else if (slot->present && !value && !slot->leaving &&
        box_transitions_opts(world, component, spec, true) &&
        !ecs_has_id(world, e, box_transitions_id(component)))
    {
        box_render_transition_neutral(component, slot->value, packed);
        box_transition_change(p, node, component, slot, spec, packed);
        slot->leaving = true;
    } else if (slot->present) {
        if (value) {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
            box_transition_range_add(component, slot, packed, true);
        }

        box_transition_send(p, node, component, slot, NULL);
        slot->present = false;
        slot->leaving = false;
    }

    if (!value && box_transitions_opts(world, component, spec, false)) {
        slot->absent = !ecs_has_id(world, e, box_transitions_id(component));
    }

    if (slot->leaving) {
        s->leaving ++;
    } else if (leaving && box_transition_lobe(component)) {
        box_transition_lobe_left(world, s, e);
    }
}
