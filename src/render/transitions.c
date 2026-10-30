/**
 * @file render/transitions.c
 * @brief Transition extract: finds the changes of transitioned components on the game thread
 * and sends them to the transition resolver as packet records.
 *
 * One cached query over the entities with Transition, owned or inherited over IsA, prefabs
 * included, compares each transitioned component to the last target it sent. Entities without
 * Transition are not visited, and a world that does not use Transition matches no table.
 *
 * Entities an active transform transition moves carry TransitionTransform (dynamic_transform);
 * a second system, after the batches were sent, sends the pose and slot records for them.
 */

#include "draw.h"

ECS_TAG_DECLARE(TransitionTransform);

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

/** An entity a transform transition moves in this frame, in the order of the walk: a node
 * before its descendants, so a parent node comes before its child nodes */
typedef struct box_transition_item_t {
    ecs_entity_t entity;            /**< The entity */
    uint32_t node;                  /**< The node that moves it, its own for a node */
    uint32_t parent;                /**< Parent node of a node, 0 for none or a member */
    int32_t up;                     /**< Item of its parent, -1 for a node without parent node */
    bool is_node;                   /**< The entity has an active transform transition */
} box_transition_item_t;

/** Main thread state of the transition extract */
struct box_render_transitions_t {
    ecs_query_t *query;             /**< Transition(self|up IsA), prefabs included */
    ecs_map_t by_entity;            /**< Entity to node id */
    ecs_vec_t nodes;                /**< box_transition_node_t by node id, 0 unused */
    ecs_vec_t free;                 /**< uint32_t node ids to reuse */
    int64_t stamp;                  /**< Extracts run */
    ecs_map_t moving;               /**< Entity to node id, nodes with an active transform
                                     *   transition in this frame */
    ecs_vec_t items;                /**< box_transition_item_t of this frame */
    ecs_vec_t stack;                /**< int32_t items of the walk still to visit */
    ecs_vec_t rels;                 /**< mat4 per item: the target chain from its node down to
                                     *   it, identity for a node */
    ecs_map_t tagged;               /**< Entity with TransitionTransform to the stamp of the
                                     *   last extract that kept it */
    ecs_vec_t tag_add;              /**< ecs_entity_t that get TransitionTransform */
    ecs_vec_t tag_remove;           /**< ecs_entity_t that lose TransitionTransform */
    ecs_vec_t shadow_scenes;        /**< ecs_entity_t, scenes with a moving shadow caster */
    ecs_vec_t shadow_last;          /**< ecs_entity_t, those of the last frame */
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

/** Check whether a node has an active transition of Position, Rotation or Scale.
 *
 * @param s The state of the extract
 * @param node The node id
 * @param time World time, the time of the packet
 * @return Whether one of its transform components is active
 */
static bool box_transition_moving(
    const box_render_transitions_t *s,
    uint32_t node,
    double time)
{
    return box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_POSITION, time) ||
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_ROTATION, time) ||
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_SCALE, time);
}

/** Check whether the world matrix of an entity is not derived from its parent's, which ends
 * the reach of a transform transition of an ancestor: ManualTransform (SurfaceAttachment), or
 * a WorldTransform without a Position.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it and its descendants render at their target
 */
static bool box_transition_stops(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has_id(world, e, ManualTransform) ||
        (!ecs_has(world, e, Position) && ecs_has(world, e, WorldTransform));
}

/** Check whether no ancestor of a moving entity moves it, through ChildOf and Parent.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity
 * @return Whether it has no parent node
 */
