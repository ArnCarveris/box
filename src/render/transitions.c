/**
 * @file render/transitions.c
 * @brief Transition extract: finds the changes of transitioned components on the game thread
 * and sends them to the transition resolver as packet records.
 *
 * One cached query over the entities with Transition, owned or inherited over IsA, prefabs
 * included, compares each transitioned component to the last target it sent. Entities without
 * Transition are not visited, and a world that does not use Transition matches no table.
 */

#include "render.h"

/** A transitioned component of an entity, as the extract last sent it */
typedef struct box_transition_slot_t {
    bool present;                   /**< A target was sent and is the value of the node */
    TimingFunction timing_function; /**< Curve of the last start */
    float delay;                    /**< Delay of the last start */
    float duration;                 /**< Duration of the last start */
    double start;                   /**< Time of the last start */
    uint8_t value[BOX_RENDER_TRANSITION_VALUE]; /**< The last target sent */
} box_transition_slot_t;

/** Private state of an entity with Transition */
typedef struct box_transition_node_t {
    ecs_entity_t entity;            /**< The entity, 0 for a free node */
    int64_t seen;                   /**< Stamp of the last extract that matched the entity */
    box_transition_slot_t slots[BOX_RENDER_TRANSITION_COMPONENTS]; /**< By
                                     *   box_render_transition_component_t */
} box_transition_node_t;

/** Main thread state of the transition extract */
struct box_render_transitions_t {
    ecs_query_t *query;             /**< Transition(self|up IsA), prefabs included */
    ecs_map_t by_entity;            /**< Entity to node id */
    ecs_vec_t nodes;                /**< box_transition_node_t by node id, 0 unused */
    ecs_vec_t free;                 /**< uint32_t node ids to reuse */
    int64_t stamp;                  /**< Extracts run */
};

/** Get the component id of a transitionable component.
 *
 * @param component The component
 * @return The id
 */
static ecs_entity_t box_transition_id(
    box_render_transition_component_t component)
{
    ecs_entity_t result = 0;
    switch (component) {
    case BOX_RENDER_TRANSITION_POSITION: result = ecs_id(Position); break;
    case BOX_RENDER_TRANSITION_ROTATION: result = ecs_id(Rotation); break;
    case BOX_RENDER_TRANSITION_SCALE: result = ecs_id(Scale); break;
    case BOX_RENDER_TRANSITION_RGB: result = ecs_id(Rgb); break;
    case BOX_RENDER_TRANSITION_OPACITY: result = ecs_id(Opacity); break;
    case BOX_RENDER_TRANSITION_ALPHA_CUTOFF: result = ecs_id(AlphaCutoff); break;
    case BOX_RENDER_TRANSITION_TINT: result = ecs_id(Tint); break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL: result = ecs_id(PbrMaterial); break;
    case BOX_RENDER_TRANSITION_EMISSIVE: result = ecs_id(Emissive); break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM: result = ecs_id(TextureTransform); break;
    case BOX_RENDER_TRANSITION_COMPONENTS: break;
    }

    return result;
}

bool box_render_transition_component(
    ecs_entity_t component,
    box_render_transition_component_t *out)
{
    bool result = false;
    int32_t c;
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS && !result; c ++) {
        if (component && box_transition_id((box_render_transition_component_t)c) == component) {
            *out = (box_render_transition_component_t)c;
            result = true;
        }
    }

    return result;
}

/** Get the private state of a node.
 *
 * @param s The state of the extract
 * @param node The node id
 * @return The node, NULL when the id is not in use
 */
static box_transition_node_t* box_transition_node(
    const box_render_transitions_t *s,
    uint32_t node)
{
    box_transition_node_t *result = NULL;
    if (node && (int32_t)node < ecs_vec_count(&s->nodes)) {
        result = ecs_vec_get_t(&s->nodes, box_transition_node_t, (int32_t)node);
        result = result->entity ? result : NULL;
    }

    return result;
}

/** Give an entity a node, reusing a freed id first, and send its creation.
 *
 * @param s The state of the extract
 * @param p The packet
 * @param entity The entity
 * @return The node id
 */
static uint32_t box_transition_node_new(
    box_render_transitions_t *s,
    box_render_packet_t *p,
    ecs_entity_t entity)
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
    ecs_map_insert(&s->by_entity, entity, result);
    box_render_packet_transition(p, BOX_RENDER_TRANSITION_CREATE, result);

    return result;
}

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
    r->delay = slot->delay;
    r->duration = slot->duration;
    r->start = slot->start;
    ecs_os_memcpy(r->value, slot->value, BOX_RENDER_TRANSITION_VALUE);
}

/** Compare a transitioned component of an entity to the last target sent and send what
 * changed.
 *
 * The first value sets without a transition, a changed value starts one with the spec of the
 * entry, and a value whose entry or component is gone snaps to its target.
 *
 * @param p The packet
 * @param node The node id
 * @param component The component
 * @param slot The slot of the component
 * @param spec The valid entry of the component, NULL when there is none
 * @param value The value the entity owns, NULL when it owns none
 */
