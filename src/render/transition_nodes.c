/**
 * @file render/transition_nodes.c
 * @brief Nodes of the transition extract and the entities it visits: observers collect the
 * entities whose Transition, transitioned components or Disabled changed, owned or inherited
 * over IsA, and the extract visits those and the nodes whose transitions run or leave.
 *
 * The first extract visits every entity of the query, so entities made before the observers
 * existed get their nodes.
 */

#include "transitions.h"

ECS_COMPONENT_DECLARE(TransitionNode);

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

/** Give an entity a node, reusing a freed id first, send its creation and store its id in the
 * TransitionNode of the entity, which moves it to another table.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet
 * @param entity The entity
 * @return The node id
 */
static uint32_t box_transitions_node_new(
    ecs_world_t *world,
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
    s->node_count ++;
    box_render_packet_transition(p, BOX_RENDER_TRANSITION_CREATE, result);
    ecs_ensure(world, entity, TransitionNode)->id = result;

    return result;
}

/** Send the destruction of a node and free its id.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet
 * @param id The node id
 */
static void box_transitions_node_free(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p,
    uint32_t id)
{
    box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, (int32_t)id);
    box_render_packet_transition(p, BOX_RENDER_TRANSITION_DESTROY, id);
    box_transitions_lobes_left(world, s, node);
    s->node_count --;
    node->entity = 0;
    *ecs_vec_append_t(NULL, &s->free, uint32_t) = id;
}

/** Check whether a node has a slot that leaves.
 *
 * @param node The node
 * @return Whether one of its slots leaves
 */
static bool box_transitions_node_leaves(
    const box_transition_node_t *node)
{
    bool result = false;
    int32_t c;
    for (c = 0; (node->live >> c) && !result; c ++) {
        result = (node->live & (1u << c)) && node->slots[c].leaving;
    }

    return result;
}

/** Get the values of the transitioned components an entity owns: the components its
 * Transition map lists and those of the live slots of its node.
 *
 * @param world The world
 * @param e The entity
 * @param map The Transition map of the entity
 * @param live The live slots of its node, 0 for none
 * @param values Receives the value of each component, NULL when the entity owns none
 */
static void box_transitions_values(
    const ecs_world_t *world,
    ecs_entity_t e,
    const Transition *map,
    uint32_t live,
    void **values)
{
    box_render_transition_component_t component;
    uint32_t want = live;
    int32_t c;
    ecs_os_memset_n(values, 0, void*, BOX_RENDER_TRANSITION_COMPONENTS);
    if (ecs_map_is_init(map)) {
        ecs_map_iter_t it = ecs_map_iter(map);
        while (ecs_map_next(&it)) {
            if (box_render_transition_component(ecs_map_key(&it), &component)) {
                want |= 1u << component;
            }
        }
    }

    for (c = 0; want >> c; c ++) {
        if (want & (1u << c)) {
            values[c] = ecs_get_mut_id(world, e,
                box_transitions_id((box_render_transition_component_t)c));
        }
    }
}

/** Compare the transitioned components of an entity to the last targets its node sent, giving
 * it a node when it becomes one and freeing its node when it no longer is one.
 *
 * An entity is a node when it is enabled and owns Transition, or inherits it and owns a value
 * it lists or one of its entries animates in or out. A node that runs or leaves is visited by
 * the next extract too.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet
 * @param e The entity
 */