static bool box_transition_root(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t e)
{
    bool result = true;
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur && result && !box_transition_stops(world, cur)) {
        result = ecs_map_get(&s->moving, cur) == NULL;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Add an item to the walk.
 *
 * @param s The state of the extract
 * @param item The item
 */
static void box_transition_item_add(
    box_render_transitions_t *s,
    const box_transition_item_t *item)
{
    *ecs_vec_append_t(NULL, &s->items, box_transition_item_t) = *item;
    *ecs_vec_append_t(NULL, &s->stack, int32_t) = ecs_vec_count(&s->items) - 1;
}

/** Walk the subtree of a node without parent node, through ChildOf and Parent, adding its
 * descendants as items of the node that moves them.
 *
 * A descendant with an active transform transition is a node of its own whose parent is that
 * node. The walk skips prefabs and stops where a world matrix is not derived from the parent's.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity of the node
 * @param node Its node id
 */
static void box_transition_walk(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e,
    uint32_t node)
{
    int32_t c;
    box_transition_item_add(s, &(box_transition_item_t){
        .entity = e, .node = node, .up = -1, .is_node = true });
    while (ecs_vec_count(&s->stack)) {
        int32_t index = *ecs_vec_last_t(&s->stack, int32_t);
        box_transition_item_t item = *ecs_vec_get_t(&s->items, box_transition_item_t, index);
        ecs_vec_remove_last(&s->stack);
        ecs_iter_t it = ecs_children(world, item.entity);
        while (ecs_children_next(&it)) {
            for (c = 0; c < it.count; c ++) {
                ecs_entity_t child = it.entities[c];
                if (ecs_has_id(world, child, EcsPrefab) || box_transition_stops(world, child)) {
                    continue;
                }

                const ecs_map_val_t *own = ecs_map_get(&s->moving, child);
                box_transition_item_add(s, &(box_transition_item_t){
                    .entity = child,
                    .node = own ? (uint32_t)*own : item.node,
                    .parent = own ? item.node : 0,
                    .up = index,
                    .is_node = own != NULL
                });
            }
        }
    }
}

/** Give TransitionTransform to the drawn entities of the walk and take it from those that
 * left it, which rekeys their slots. Called outside of every iteration, as the tag moves the
 * entities to other tables.
 *
 * @param world The world
 * @param s The state of the extract
 */
static void box_transition_tag(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    int32_t i, count = ecs_vec_count(&s->items);
    ecs_vec_clear(&s->tag_add);
    ecs_vec_clear(&s->tag_remove);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&s->items, box_transition_item_t, i)->entity;
        if (!ecs_has(world, e, DrawSlot) && !ecs_has(world, e, TextSlots)) {
            continue;
        }

        ecs_map_val_t *stamp = ecs_map_ensure(&s->tagged, e);
        if (!*stamp) {
            *ecs_vec_append_t(NULL, &s->tag_add, ecs_entity_t) = e;
        }

        *stamp = (ecs_map_val_t)s->stamp;
    }

    ecs_map_iter_t mit = ecs_map_iter(&s->tagged);
    while (ecs_map_next(&mit)) {
        if (ecs_map_value(&mit) != (ecs_map_val_t)s->stamp) {
            *ecs_vec_append_t(NULL, &s->tag_remove, ecs_entity_t) =
                (ecs_entity_t)ecs_map_key(&mit);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->tag_remove); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&s->tag_remove, ecs_entity_t, i);
        ecs_map_remove(&s->tagged, e);
        if (ecs_is_alive(world, e)) {
            ecs_remove_id(world, e, TransitionTransform);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->tag_add); i ++) {
        ecs_add_id(world, *ecs_vec_get_t(&s->tag_add, ecs_entity_t, i), TransitionTransform);
    }
}

/** Find the entities the active transform transitions move in this frame, nodes first and
 * parent nodes before their children, and keep TransitionTransform on the drawn ones.
 *
 * Clearing a map allocates its buckets again, so a frame without moving nodes leaves the map
 * as it is and allocates nothing.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when no entity has Transition
 */
