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

/** Set or widen the per field range of the targets of a transform component or a BoxShadow.
 *
 * @param component The component, nothing for one other than Position, Rotation, Scale or
 * BoxShadow
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
    if (component > BOX_RENDER_TRANSITION_SCALE &&
        component != BOX_RENDER_TRANSITION_BOX_SHADOW)
    {
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

void box_transitions_compare(
    const ecs_world_t *world,
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
    bool added = slot->absent;
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
    }
}