static void box_transitions_visit(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p,
    ecs_entity_t e)
{
    void *values[BOX_RENDER_TRANSITION_COMPONENTS];
    const TransitionSpec *specs[BOX_RENDER_TRANSITION_COMPONENTS];
    ecs_table_t *table = ecs_get_table(world, e);
    const TransitionNode *own = ecs_get(world, e, TransitionNode);
    const Transition *map = NULL;
    uint32_t id = own ? own->id : 0, mask = 0, visit;
    box_transition_node_t *node = box_transitions_node_state(s, id);
    bool is_node = false;
    int32_t c;
    if (node && node->entity != e) {
        node = NULL;
        id = 0;
    }

    if (node && node->seen == s->stamp) {
        goto done;
    }

    if (table && !ecs_table_has_flags(table, EcsTableIsDisabled)) {
        map = ecs_get(world, e, Transition);
    }

    if (map) {
        box_transitions_values(world, e, map, node ? node->live : 0, values);
        is_node = box_transitions_specs(world, map, values, specs, &mask) ||
            ecs_owns(world, e, Transition);
    }

    if (!is_node) {
        if (node) {
            box_transitions_node_free(world, s, p, id);
        }

        goto done;
    }

    if (!node) {
        id = box_transitions_node_new(world, s, p, e);
        node = ecs_vec_get_t(&s->nodes, box_transition_node_t, (int32_t)id);
        map = ecs_get(world, e, Transition);
        box_transitions_values(world, e, map, 0, values);
        box_transitions_specs(world, map, values, specs, &mask);
    }

    node->seen = s->stamp;
    node->listed = mask;
    visit = mask | node->live;
    for (c = 0; visit >> c; c ++) {
        box_render_transition_component_t component = (box_render_transition_component_t)c;
        if (visit & (1u << c)) {
            box_transitions_compare(world, s, p, e, id, component, &node->slots[c],
                specs[c], values[c]);
            box_transitions_track(node, component);
        }
    }

    if (box_transitions_runs(node, p->time)) {
        *ecs_vec_append_t(NULL, &s->running, int32_t) = (int32_t)id;
        *ecs_vec_append_t(NULL, &s->revisit, uint32_t) = id;
    } else if (box_transitions_node_leaves(node)) {
        *ecs_vec_append_t(NULL, &s->revisit, uint32_t) = id;
    }
done:
    return;
}

/** Add every entity of the query to the changed entities.
 *
 * @param world The world
 * @param s The state of the extract
 */
static void box_transitions_rescan(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    int32_t i;
    ecs_iter_t it = ecs_query_iter(world, s->query);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, &s->dirty, ecs_entity_t) = it.entities[i];
        }
    }

    s->rescan = false;
}

box_render_packet_t* box_transitions_changes(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    box_render_packet_t *p = NULL;
    ecs_vec_t last = s->revisit_last;
    int32_t i;
    s->stamp ++;
    s->leaving = 0;
    ecs_vec_clear(&s->running);
    s->revisit_last = s->revisit;
    s->revisit = last;
    ecs_vec_clear(&s->revisit);
    if (s->rescan) {
        box_transitions_rescan(world, s);
    }

    if (s->node_count || ecs_vec_count(&s->dirty)) {
        p = box_render_packet(world);
    }

    for (i = 0; i < ecs_vec_count(&s->gone); i ++) {
        uint32_t id = *ecs_vec_get_t(&s->gone, uint32_t, i);
        const box_transition_node_t *node = box_transitions_node_state(s, id);
        if (node && !ecs_is_alive(world, node->entity)) {
            box_transitions_node_free(world, s, p, id);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->dirty); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&s->dirty, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            box_transitions_visit(world, s, p, e);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->revisit_last); i ++) {
        uint32_t id = *ecs_vec_get_t(&s->revisit_last, uint32_t, i);
        const box_transition_node_t *node = box_transitions_node_state(s, id);
        if (node && !ecs_is_alive(world, node->entity)) {
            box_transitions_node_free(world, s, p, id);
        } else if (node) {
            box_transitions_visit(world, s, p, node->entity);
        }
    }

    ecs_vec_clear(&s->gone);
    ecs_vec_clear(&s->dirty);

    return p;
}

/** Add the entities of an event to the changed entities.
 *
 * @param it The iterator of the observer
 */
static void BoxTransitionsOnChange(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        *ecs_vec_append_t(NULL, &s->dirty, ecs_entity_t) = it->entities[i];
    }
}