static void box_transition_affect(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&s->nodes);
    ecs_vec_clear(&s->items);
    if (ecs_map_count(&s->moving)) {
        ecs_map_clear(&s->moving);
    }

    if (!ecs_map_count(&s->by_entity) && !ecs_map_count(&s->tagged)) {
        goto done;
    }

    for (i = 1; p && i < count; i ++) {
        const box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, i);
        if (node->entity && box_transition_moving(s, (uint32_t)i, p->time) &&
            ecs_has(world, node->entity, Position) &&
            !ecs_has_id(world, node->entity, EcsPrefab) &&
            !ecs_has_id(world, node->entity, ManualTransform))
        {
            ecs_map_insert(&s->moving, node->entity, (ecs_map_val_t)i);
        }
    }

    for (i = 1; ecs_map_count(&s->moving) && i < count; i ++) {
        const box_transition_node_t *node = ecs_vec_get_t(&s->nodes, box_transition_node_t, i);
        if (node->entity && ecs_map_get(&s->moving, node->entity) &&
            box_transition_root(world, s, node->entity))
        {
            box_transition_walk(world, s, node->entity, (uint32_t)i);
        }
    }

    box_transition_tag(world, s);
done:
    return;
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

    box_transition_affect(world, s, ecs_map_count(&s->by_entity) ? p : NULL);
}

/** Get the local matrix of the target pose of an entity, identity without a Position.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the matrix
 */
static void box_transition_target_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const Position *position = ecs_get(world, e, Position);
    if (position) {
        box_transform_local(position, ecs_get(world, e, Rotation), ecs_get(world, e, Scale),
            out);
    } else {
        glm_mat4_identity(out);
    }
}

/** Copy a matrix into the packet data.
 *
 * @param p The packet
 * @param m The matrix
 * @return Offset of the copy in the packet data
 */
static uint32_t box_transition_matrix(
    box_render_packet_t *p,
    mat4 m)
{
    uint32_t result = 0;
    void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(mat4), &result);
    ecs_os_memcpy(dst, m, ECS_SIZEOF(mat4));

    return result;
}

/** Send the pose record of a node: the target chain from its parent node, or the target world
 * matrix of its parent without a parent node, and its target pose.
 *
 * @param world The world
 * @param p The packet
 * @param item The item of the node
 * @param rel The target chain from the parent node, NULL without a parent node
 */
static void box_transition_send_pose(
    const ecs_world_t *world,
    box_render_packet_t *p,
    const box_transition_item_t *item,
    vec4 *rel)
{
    box_render_transition_pose_t pose = {0};
    const Position *position = ecs_get(world, item->entity, Position);
    const Rotation *rotation = ecs_get(world, item->entity, Rotation);
    const Scale *scale = ecs_get(world, item->entity, Scale);
    uint32_t offset = 0;
    if (rel) {
        ecs_os_memcpy(pose.rel, rel, ECS_SIZEOF(mat4));
    } else {
        const WorldTransform *parent = NULL;
        ecs_entity_t cur = ecs_get_parent(world, item->entity);
        while (cur && !parent) {
            parent = ecs_get(world, cur, WorldTransform);
            cur = ecs_get_parent(world, cur);
        }

        if (parent) {
            ecs_os_memcpy(pose.rel, parent->m, ECS_SIZEOF(mat4));
        } else {
            ecs_os_memcpy(pose.rel, GLM_MAT4_IDENTITY, ECS_SIZEOF(mat4));
        }
    }

    if (position) {
        pose.position[0] = position->x;
        pose.position[1] = position->y;
        pose.position[2] = position->z;
    }

    if (rotation) {
        pose.rotation[0] = rotation->x;
        pose.rotation[1] = rotation->y;
        pose.rotation[2] = rotation->z;
    }

    pose.scale[0] = scale ? scale->x : 1.0f;
    pose.scale[1] = scale ? scale->y : 1.0f;
    pose.scale[2] = scale ? scale->z : 1.0f;
    void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(pose), &offset);
    ecs_os_memcpy(dst, &pose, ECS_SIZEOF(pose));

    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_POSE, item->node);
    r->parent = item->parent;
    r->data = offset;
}