static void box_transition_compare(
    box_render_packet_t *p,
    uint32_t node,
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const TransitionSpec *spec,
    const void *value)
{
    uint8_t packed[BOX_RENDER_TRANSITION_VALUE];
    if (value) {
        box_render_transition_pack(component, value, packed);
    }

    if (spec && value) {
        if (!slot->present) {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
            slot->present = true;
            box_transition_send(p, node, component, slot, NULL);
        } else if (!box_render_transition_equal(component, slot->value, packed)) {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
            box_transition_send(p, node, component, slot, spec);
        }
    } else if (slot->present) {
        if (value) {
            ecs_os_memcpy(slot->value, packed, BOX_RENDER_TRANSITION_VALUE);
        }

        box_transition_send(p, node, component, slot, NULL);
        slot->present = false;
    }
}

/** Find the changes of the transitioned components of the entities with Transition, send the
 * creation of a node for each new one and the destruction of the nodes of entities that lost
 * Transition or were deleted.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTransitions(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = NULL;
    int32_t i, c, count;
    s->stamp ++;
    ecs_iter_t qit = ecs_query_iter(world, s->query);
    while (ecs_query_next(&qit)) {
        const Transition *maps = ecs_field(&qit, Transition, 0);
        bool shared = !ecs_field_is_self(&qit, 0);
        void *columns[BOX_RENDER_TRANSITION_COMPONENTS];
        if (!p) {
            p = box_render_packet(world);
        }

        for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
            columns[c] = ecs_table_get_id(world, qit.table,
                box_transition_id((box_render_transition_component_t)c), qit.offset);
        }

        for (i = 0; i < qit.count; i ++) {
            ecs_entity_t e = qit.entities[i];
            const Transition *map = &maps[shared ? 0 : i];
            ecs_map_val_t *found = ecs_map_get(&s->by_entity, e);
            uint32_t id = found ? (uint32_t)*found : box_transition_node_new(s, p, e);
            box_transition_node_t *node = ecs_vec_get_t(
                &s->nodes, box_transition_node_t, (int32_t)id);
            node->seen = s->stamp;
            for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
                box_render_transition_component_t component =
                    (box_render_transition_component_t)c;
                ecs_entity_t cid = box_transition_id(component);
                const TransitionSpec *spec = ecs_map_is_init(map)
                    ? ecs_map_get_deref(map, TransitionSpec, cid) : NULL;
                const void *value = columns[c] ? ECS_ELEM(columns[c],
                    box_render_transition_layout(component)->size, i) : NULL;
                if (spec && !box_transition_valid(world, cid, spec)) {
                    spec = NULL;
                }

                box_transition_compare(p, id, component, &node->slots[c], spec, value);
            }
        }
    }

    count = ecs_vec_count(&s->nodes);
    for (i = 1; i < count && ecs_map_count(&s->by_entity); i ++) {
        box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, i);
        if (!node->entity || node->seen == s->stamp) {
            continue;
        }

        if (!p) {
            p = box_render_packet(world);
        }

        box_render_packet_transition(p, BOX_RENDER_TRANSITION_DESTROY, (uint32_t)i);
        ecs_map_remove(&s->by_entity, node->entity);
        node->entity = 0;
        *ecs_vec_append_t(NULL, &s->free, uint32_t) = (uint32_t)i;
    }
}

/** Free the state of the transition extract.
 *
 * @param ctx The state
 */
static void box_transitions_free(
    void *ctx)
{
    box_render_transitions_t *s = ctx;
    ecs_map_fini(&s->by_entity);
    ecs_vec_fini_t(NULL, &s->nodes, box_transition_node_t);
    ecs_vec_fini_t(NULL, &s->free, uint32_t);
    ecs_os_free(s);
}

box_render_transitions_t* box_render_transitions_get(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractTransitions");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;

    return sys ? sys->ctx : NULL;
}

uint32_t box_render_transitions_node(
    const box_render_transitions_t *s,
    ecs_entity_t entity)
{
    const ecs_map_val_t *found = s ? ecs_map_get(&s->by_entity, entity) : NULL;

    return found ? (uint32_t)*found : 0;
}

bool box_render_transitions_active(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component,
    double time)
{
    const box_transition_node_t *n = box_transition_node(s, node);
    const box_transition_slot_t *slot = n ? &n->slots[component] : NULL;

    return slot && slot->present &&
        box_render_transition_running(slot->start, slot->delay, slot->duration, time);
}

bool box_render_transitions_node_active(
    const box_render_transitions_t *s,
    uint32_t node,
    double time)
{
    bool result = false;
    int32_t c;
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS && !result; c ++) {
        result = box_render_transitions_active(
            s, node, (box_render_transition_component_t)c, time);
    }

    return result;
}

void box_render_transitions_init(
    ecs_world_t *world)
{
    box_render_transitions_t *s = ecs_os_calloc_t(box_render_transitions_t);
    ecs_map_init(&s->by_entity, NULL);
    ecs_vec_init_t(NULL, &s->nodes, box_transition_node_t, 0);
    ecs_vec_init_t(NULL, &s->free, uint32_t, 0);
    s->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderTransitions" }),
        .terms = {{
            .id = ecs_id(Transition),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA,
            .inout = EcsIn
        }},
        .flags = EcsQueryMatchPrefab,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractTransitions" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTransitions,
        .ctx = s,
        .ctx_free = box_transitions_free
    });
}