/** Add the nodes whose entity a set of a transitioned component changes: the ones whose
 * Transition lists the component or whose slot of it is live. An entity without a node gets one
 * when it gets the component or Transition, which the observers of those events add.
 *
 * @param it The iterator of the observer
 */
static void BoxTransitionsOnSet(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    const TransitionNode *ids = ecs_table_get_id(
        it->world, it->table, ecs_id(TransitionNode), it->offset);
    box_render_transition_component_t component;
    int32_t i;
    if (!ids || !box_render_transition_component(it->event_id, &component)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        const box_transition_node_t *node = box_transitions_node_state(s, ids[i].id);
        if (node && node->entity == it->entities[i] &&
            ((node->listed | node->live) & (1u << component)))
        {
            *ecs_vec_append_t(NULL, &s->dirty, ecs_entity_t) = it->entities[i];
        }
    }
done:
    return;
}

/** Add the nodes of entities that lose TransitionNode, as deleted ones do, to the nodes the
 * next extract frees when their entity is gone.
 *
 * @param it The iterator of the observer
 */
static void BoxTransitionsOnNodeRemove(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    const TransitionNode *ids = ecs_field(it, TransitionNode, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (ids[i].id) {
            *ecs_vec_append_t(NULL, &s->gone, uint32_t) = ids[i].id;
        }
    }
}

/** Register an observer that holds a reference to the state of the extract.
 *
 * @param world The world
 * @param s The state of the extract
 * @param name Name of the observer
 * @param desc The observer, without entity, callback and context
 * @param callback The callback
 */
static void box_transitions_observer(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const char *name,
    ecs_observer_desc_t desc,
    ecs_iter_action_t callback)
{
    desc.entity = ecs_entity(world, { .name = name });
    desc.callback = callback;
    desc.ctx = s;
    desc.ctx_free = box_transitions_release;
    desc.query.flags |= EcsQueryMatchPrefab | EcsQueryMatchDisabled;
    s->refs ++;
    ecs_observer_init(world, &desc);
}

void box_transitions_observers(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    char name[128];
    int32_t c;
    box_transitions_observer(world, s, "TransitionsOnTransition", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Transition), .src.id = EcsSelf | EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove } }, BoxTransitionsOnChange);
    box_transitions_observer(world, s, "TransitionsOnDisabled", (ecs_observer_desc_t){
        .query.terms = {{ .id = EcsDisabled }, {
            .id = ecs_id(Transition), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
            .inout = EcsInOutFilter
        }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxTransitionsOnChange);
    box_transitions_observer(world, s, "TransitionsOnNodeRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TransitionNode) }},
        .events = { EcsOnRemove } }, BoxTransitionsOnNodeRemove);
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
        ecs_entity_t id = box_transitions_id((box_render_transition_component_t)c);
        bool inherit = ecs_has_pair(world, id, EcsOnInstantiate, EcsInherit);
        ecs_os_snprintf(name, ECS_SIZEOF(name), "TransitionsOnSet%s", ecs_get_name(world, id));
        box_transitions_observer(world, s, name, (ecs_observer_desc_t){
            .query.terms = {{ .id = id }},
            .events = { EcsOnSet } }, BoxTransitionsOnSet);
        ecs_os_snprintf(name, ECS_SIZEOF(name), "TransitionsOnAdd%s", ecs_get_name(world, id));
        box_transitions_observer(world, s, name, (ecs_observer_desc_t){
            .query.terms = {{
                .id = id,
                .src.id = inherit ? EcsSelf | EcsUp : EcsSelf,
                .trav = inherit ? EcsIsA : 0
            }, {
                .id = ecs_id(Transition), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
                .inout = EcsInOutFilter
            }},
            .events = { EcsOnAdd, EcsOnRemove } }, BoxTransitionsOnChange);
    }
}