/** Send the slot record of a transform a node moves, when its batch uploaded its run into this
 * packet; a moving shadow caster marks its scene.
 *
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param node The node id
 * @param batch Index of the batch
 * @param slot The slot
 * @param rel The target chain from the node down to the instance
 */
static void box_transition_send_slot(
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    uint32_t node,
    int32_t batch,
    int32_t slot,
    mat4 rel)
{
    box_draw_batch_t *b = batch >= 0 && batch < ecs_vec_count(&d->batches)
        ? box_draws_batch(d, batch) : NULL;
    int32_t i;
    bool found = false;
    if (!b || !b->alive || !b->key.dynamic_transform || b->transform_frame != p->frame ||
        slot < 0 || slot >= ecs_vec_count(&b->transforms))
    {
        goto done;
    }

    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_SLOT, node);
    r->data = box_transition_matrix(p, rel);
    r->offset = b->transform_data + (uint32_t)slot * (uint32_t)sizeof(box_render_transform_t);
    if (!d->shadow_record || !box_draws_caster(b)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&s->shadow_scenes) && !found; i ++) {
        found = *ecs_vec_get_t(&s->shadow_scenes, ecs_entity_t, i) == b->key.scene;
    }

    if (!found) {
        *ecs_vec_append_t(NULL, &s->shadow_scenes, ecs_entity_t) = b->key.scene;
    }
done:
    return;
}

/** Send the slot records of the shape and the glyphs of an entity a node moves: the target
 * chain from the node with the instance matrix of each, as box_draws_transform and the glyph
 * quads write them.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param item The item of the entity
 * @param rel The target chain from the node down to the entity
 */
static void box_transition_send_slots(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    const box_transition_item_t *item,
    vec4 *rel)
{
    const DrawSlot *ds = ecs_get(world, item->entity, DrawSlot);
    const ShapeMesh *shape = ecs_get(world, item->entity, ShapeMesh);
    const TextSlots *ts = ecs_get(world, item->entity, TextSlots);
    const TextGlyphs *tg = ts ? ecs_get(world, item->entity, TextGlyphs) : NULL;
    mat4 instance, m;
    int32_t i, count;
    if (!ecs_has(world, item->entity, WorldTransform)) {
        goto done;
    }

    if (ds && shape && ds->batch >= 0) {
        glm_mat4_identity(instance);
        instance[0][0] = shape->scale[0];
        instance[1][1] = shape->scale[1];
        instance[2][2] = shape->scale[2];
        glm_mul(rel, instance, m);
        box_transition_send_slot(s, d, p, item->node, ds->batch, ds->slot, m);
    }

    count = ts && tg ? ecs_vec_count(&ts->slots) : 0;
    if (tg && ecs_vec_count(&tg->glyphs) < count) {
        count = ecs_vec_count(&tg->glyphs);
    }

    for (i = 0; i < count; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        const box_text_glyph_t *g = ecs_vec_get_t(&tg->glyphs, box_text_glyph_t, i);
        glm_mat4_identity(instance);
        instance[0][0] = g->width;
        instance[1][1] = g->height;
        instance[3][0] = g->x;
        instance[3][1] = g->y;
        glm_mul(rel, instance, m);
        box_transition_send_slot(s, d, p, item->node, at->batch, at->slot, m);
    }
done:
    return;
}

/** Record a region over the whole of every scene with a moving shadow caster in this frame or
 * the last one, so the shadow atlas renders every tile of the scene.
 *
 * The game thread does not know where the resolver puts a caster, and the tiles it was in
 * during the last frame must render again when it stops.
 *
 * @param s The state of the extract
 * @param d The state of the batches
 */
