/**
 * @file render/transition_compare.c
 * @brief Comparison of the transitioned components of a node to the last targets the extract
 * sent: the start records of changes, of an added component that animates in from its neutral
 * value and of a removed one that animates out to it.
 */

#include "transitions.h"

ECS_COMPONENT_DECLARE(TransitionNode);

_Static_assert(BOX_RENDER_TRANSITION_COMPONENTS <= 32, "a bit per component in node live");

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
 * @return Whether it is Sheen, ClearCoat, SpecularAnisotropy, DiffuseTransmission, Fresnel or
 * Distortion, which keeps the shape transmissive
 */
static bool box_transition_lobe(
    box_render_transition_component_t component)
{
    return component == BOX_RENDER_TRANSITION_SHEEN ||
        component == BOX_RENDER_TRANSITION_CLEAR_COAT ||
        component == BOX_RENDER_TRANSITION_ANISOTROPY ||
        component == BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION ||
        component == BOX_RENDER_TRANSITION_FRESNEL ||
        component == BOX_RENDER_TRANSITION_DISTORTION;
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
    bool light = component == BOX_RENDER_TRANSITION_POINT_LIGHT ||
        component == BOX_RENDER_TRANSITION_SPOT_LIGHT;
    bool same = spec && value && slot->present &&
        box_render_transition_equal(component, slot->value, value);
    slot->absent = false;
    if (value && (!same || light)) {
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
        } else if (!same) {
            box_transition_change(p, node, component, slot, spec, packed);
        } else if (light) {
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

bool box_transitions_runs(
    const box_transition_node_t *node,
    double time)
{
    bool result = node->live && time < node->until;
    int32_t c;
    for (c = BOX_RENDER_TRANSITION_POINT_LIGHT; !result && node->live &&
        c <= BOX_RENDER_TRANSITION_SPOT_LIGHT; c ++)
    {
        const box_transition_slot_t *slot = &node->slots[c];
        result = (node->live & (1u << c)) && slot->present && (slot->leaving ||
            box_render_transition_running(slot->start, slot->delay, slot->duration, time));
    }

    return result;
}

box_render_packet_t* box_transitions_sweep(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&s->nodes);
    for (i = 1; i < count && s->node_count; i ++) {
        box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, i);
        if (!node->entity || node->seen == s->stamp) {
            continue;
        }

        if (!p) {
            p = box_render_packet(world);
        }

        box_render_packet_transition(p, BOX_RENDER_TRANSITION_DESTROY, (uint32_t)i);
        box_transitions_lobes_left(world, s, node);
        s->node_count --;
        node->entity = 0;
        *ecs_vec_append_t(NULL, &s->free, uint32_t) = (uint32_t)i;
    }

    return p;
}

uint32_t box_transitions_node_new(
    box_render_transitions_t *s,
    box_render_packet_t *p,
    ecs_entity_t entity,
    TransitionNode *out)
{
    uint32_t result;
    int32_t free_count = ecs_vec_count(&s->free);
    if (free_count) {
        result = *ecs_vec_get_t(&s->free, uint32_t, free_count - 1);
        ecs_vec_remove_last(&s->free);
    } else {
        result = (uint32_t)ecs_vec_count(&s->nodes);
        if (!result) {
            ecs_vec_append_t(NULL, &s->nodes, box_transition_node_t)->entity = 0;
            result = 1;
        }

        ecs_vec_append_t(NULL, &s->nodes, box_transition_node_t);
    }

    box_transition_node_t *node = ecs_vec_get_t(
        &s->nodes, box_transition_node_t, (int32_t)result);
    ecs_os_memset_t(node, 0, box_transition_node_t);
    node->entity = entity;
    s->node_count ++;
    if (out) {
        out->id = result;
    } else {
        *ecs_vec_append_t(NULL, &s->node_add, uint32_t) = result;
    }

    box_render_packet_transition(p, BOX_RENDER_TRANSITION_CREATE, result);

    return result;
}

/** Copy TransitionNode values: a copy is a new entity that has no node yet.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transitions_node_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    ecs_os_memset(dst, 0, ti->size * count);
}

/** Move TransitionNode values with the entity they belong to.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transitions_node_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
}

void box_transitions_node_define(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, TransitionNode);
    ecs_set_hooks(world, TransitionNode, {
        .ctor = flecs_default_ctor,
        .copy = box_transitions_node_copy,
        .move = box_transitions_node_move
    });
    ecs_add_pair(world, ecs_id(TransitionNode), EcsOnInstantiate, EcsDontInherit);
}

void box_transitions_node_attach(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->node_add); i ++) {
        uint32_t id = *ecs_vec_get_t(&s->node_add, uint32_t, i);
        const box_transition_node_t *node = box_transitions_node_state(s, id);
        if (node && ecs_is_alive(world, node->entity)) {
            ecs_ensure(world, node->entity, TransitionNode)->id = id;
        }
    }

    ecs_vec_clear(&s->node_add);
}

const box_transition_node_t* box_transitions_running(
    const box_render_transitions_t *s,
    int32_t index,
    int32_t *id)
{
    *id = *ecs_vec_get_t(&s->running, int32_t, index);

    return ecs_vec_get_t(&s->nodes, box_transition_node_t, *id);
}

bool box_transitions_specs(
    const ecs_world_t *world,
    const Transition *map,
    void *const *columns,
    const TransitionSpec **specs,
    uint32_t *mask)
{
    bool result = false;
    int32_t c;
    ecs_os_memset_n(specs, 0, const TransitionSpec*, BOX_RENDER_TRANSITION_COMPONENTS);
    *mask = 0;
    if (!ecs_map_is_init(map)) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(map);
    while (ecs_map_next(&it)) {
        ecs_entity_t cid = ecs_map_key(&it);
        const TransitionSpec *spec = ecs_map_ptr(&it);
        for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
            box_render_transition_component_t component = (box_render_transition_component_t)c;
            if (box_transitions_id(component) != cid) {
                continue;
            }

            if (spec && box_transition_valid(world, cid, spec)) {
                specs[c] = spec;
                *mask |= 1u << c;
                result |= columns[c] || box_transitions_opts(world, component, spec, false) ||
                    box_transitions_opts(world, component, spec, true);
            }

            break;
        }
    }

done:
    return result;
}

void box_transitions_track(
    box_transition_node_t *node,
    box_render_transition_component_t component)
{
    const box_transition_slot_t *slot = &node->slots[component];
    uint32_t bit = 1u << component;
    double end = slot->start + (double)slot->delay +
        (slot->duration > 0.0f ? (double)slot->duration : 0.0);
    node->live = (slot->present || slot->leaving || slot->absent)
        ? (node->live | bit) : (node->live & ~bit);
    if (end > node->until) {
        node->until = end;
    }
}