static void box_transition_shadows(
    box_render_transitions_t *s,
    box_render_draws_t *d)
{
    int32_t l, i;
    ecs_vec_t *lists[2] = { &s->shadow_scenes, &s->shadow_last };
    for (l = 0; d->shadow_record && l < 2; l ++) {
        for (i = 0; i < ecs_vec_count(lists[l]); i ++) {
            box_render_shadow_region_t *out = ecs_vec_append_t(
                NULL, &d->shadow_regions, box_render_shadow_region_t);
            out->scene = *ecs_vec_get_t(lists[l], ecs_entity_t, i);
            glm_vec3_fill(out->min, -FLT_MAX);
            glm_vec3_fill(out->max, FLT_MAX);
        }
    }

    ecs_vec_t tmp = s->shadow_last;
    s->shadow_last = s->shadow_scenes;
    s->shadow_scenes = tmp;
    ecs_vec_clear(&s->shadow_scenes);
}

/** Send the pose of every node the walk of this frame found, parent nodes first, and a slot
 * record for every transform it moves that a batch uploaded into the packet, after the
 * batches were sent.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTransitionSlots(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    ecs_world_t *world = it->world;
    box_render_draws_t *d = box_draws_state(world);
    box_render_packet_t *p;
    int32_t i, count = ecs_vec_count(&s->items);
    if (!d || (!count && !ecs_vec_count(&s->shadow_last))) {
        goto done;
    }

    p = box_render_packet(world);
    ecs_vec_set_count_t(NULL, &s->rels, mat4, count);
    for (i = 0; i < count; i ++) {
        const box_transition_item_t *item = ecs_vec_get_t(
            &s->items, box_transition_item_t, i);
        vec4 *rel = *ecs_vec_get_t(&s->rels, mat4, i);
        vec4 *up = item->up >= 0 ? *ecs_vec_get_t(&s->rels, mat4, item->up) : NULL;
        if (!ecs_is_alive(world, item->entity)) {
            glm_mat4_identity(rel);
            continue;
        }

        if (item->is_node) {
            box_transition_send_pose(world, p, item, item->parent ? up : NULL);
            glm_mat4_identity(rel);
        } else {
            mat4 local;
            box_transition_target_local(world, item->entity, local);
            glm_mul(up, local, rel);
        }

        box_transition_send_slots(world, s, d, p, item, rel);
    }

    box_transition_shadows(s, d);
done:
    return;
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
    ecs_map_fini(&s->moving);
    ecs_map_fini(&s->tagged);
    ecs_vec_fini_t(NULL, &s->nodes, box_transition_node_t);
    ecs_vec_fini_t(NULL, &s->free, uint32_t);
    ecs_vec_fini_t(NULL, &s->items, box_transition_item_t);
    ecs_vec_fini_t(NULL, &s->stack, int32_t);
    ecs_vec_fini_t(NULL, &s->rels, mat4);
    ecs_vec_fini_t(NULL, &s->tag_add, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->tag_remove, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->shadow_scenes, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->shadow_last, ecs_entity_t);
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
    ECS_TAG_DEFINE(world, TransitionTransform);
    ecs_add_pair(world, TransitionTransform, EcsOnInstantiate, EcsDontInherit);
    ecs_map_init(&s->by_entity, NULL);
    ecs_map_init(&s->moving, NULL);
    ecs_map_init(&s->tagged, NULL);
    ecs_vec_init_t(NULL, &s->nodes, box_transition_node_t, 0);
    ecs_vec_init_t(NULL, &s->free, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->items, box_transition_item_t, 0);
    ecs_vec_init_t(NULL, &s->stack, int32_t, 0);
    ecs_vec_init_t(NULL, &s->rels, mat4, 0);
    ecs_vec_init_t(NULL, &s->tag_add, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->tag_remove, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->shadow_scenes, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->shadow_last, ecs_entity_t, 0);
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
        .ctx_free = box_transitions_free,
        .immediate = true
    });
}

void box_render_transitions_slots_init(
    ecs_world_t *world)
{
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_assert(s != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractTransitionSlots" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTransitionSlots,
        .ctx = s
    });
}
