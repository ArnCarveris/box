/**
 * @file render/transitions.c
 * @brief Transition extract: finds the changes of transitioned components on the game thread
 * and sends them to the transition resolver as packet records.
 *
 * The entities whose Transition or transitioned components changed and the nodes whose
 * transitions run or leave compare each transitioned component to the last target sent
 * (transition_nodes.c), so an entity that did not change is not visited.
 *
 * Shapes a transform transition moves carry TransitionTransform, shapes whose shading one
 * patches TransitionMaterial, shapes under an Opacity transition TransitionOpacity and shapes
 * whose BoxShadow transitions TransitionBoxShadow; a later system sends what patches them.
 */

#include "transitions.h"
#include <string.h>

ECS_TAG_DECLARE(TransitionTransform);
ECS_TAG_DECLARE(TransitionMaterial);
ECS_TAG_DECLARE(TransitionOpacity);
ECS_TAG_DECLARE(TransitionBoxShadow);
ECS_COMPONENT_DECLARE(RenderTransitionState);

/** Material components whose transitions patch the material table and the 2D shading */
static const box_render_transition_component_t box_transition_material_components[] = {
    BOX_RENDER_TRANSITION_RGB,
    BOX_RENDER_TRANSITION_ALPHA_CUTOFF,
    BOX_RENDER_TRANSITION_PBR_MATERIAL,
    BOX_RENDER_TRANSITION_EMISSIVE,
    BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM,
    BOX_RENDER_TRANSITION_TRANSMISSION,
    BOX_RENDER_TRANSITION_CLEAR_COAT,
    BOX_RENDER_TRANSITION_ANISOTROPY,
    BOX_RENDER_TRANSITION_SHEEN,
    BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION,
    BOX_RENDER_TRANSITION_FRESNEL,
    BOX_RENDER_TRANSITION_DISTORTION
};

/** Number of material components */
#define BOX_TRANSITION_MATERIALS \
    ((int32_t)(sizeof(box_transition_material_components) / \
        sizeof(box_transition_material_components[0])))

/** Components a slot of the material table reads */
#define BOX_TRANSITION_MASK_TABLE \
    ((1u << BOX_RENDER_TRANSITION_RGB) | (1u << BOX_RENDER_TRANSITION_ALPHA_CUTOFF) | \
     (1u << BOX_RENDER_TRANSITION_PBR_MATERIAL) | (1u << BOX_RENDER_TRANSITION_EMISSIVE) | \
     (1u << BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM) | \
     (1u << BOX_RENDER_TRANSITION_TRANSMISSION) | (1u << BOX_RENDER_TRANSITION_CLEAR_COAT) | \
     (1u << BOX_RENDER_TRANSITION_ANISOTROPY) | (1u << BOX_RENDER_TRANSITION_SHEEN) | \
     (1u << BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION) | \
     (1u << BOX_RENDER_TRANSITION_FRESNEL) | (1u << BOX_RENDER_TRANSITION_DISTORTION))

/** Components the shading of a 2D shape reads: a 2D shape has no alpha test, transmission,
 * coat, anisotropy, sheen, diffuse transmission nor fresnel and its material words hold its
 * shape and its Border, which a Line does not draw */
#define BOX_TRANSITION_MASK_SHAPE \
    ((1u << BOX_RENDER_TRANSITION_RGB) | (1u << BOX_RENDER_TRANSITION_EMISSIVE) | \
     (1u << BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM) | (1u << BOX_RENDER_TRANSITION_BORDER))

/** Components the shading of a glyph reads */
#define BOX_TRANSITION_MASK_GLYPH \
    ((1u << BOX_RENDER_TRANSITION_RGB) | (1u << BOX_RENDER_TRANSITION_EMISSIVE))

/** Components whose change an alpha tested shadow caster reads through the material table */
#define BOX_TRANSITION_MASK_SHADOW \
    ((1u << BOX_RENDER_TRANSITION_ALPHA_CUTOFF) | \
     (1u << BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM))


ecs_entity_t box_transitions_id(
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
    case BOX_RENDER_TRANSITION_TRANSMISSION: result = ecs_id(Transmission); break;
    case BOX_RENDER_TRANSITION_CLEAR_COAT: result = ecs_id(ClearCoat); break;
    case BOX_RENDER_TRANSITION_ANISOTROPY: result = ecs_id(SpecularAnisotropy); break;
    case BOX_RENDER_TRANSITION_SHEEN: result = ecs_id(Sheen); break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION: result = ecs_id(DiffuseTransmission); break;
    case BOX_RENDER_TRANSITION_FRESNEL: result = ecs_id(Fresnel); break;
    case BOX_RENDER_TRANSITION_DISTORTION: result = ecs_id(Distortion); break;
    case BOX_RENDER_TRANSITION_BOX_SHADOW: result = ecs_id(BoxShadow); break;
    case BOX_RENDER_TRANSITION_BORDER: result = ecs_id(Border); break;
    case BOX_RENDER_TRANSITION_POINT_LIGHT: result = ecs_id(PointLight); break;
    case BOX_RENDER_TRANSITION_SPOT_LIGHT: result = ecs_id(SpotLight); break;
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
        if (component && box_transitions_id((box_render_transition_component_t)c) == component) {
            *out = (box_render_transition_component_t)c;
            result = true;
        }
    }

    return result;
}

box_transition_node_t* box_transitions_node_state(
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
    const box_transition_node_t *n = box_transitions_node_state(s, node);

    return n && n->live && time < n->until && (
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_POSITION, time) ||
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_ROTATION, time) ||
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_SCALE, time));
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

/** Check whether no ancestor of a node of a walk is a node of it, through ChildOf and Parent.
 *
 * @param world The world
 * @param nodes Entity to node id, the nodes of the walk
 * @param e The entity
 * @param stops Whether the walk stops where a world matrix is not derived from the parent's
 * @return Whether it has no parent node
 */
static bool box_transition_root(
    const ecs_world_t *world,
    const ecs_map_t *nodes,
    ecs_entity_t e,
    bool stops)
{
    bool result = true;
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur && result && !(stops && box_transition_stops(world, cur))) {
        result = ecs_map_get(nodes, cur) == NULL;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Add an item to a walk.
 *
 * @param s The state of the extract
 * @param items The items of the walk
 * @param item The item
 */
static void box_transition_item_add(
    box_render_transitions_t *s,
    ecs_vec_t *items,
    const box_transition_item_t *item)
{
    *ecs_vec_append_t(NULL, items, box_transition_item_t) = *item;
    *ecs_vec_append_t(NULL, &s->stack, int32_t) = ecs_vec_count(items) - 1;
}

/** Walk the subtree of a node without parent node, through ChildOf and Parent, adding its
 * descendants as items of the node that affects them.
 *
 * A descendant that is a node of the walk is a node of its own whose parent is that node. The
 * walk skips prefabs, and the walk of transforms stops where a world matrix is not derived from
 * the parent's.
 *
 * @param world The world
 * @param s The state of the extract
 * @param nodes Entity to node id, the nodes of the walk
 * @param items Receives the items
 * @param e The entity of the node
 * @param node Its node id
 * @param stops Whether the walk stops where a world matrix is not derived from the parent's
 */
static void box_transition_walk(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    const ecs_map_t *nodes,
    ecs_vec_t *items,
    ecs_entity_t e,
    uint32_t node,
    bool stops)
{
    int32_t c;
    box_transition_item_add(s, items, &(box_transition_item_t){
        .entity = e, .node = node, .up = -1, .is_node = true });
    while (ecs_vec_count(&s->stack)) {
        int32_t index = *ecs_vec_last_t(&s->stack, int32_t);
        box_transition_item_t item = *ecs_vec_get_t(items, box_transition_item_t, index);
        ecs_vec_remove_last(&s->stack);
        ecs_iter_t it = ecs_children(world, item.entity);
        while (ecs_children_next(&it)) {
            for (c = 0; c < it.count; c ++) {
                ecs_entity_t child = it.entities[c];
                if (ecs_has_id(world, child, EcsPrefab) ||
                    (stops && box_transition_stops(world, child)))
                {
                    continue;
                }

                const ecs_map_val_t *own = ecs_map_get(nodes, child);
                box_transition_item_add(s, items, &(box_transition_item_t){
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

void box_transitions_keep(
    box_render_transitions_t *s,
    ecs_map_t *tagged,
    ecs_entity_t e)
{
    ecs_map_val_t *stamp = ecs_map_ensure(tagged, e);
    if (!*stamp) {
        *ecs_vec_append_t(NULL, &s->tag_add, ecs_entity_t) = e;
    }

    *stamp = (ecs_map_val_t)s->stamp;
}

void box_transitions_retag(
    ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_map_t *tagged,
    ecs_entity_t tag)
{
    int32_t i;
    ecs_map_iter_t mit = ecs_map_iter(tagged);
    while (ecs_map_next(&mit)) {
        if (ecs_map_value(&mit) != (ecs_map_val_t)s->stamp) {
            *ecs_vec_append_t(NULL, &s->tag_remove, ecs_entity_t) =
                (ecs_entity_t)ecs_map_key(&mit);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->tag_remove); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&s->tag_remove, ecs_entity_t, i);
        ecs_map_remove(tagged, e);
        if (ecs_is_alive(world, e)) {
            ecs_remove_id(world, e, tag);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->tag_add); i ++) {
        ecs_add_id(world, *ecs_vec_get_t(&s->tag_add, ecs_entity_t, i), tag);
    }

    ecs_vec_clear(&s->tag_add);
    ecs_vec_clear(&s->tag_remove);
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

        box_transitions_keep(s, &s->tagged, e);
    }

    box_transitions_retag(world, s, &s->tagged, TransitionTransform);
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
    int32_t r, i, count = ecs_vec_count(&s->running);
    ecs_vec_clear(&s->items);
    if (ecs_map_count(&s->moving)) {
        ecs_map_clear(&s->moving);
    }

    if (!s->node_count && !ecs_map_count(&s->tagged)) {
        goto done;
    }

    for (r = 0; p && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && box_transition_moving(s, (uint32_t)i, p->time) &&
            ecs_has(world, node->entity, Position) &&
            !ecs_has_id(world, node->entity, EcsPrefab) &&
            !ecs_has_id(world, node->entity, ManualTransform))
        {
            ecs_map_insert(&s->moving, node->entity, (ecs_map_val_t)i);
        }
    }

    for (r = 0; ecs_map_count(&s->moving) && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && ecs_map_get(&s->moving, node->entity) &&
            box_transition_root(world, &s->moving, node->entity, true))
        {
            box_transition_walk(world, s, &s->moving, &s->items, node->entity, (uint32_t)i,
                true);
        }
    }

    box_transition_tag(world, s);
done:
    return;
}

/** Check whether a node leaves a component (box_transition_slot_t leaving).
 *
 * @param s The state of the extract
 * @param node The node id
 * @param component The component
 * @return Whether the slot of the component leaves
 */
static bool box_transition_leaving(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);

    return n && (n->live & (1u << component)) && n->slots[component].present &&
        n->slots[component].leaving;
}

/** Find the node that leaves a component an entity no longer has, owned or inherited: the
 * node of the entity itself, or of the nearest IsA base that left it.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity
 * @param component The component
 * @param depth Levels of IsA walked, which bounds the walk
 * @return The node, 0 for none
 */
static uint32_t box_transition_leaving_node(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t e,
    box_render_transition_component_t component,
    int32_t depth)
{
    uint32_t result = box_render_transitions_node(s, e);
    ecs_entity_t base;
    int32_t i;
    if (result && box_transition_leaving(s, result, component)) {
        goto done;
    }

    result = 0;
    for (i = 0; !result && depth < 32 && (base = ecs_get_target(world, e, EcsIsA, i)); i ++) {
        result = box_transition_leaving_node(world, s, base, component, depth + 1);
    }
done:
    return result;
}

uint32_t box_transitions_owner(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t e,
    box_render_transition_component_t component)
{
    ecs_entity_t owner = ecs_get_target_for_id(world, e, EcsIsA, box_transitions_id(component));
    uint32_t result = owner ? box_render_transitions_node(s, owner) : 0;
    if (!owner && s->leaving) {
        result = box_transition_leaving_node(world, s, e, component, 0);
    }

    return result;
}

/** Find the nodes whose running transitions a record reads: for each material component of
 * a mask, the node the value comes from (box_transitions_owner) when its transition runs.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity the record is encoded from
 * @param mask 1 << box_render_transition_component_t per component the record reads
 * @param time World time, the time of the packet
 * @param out Receives the nodes, 0 for a component at its target
 * @return 1 << component per component with a node, 0 for none
 */
static uint32_t box_transition_material_nodes(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t e,
    uint32_t mask,
    double time,
    uint32_t *out)
{
    uint32_t result = 0;
    int32_t i;
    ecs_os_memset(out, 0, ECS_SIZEOF(uint32_t) * BOX_RENDER_TRANSITION_COMPONENTS);
    for (i = 0; i < BOX_TRANSITION_MATERIALS; i ++) {
        box_render_transition_component_t c = box_transition_material_components[i];
        if (!(mask & (1u << c))) {
            continue;
        }

        uint32_t node = box_transitions_owner(world, s, e, c);
        if (node && box_render_transitions_active(s, node, c, time)) {
            out[c] = node;
            result |= 1u << c;
        }
    }

    if ((mask & (1u << BOX_RENDER_TRANSITION_BORDER)) && ecs_owns(world, e, Border)) {
        uint32_t node = box_render_transitions_node(s, e);
        if (node && box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_BORDER, time)) {
            out[BOX_RENDER_TRANSITION_BORDER] = node;
            result |= 1u << BOX_RENDER_TRANSITION_BORDER;
        }
    }

    return result;
}

/** Check whether a node has an active transition of a material component.
 *
 * @param s The state of the extract
 * @param node The node id
 * @param time World time, the time of the packet
 * @return Whether one of its material components is active
 */
static bool box_transition_material_active(
    const box_render_transitions_t *s,
    uint32_t node,
    double time)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);
    uint32_t live = n && time < n->until ? n->live : 0;
    bool result = false;
    int32_t i;
    for (i = 0; live && i < BOX_TRANSITION_MATERIALS && !result; i ++) {
        result = (live & (1u << box_transition_material_components[i])) &&
            box_render_transitions_active(s, node, box_transition_material_components[i], time);
    }

    return result;
}

/** Get the components the shading of a drawn 2D entity reads, 0 for an entity without 2D
 * shading: its 2D shape and the glyphs of its Text.
 *
 * @param world The world
 * @param e The entity
 * @return 1 << box_render_transition_component_t per component
 */
static uint32_t box_transition_shading_mask(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    uint32_t result = 0;
    if (ecs_has_id(world, e, EcsPrefab)) {
        goto done;
    }

    const Shape2D *shape = ecs_has(world, e, DrawSlot) ? ecs_get(world, e, Shape2D) : NULL;
    if (shape) {
        result |= BOX_TRANSITION_MASK_SHAPE;
        if (shape->kind == BOX_SHAPE2D_LINE) {
            result &= ~(1u << BOX_RENDER_TRANSITION_BORDER);
        }
    }

    if (ecs_has(world, e, TextSlots)) {
        result |= BOX_TRANSITION_MASK_GLYPH;
    }
done:
    return result;
}

/** Check whether the entities of a table of IsA instances own a slot of the material table
 * (MaterialId) or have the shading of a 2D shape or text. A mesh shape that inherits its
 * material reads the slot of its base and has nothing to patch.
 *
 * @param world The world
 * @param table The table
 * @return Whether its entities are visited
 */
static bool box_transition_material_reads(
    const ecs_world_t *world,
    const ecs_table_t *table)
{
    return ecs_table_has_id(world, table, ecs_id(MaterialId)) ||
        ecs_search_relation(world, table, 0, ecs_id(TextSlots), EcsIsA, EcsSelf | EcsUp,
            NULL, NULL, NULL) != -1 ||
        (ecs_search_relation(world, table, 0, ecs_id(DrawSlot), EcsIsA, EcsSelf | EcsUp,
            NULL, NULL, NULL) != -1 &&
         ecs_search_relation(world, table, 0, ecs_id(Shape2D), EcsIsA, EcsSelf | EcsUp,
            NULL, NULL, NULL) != -1);
}

/** Check whether an entity is a mesh shape that owns a MaterialId the extract of the batches
 * has not given a slot yet, as one added with a material component in this frame (With). The
 * walk takes the slot first, so an added component patches its slot from the first frame.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it takes its slot in the walk
 */
static bool box_transition_material_claims(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_owns(world, e, MaterialId) && ecs_has(world, e, DrawSlot) &&
        ecs_has(world, e, ShapeMesh) && !ecs_has(world, e, Shape2D) &&
        !ecs_has_id(world, e, EcsPrefab);
}

/** Add what reads a material value of an entity: the entity and its IsA instances, down
 * every level. The ones that own a slot of the material table patch it, made private to them
 * for this frame (box_draws_material_private); the drawn 2D entities patch their shading.
 *
 * A slot reads its value from its owner or an IsA base of it, and Transition is inherited over
 * IsA, so this reaches every affected slot and visits only entities with Transition.
 *
 * The instances are checked per table: a table whose entities own no slot and have no 2D or
 * text shading is skipped, but for its entities that are IsA bases themselves, so one prefab
 * with many mesh instances that inherit its material visits the prefab alone.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The draws, with the material table
 * @param e The entity of a node with an active material transition
 * @param time World time, the time of the packet
 */
static void box_transition_material_walk(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    ecs_entity_t e,
    double time)
{
    int32_t i;
    ecs_vec_clear(&s->instances);
    *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = e;
    while (ecs_vec_count(&s->instances)) {
        ecs_entity_t cur = *ecs_vec_last_t(&s->instances, ecs_entity_t);
        ecs_vec_remove_last(&s->instances);
        if (ecs_map_get(&s->visited, cur)) {
            continue;
        }

        ecs_map_insert(&s->visited, cur, 1);
        if (!box_draws_material_owns(d, cur) && box_transition_material_claims(world, cur)) {
            box_draws_material_id(world, d, cur, true, false);
        }

        if (box_draws_material_owns(d, cur)) {
            box_transition_material_t owned = { .entity = cur };
            uint32_t found = box_transition_material_nodes(world, s, cur,
                BOX_TRANSITION_MASK_TABLE, time, owned.nodes);
            if (found) {
                owned.slot = box_draws_material_private(world, d, cur);
                owned.shadow = (found & BOX_TRANSITION_MASK_SHADOW) != 0;
                *ecs_vec_append_t(NULL, &s->material_slots, box_transition_material_t) = owned;
            }
        }

        uint32_t mask = box_transition_shading_mask(world, cur);
        box_transition_material_t item = { .entity = cur };
        if (mask && box_transition_material_nodes(world, s, cur, mask, time, item.nodes)) {
            *ecs_vec_append_t(NULL, &s->shadings, box_transition_material_t) = item;
            box_transitions_keep(s, &s->tagged_material, cur);
        }

        ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, cur));
        while (ecs_each_next(&it)) {
            bool reads = box_transition_material_reads(world, it.table);
            if (!reads && !ecs_table_has_flags(it.table, EcsTableHasTraversable)) {
                continue;
            }

            for (i = 0; i < it.count; i ++) {
                ecs_entity_t inst = it.entities[i];
                if (reads || ecs_id_in_use(world, ecs_pair(EcsIsA, inst))) {
                    *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = inst;
                }
            }
        }
    }
}

/** Add the drawn 2D shape of a node with an active Border transition that no walk of a
 * material transition visited.
 *
 * Border is copied into an IsA instance (OnInstantiate, Override), so a shape draws its own
 * value and no change of a prefab reaches its instances: the entity alone reads it.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity of the node
 * @param time World time, the time of the packet
 */
static void box_transition_border_add(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e,
    double time)
{
    uint32_t mask = box_transition_shading_mask(world, e);
    box_transition_material_t item = { .entity = e };
    if (ecs_map_get(&s->visited, e)) {
        goto done;
    }

    ecs_map_insert(&s->visited, e, 1);
    if ((mask & (1u << BOX_RENDER_TRANSITION_BORDER)) &&
        box_transition_material_nodes(world, s, e, mask, time, item.nodes))
    {
        *ecs_vec_append_t(NULL, &s->shadings, box_transition_material_t) = item;
        box_transitions_keep(s, &s->tagged_material, e);
    }
done:
    return;
}

/** Check whether a node has an active transition of Tint or Opacity.
 *
 * @param s The state of the extract
 * @param node The node id
 * @param time World time, the time of the packet
 * @return Whether its Tint or its Opacity is active
 */
static bool box_transition_fading(
    const box_render_transitions_t *s,
    uint32_t node,
    double time)
{
    return box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_TINT, time) ||
        box_render_transitions_active(s, node, BOX_RENDER_TRANSITION_OPACITY, time);
}

/** Check whether an entity of a walk has a slot to patch: a shape or the glyphs of a Text.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is drawn
 */
static bool box_transition_drawn(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, DrawSlot) || ecs_has(world, e, TextSlots);
}

/** Find the entities the active Tint and Opacity transitions reach in this frame, nodes first
 * and parent nodes before their children, before the batches are sent.
 *
 * The walk goes from every node with an active Tint or Opacity transition and no such node
 * above it down its subtree. Drawn entities keep TransitionMaterial, and the 2D shapes and texts
 * no material transition listed join the shadings to patch.
 *
 * Clearing a map allocates its buckets again, so a frame without fading nodes leaves the maps
 * as they are and allocates nothing.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when no entity has Transition
 */
static void box_transition_fades(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p)
{
    int32_t r, i, count = ecs_vec_count(&s->running);
    ecs_vec_clear(&s->fade_items);
    if (ecs_map_count(&s->fading)) {
        ecs_map_clear(&s->fading);
    }

    if (ecs_map_count(&s->fade_index)) {
        ecs_map_clear(&s->fade_index);
    }

    for (r = 0; p && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && box_transition_fading(s, (uint32_t)i, p->time) &&
            ecs_is_alive(world, node->entity) &&
            !ecs_has_id(world, node->entity, EcsPrefab))
        {
            ecs_map_insert(&s->fading, node->entity, (ecs_map_val_t)i);
        }
    }

    for (r = 0; ecs_map_count(&s->fading) && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && ecs_map_get(&s->fading, node->entity) &&
            box_transition_root(world, &s->fading, node->entity, false))
        {
            box_transition_walk(world, s, &s->fading, &s->fade_items, node->entity,
                (uint32_t)i, false);
        }
    }

    count = ecs_vec_count(&s->fade_items);
    for (i = 0; i < count; i ++) {
        box_transition_item_t *item = ecs_vec_get_t(&s->fade_items, box_transition_item_t, i);
        const box_transition_item_t *up = item->up >= 0
            ? ecs_vec_get_t(&s->fade_items, box_transition_item_t, item->up) : NULL;
        ecs_entity_t e = item->entity;
        item->opacity = (up && up->opacity) || (item->is_node &&
            box_render_transitions_active(s, item->node, BOX_RENDER_TRANSITION_OPACITY,
                p->time));
        ecs_map_insert(&s->fade_index, e, (ecs_map_val_t)(i + 1));
        if (!box_transition_drawn(world, e)) {
            continue;
        }

        const ecs_map_val_t *stamp = ecs_map_get(&s->tagged_material, e);
        bool listed = stamp && *stamp == (ecs_map_val_t)s->stamp;
        uint32_t mask = box_transition_shading_mask(world, e);
        box_transitions_keep(s, &s->tagged_material, e);
        if (!listed && mask) {
            box_transition_material_t shading = { .entity = e };
            box_transition_material_nodes(world, s, e, mask, p->time, shading.nodes);
            *ecs_vec_append_t(NULL, &s->shadings, box_transition_material_t) = shading;
        }
    }
}

/** Give TransitionOpacity to the drawn entities an active Opacity transition reaches in this
 * frame and take it from those it no longer reaches, which rekeys their slots: blended while it
 * runs, the batch of their target after.
 *
 * @param world The world
 * @param s The state of the extract
 */
static void box_transition_fade_tag(
    ecs_world_t *world,
    box_render_transitions_t *s)
{
    int32_t i, count = ecs_vec_count(&s->fade_items);
    ecs_vec_clear(&s->tag_add);
    ecs_vec_clear(&s->tag_remove);
    for (i = 0; i < count; i ++) {
        const box_transition_item_t *item = ecs_vec_get_t(
            &s->fade_items, box_transition_item_t, i);
        if (item->opacity && box_transition_drawn(world, item->entity)) {
            box_transitions_keep(s, &s->tagged_opacity, item->entity);
        }
    }

    if (ecs_map_count(&s->tagged_opacity)) {
        box_transitions_retag(world, s, &s->tagged_opacity, TransitionOpacity);
    }
}

/** Find what the active material transitions patch in this frame, before the batches and the
 * material table are sent: the slots of the material table, which upload in this frame, and
 * the drawn 2D entities, which keep TransitionMaterial.
 *
 * The slots of the last frame upload again, so the frame after a transition ended has its
 * target; an owner the walk no longer lists returns to a shared slot in the extract of the
 * batches.
 *
 * A frame without an active material transition visits no entity, and a frame with one visits
 * only the entities of its nodes and their IsA instances.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when no entity has Transition
 */
static void box_transition_materials(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p)
{
    int32_t r, i, count = ecs_vec_count(&s->running);
    bool any = false;
    box_render_draws_t *d = NULL;
    ecs_vec_clear(&s->shadings);
    ecs_vec_clear(&s->tag_add);
    ecs_vec_clear(&s->tag_remove);
    if (ecs_vec_count(&s->material_slots)) {
        d = s->draws;
    }

    for (i = 0; d && i < ecs_vec_count(&s->material_slots); i ++) {
        const box_transition_material_t *item = ecs_vec_get_t(
            &s->material_slots, box_transition_material_t, i);
        box_draws_material_changed(d, item->entity);
        if (item->shadow && d->shadow_record) {
            *ecs_vec_append_t(NULL, &d->shadow_materials, uint32_t) = item->slot;
        }
    }

    ecs_vec_clear(&s->material_slots);
    for (r = 0; p && r < count && !any; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        any = node->entity && (box_transition_material_active(s, (uint32_t)i, p->time) ||
            box_render_transitions_active(s, (uint32_t)i, BOX_RENDER_TRANSITION_BORDER, p->time));
    }

    if (ecs_map_count(&s->visited)) {
        ecs_map_clear(&s->visited);
    }

    d = any ? s->draws : NULL;
    for (r = 0; d && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && ecs_is_alive(world, node->entity) &&
            box_transition_material_active(s, (uint32_t)i, p->time))
        {
            box_transition_material_walk(world, s, d, node->entity, p->time);
        }
    }

    for (r = 0; d && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (node->entity && box_render_transitions_active(
                s, (uint32_t)i, BOX_RENDER_TRANSITION_BORDER, p->time) &&
            ecs_is_alive(world, node->entity))
        {
            box_transition_border_add(world, s, node->entity, p->time);
        }
    }

    for (i = 0; d && i < ecs_vec_count(&s->material_slots); i ++) {
        const box_transition_material_t *item = ecs_vec_get_t(
            &s->material_slots, box_transition_material_t, i);
        box_draws_material_changed(d, item->entity);
        if (item->shadow && d->shadow_record) {
            *ecs_vec_append_t(NULL, &d->shadow_materials, uint32_t) = item->slot;
        }
    }

    box_transition_fades(world, s, p);
    if (ecs_map_count(&s->tagged_material)) {
        box_transitions_retag(world, s, &s->tagged_material, TransitionMaterial);
    }

    box_transition_fade_tag(world, s);
}

/** Find the changes of the transitioned components of the entities that changed since the
 * last extract and of the nodes whose transitions run or leave, then walk what the active
 * transitions reach.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTransitions(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_transitions_changes(world, s);
    box_transition_affect(world, s, s->node_count ? p : NULL);
    box_transition_materials(world, s, s->node_count ? p : NULL);
    box_transitions_box_shadows(world, s, s->node_count ? p : NULL);
    box_transitions_lights(world, s, s->node_count ? p : NULL);
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
 * With a range, get the world matrices it can render with: those of its parent node times the
 * chain and its local matrices, from the targets range of each component that runs.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet
 * @param item The item of the node
 * @param rel The target chain from the parent node, or the world matrix of the parent
 * @param offset Subtracted from the position, the render origin for a root, NULL for none
 * @param up The range of the parent node, NULL without a parent node
 * @param range Receives the range of the node, NULL to skip it
 */
static void box_transition_send_pose(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_packet_t *p,
    const box_transition_item_t *item,
    vec4 *rel,
    const double *offset,
    const box_draw_transition_range_t *up,
    box_draw_transition_range_t *range)
{
    box_render_transition_pose_t pose = {0};
    const Position *position = ecs_get(world, item->entity, Position);
    const Rotation *rotation = ecs_get(world, item->entity, Rotation);
    const Scale *scale = ecs_get(world, item->entity, Scale);
    const box_transition_node_t *node = box_transitions_node_state(s, item->node);
    vec3 local_position = { 0, 0, 0 };
    float *targets[3] = { local_position, pose.rotation, pose.scale };
    vec3 lo[3], hi[3];
    uint32_t data = 0;
    int32_t c;
    ecs_os_memcpy(pose.rel, rel, ECS_SIZEOF(mat4));
    if (offset) {
        box_dvec3_copy(offset, pose.offset);
    }

    if (position) {
        pose.position[0] = position->x;
        pose.position[1] = position->y;
        pose.position[2] = position->z;
        box_dvec3_rel(pose.position, pose.offset, local_position);
    }

    if (rotation) {
        pose.rotation[0] = rotation->x;
        pose.rotation[1] = rotation->y;
        pose.rotation[2] = rotation->z;
    }

    pose.scale[0] = scale ? scale->x : 1.0f;
    pose.scale[1] = scale ? scale->y : 1.0f;
    pose.scale[2] = scale ? scale->z : 1.0f;
    void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(pose), &data);
    ecs_os_memcpy(dst, &pose, ECS_SIZEOF(pose));

    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_POSE, item->node);
    r->parent = item->parent;
    r->data = data;
    for (c = 0; range && c < 3; c ++) {
        bool runs = node && box_render_transitions_active(
            s, item->node, (box_render_transition_component_t)c, p->time);
        glm_vec3_copy(runs ? ECS_CONST_CAST(float*, node->slots[c].lo) : targets[c], lo[c]);
        glm_vec3_copy(runs ? ECS_CONST_CAST(float*, node->slots[c].hi) : targets[c], hi[c]);
        if (runs && c == BOX_RENDER_TRANSITION_POSITION) {
            int32_t k;
            for (k = 0; k < 3; k ++) {
                lo[c][k] = (float)((double)lo[c][k] - pose.offset[k]);
                hi[c][k] = (float)((double)hi[c][k] - pose.offset[k]);
            }
        }
    }

    if (range) {
        box_draws_range_pose(rel, up, lo, hi, range);
    }
}

/** Send the slot record of a transform a node moves, when its batch uploaded its run into this
 * packet; a moving shadow caster records the bounds of the poses it can render at. The batch
 * record loses its world bounds: they hold the target, and the resolver renders another pose.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param node The node id
 * @param batch Index of the batch
 * @param slot The slot
 * @param rel The target chain from the node down to the instance
 * @param range The range of the world matrices of the node, NULL when the draws record no
 * shadow regions
 */
static void box_transition_send_slot(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    uint32_t node,
    int32_t batch,
    int32_t slot,
    mat4 rel,
    const box_draw_transition_range_t *range)
{
    box_draw_batch_t *b = batch >= 0 && batch < ecs_vec_count(&d->batches)
        ? box_draws_batch(d, batch) : NULL;
    if (!b || !b->alive || !b->key.dynamic_transform || b->transform_frame != p->frame ||
        slot < 0 || slot >= ecs_vec_count(&b->transforms))
    {
        goto done;
    }

    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_SLOT, node);
    r->data = box_transition_matrix(p, rel);
    r->offset = b->transform_data + (uint32_t)slot * (uint32_t)sizeof(box_render_transform_t);
    if (b->record_frame == p->frame) {
        ecs_vec_get_t(&p->batches, box_render_batch_t, b->record)->bounds = false;
    }

    if (range && d->shadow_record && box_draws_caster(b)) {
        box_draws_range_region(world, b, slot, range, rel, ecs_vec_append_t(
            NULL, &s->shadow_regions, box_render_shadow_region_t));
    }
done:
    return;
}

/** Send the slot records of the shape, the shadow and the glyphs of an entity a node moves: the
 * target chain from the node with the instance matrix of each, as box_draws_transform, the
 * shadow (box_draws_box_shadow_instance) and the glyph quads write them.
 *
 * A shadow whose BoxShadow transitions gets no slot record: its box shadow record, sent after
 * the poses, takes the node and the chain, and a caster records the region of every shadow box
 * between the bounds of its BoxShadow (box_draws_box_shadow_bound).
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param item The item of the entity
 * @param rel The target chain from the node down to the entity
 * @param range The range of the world matrices of its node, NULL when the draws record no
 * shadow regions
 */
static void box_transition_send_slots(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    const box_transition_item_t *item,
    vec4 *rel,
    const box_draw_transition_range_t *range)
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
        box_transition_send_slot(world, s, d, p, item->node, ds->batch, ds->slot, m, range);
    }

    const BoxShadowSlot *bs = d->box_shadows && ds && ds->batch >= 0
        ? ecs_get(world, item->entity, BoxShadowSlot) : NULL;
    box_transition_box_shadow_t *animated = bs && bs->batch >= 0
        ? box_transitions_box_shadow_of(s, item->entity) : NULL;
    if (animated) {
        const box_transition_node_t *owner = box_transitions_node_state(s, animated->node);
        box_draw_batch_t *b = box_draws_batch(d, bs->batch);
        animated->moving = item->node;
        glm_mat4_copy(rel, animated->rel);
        const box_transition_slot_t *slot = owner
            ? &owner->slots[BOX_RENDER_TRANSITION_BOX_SHADOW] : NULL;
        if (slot && range && d->shadow_record && b->alive && box_draws_caster(b) &&
            (slot->present
                ? box_draws_box_shadow_bound(world, item->entity, slot->lo, slot->hi, instance)
                : box_draws_box_shadow_instance(world, item->entity, instance)))
        {
            glm_mul(rel, instance, m);
            box_draws_range_region(world, b, bs->slot, range, m, ecs_vec_append_t(
                NULL, &s->shadow_regions, box_render_shadow_region_t));
        }
    } else if (bs && bs->batch >= 0 &&
        box_draws_box_shadow_instance(world, item->entity, instance))
    {
        glm_mul(rel, instance, m);
        box_transition_send_slot(world, s, d, p, item->node, bs->batch, bs->slot, m, range);
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
        instance[1][0] = g->shear * g->height;
        instance[1][1] = g->height;
        instance[3][0] = g->x;
        instance[3][1] = g->y;
        glm_mul(rel, instance, m);
        box_transition_send_slot(world, s, d, p, item->node, at->batch, at->slot, m, range);
    }
done:
    return;
}

/** Mark the items that need the range of their world matrices: those with a shadow caster
 * slot at or below them, walking back from the last item, since a parent comes before its
 * descendants. The others skip the range math, which only shadow regions read.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param count Items of this frame
 */
static void box_transition_casting(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    int32_t count)
{
    bool *casting;
    int32_t i;
    ecs_vec_set_count_t(NULL, &s->casting, bool, count);
    casting = ecs_vec_first_t(&s->casting, bool);
    if (count) {
        ecs_os_memset_n(casting, 0, bool, count);
    }

    for (i = count - 1; i >= 0; i --) {
        const box_transition_item_t *item = ecs_vec_get_t(
            &s->items, box_transition_item_t, i);
        casting[i] = casting[i] || (ecs_is_alive(world, item->entity) &&
            box_draws_entity_casts(world, d, item->entity));
        if (casting[i] && item->up >= 0) {
            casting[item->up] = true;
        }
    }
}

/** Record the regions of the poses the moving shadow casters can render at in this frame and
 * in the last one, so the shadow atlas renders the tiles a caster passes through and, when it
 * stops, those it was in during the last frame, and no shadow stays behind.
 *
 * @param s The state of the extract
 * @param d The state of the batches
 */
static void box_transition_shadows(
    box_render_transitions_t *s,
    box_render_draws_t *d)
{
    int32_t l;
    ecs_vec_t *lists[2] = { &s->shadow_regions, &s->shadow_last };
    for (l = 0; d->shadow_record && l < 2; l ++) {
        int32_t count = ecs_vec_count(lists[l]);
        if (count) {
            ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &d->shadow_regions,
                box_render_shadow_region_t, count), ecs_vec_first(lists[l]),
                box_render_shadow_region_t, count);
        }
    }

    ecs_vec_t tmp = s->shadow_last;
    s->shadow_last = s->shadow_regions;
    s->shadow_regions = tmp;
    ecs_vec_clear(&s->shadow_regions);
}

/** Send a material or shading record: the target material with the nodes of the components
 * that run, copied into the packet data, and the record it patches. A component a node leaves
 * gets its neutral value (box_transitions_leaving_target).
 *
 * @param s The state of the extract
 * @param p The packet
 * @param op BOX_RENDER_TRANSITION_MATERIAL or BOX_RENDER_TRANSITION_SHADING
 * @param material The target material with its nodes
 * @param offset Packet data offset of the record to patch
 */
static void box_transition_send_material(
    const box_render_transitions_t *s,
    box_render_packet_t *p,
    box_render_transition_op_t op,
    const box_draw_transition_material_t *material,
    uint32_t offset)
{
    uint32_t data = 0, node = 0;
    int32_t c;
    void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(*material), &data);
    ecs_os_memcpy(dst, material, ECS_SIZEOF(*material));
    if (s->leaving) {
        box_draw_transition_material_t leaving;
        ecs_os_memcpy(&leaving, material, ECS_SIZEOF(leaving));
        box_transitions_leaving_target(s, &leaving);
        ecs_os_memcpy(dst, &leaving, ECS_SIZEOF(leaving));
    }

    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS && !node; c ++) {
        node = material->nodes[c];
    }

    if (!node) {
        node = material->fade_node;
    }

    box_render_transition_record_t *r = box_render_packet_transition(p, op, node);
    r->data = data;
    r->offset = offset;
}

void box_transitions_leaving_target(
    const box_render_transitions_t *s,
    box_draw_transition_material_t *target)
{
    int32_t c;
    for (c = 0; s->leaving && c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
        box_render_transition_component_t component = (box_render_transition_component_t)c;
        const box_transition_node_t *n = box_transitions_node_state(s, target->nodes[c]);
        void *value = box_draws_transition_value(target, component);
        if (!n || !value || (target->present & (1u << c)) ||
            !box_transition_leaving(s, target->nodes[c], component))
        {
            continue;
        }

        target->present |= 1u << c;
        ecs_os_memcpy(value, n->slots[c].value, box_render_transition_layout(component)->size);
    }
}

bool box_transitions_shading_data(
    box_render_draws_t *d,
    const box_render_packet_t *p,
    int32_t batch,
    int32_t slot,
    uint32_t *out)
{
    box_draw_batch_t *b = batch >= 0 && batch < ecs_vec_count(&d->batches)
        ? box_draws_batch(d, batch) : NULL;
    bool flat = b && b->key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
    uint32_t stride = flat ? (uint32_t)sizeof(box_render_shading_t)
        : (uint32_t)sizeof(box_render_mesh_shading_t);
    bool result = b && b->alive && b->key.dynamic_material && b->shading_frame == p->frame &&
        slot >= 0 && slot < ecs_vec_count(flat ? &b->shadings : &b->mesh_shadings);
    if (result) {
        *out = b->shading_data + (uint32_t)slot * stride;
    }

    return result;
}

void box_transitions_fade_target(
    const box_render_transitions_t *s,
    ecs_entity_t e,
    double time,
    box_draw_transition_material_t *target)
{
    const ecs_map_val_t *found = ecs_map_get(&s->fade_index, e);
    const box_transition_item_t *item = found ? ecs_vec_get_t(
        &s->fade_items, box_transition_item_t, (int32_t)*found - 1) : NULL;
    if (!item) {
        goto done;
    }

    target->fade_node = item->node;
    target->fade_self = item->is_node;
    if (item->is_node) {
        if (box_render_transitions_active(s, item->node, BOX_RENDER_TRANSITION_TINT, time)) {
            target->nodes[BOX_RENDER_TRANSITION_TINT] = item->node;
        }

        if (box_render_transitions_active(s, item->node, BOX_RENDER_TRANSITION_OPACITY, time)) {
            target->nodes[BOX_RENDER_TRANSITION_OPACITY] = item->node;
        }
    } else if (item->up >= 0 && item->up < ecs_vec_count(&s->fade_rels)) {
        const box_transition_fade_rel_t *rel = ecs_vec_get_t(
            &s->fade_rels, box_transition_fade_rel_t, item->up);
        target->fade_rel = rel->tint;
        target->fade_alpha = rel->alpha;
    }
done:
    return;
}

/** Send the shading record of the mesh shape of an entity an active Tint or Opacity transition
 * reaches, when its batch uploaded its shading into this packet. An alpha tested caster records
 * its region, as its shadow reads the alpha.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param e The entity
 */
static void box_transition_send_mesh(
    ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    ecs_entity_t e)
{
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    box_draw_transition_material_t target;
    box_draw_material_t material;
    uint32_t offset;
    if (!ds || ecs_has(world, e, Shape2D) || !ecs_has(world, e, ShapeMesh) ||
        !box_transitions_shading_data(d, p, ds->batch, ds->slot, &offset))
    {
        goto done;
    }

    box_draws_shape_material(world, e, ds, &material);
    box_draws_transition_target(&target, &material, BOX_DRAW_TRANSITION_MESH);
    box_transitions_fade_target(s, e, p->time, &target);
    box_transition_send_material(s, p, BOX_RENDER_TRANSITION_SHADING, &target, offset);
    box_draw_batch_t *b = box_draws_batch(d, ds->batch);
    if (d->shadow_record && b->key.cutout && box_draws_caster(b)) {
        box_draws_shadow_slot(world, d, b, ds->slot);
    }
done:
    return;
}

/** Send the fade record of every node the walk of Tint and Opacity found, parent nodes first,
 * computing the chain of each item from its node, and a shading record for every mesh shape
 * it reaches. The 2D shapes and texts are in the shadings, sent after.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 */
static void box_transition_send_fades(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&s->fade_items);
    ecs_vec_set_count_t(NULL, &s->fade_rels, box_transition_fade_rel_t, count);
    for (i = 0; i < count; i ++) {
        const box_transition_item_t *item = ecs_vec_get_t(
            &s->fade_items, box_transition_item_t, i);
        box_transition_fade_rel_t *rel = ecs_vec_get_t(
            &s->fade_rels, box_transition_fade_rel_t, i);
        const box_transition_fade_rel_t *up = item->up >= 0 ? ecs_vec_get_t(
            &s->fade_rels, box_transition_fade_rel_t, item->up) : NULL;
        *rel = (box_transition_fade_rel_t){ .tint = { .keep = 1.0f }, .alpha = 1.0f };
        if (!ecs_is_alive(world, item->entity)) {
            continue;
        }

        if (item->is_node) {
            const box_transition_node_t *node = box_transitions_node_state(s, item->node);
            bool tint = box_transition_leaving(s, item->node, BOX_RENDER_TRANSITION_TINT);
            bool opacity = box_transition_leaving(s, item->node, BOX_RENDER_TRANSITION_OPACITY);
            Tint leaving_tint = {0};
            Opacity leaving_opacity = {0};
            if (tint) {
                ecs_os_memcpy(&leaving_tint, node->slots[BOX_RENDER_TRANSITION_TINT].value,
                    ECS_SIZEOF(Tint));
            }

            if (opacity) {
                ecs_os_memcpy(&leaving_opacity,
                    node->slots[BOX_RENDER_TRANSITION_OPACITY].value, ECS_SIZEOF(Opacity));
            }

            box_draws_transition_send_fade(world, p, item->entity, item->node, item->parent,
                item->parent && up ? &up->tint : NULL, item->parent && up ? up->alpha : 1.0f,
                tint ? &leaving_tint : NULL, opacity ? &leaving_opacity : NULL);
        } else if (up) {
            *rel = *up;
            box_draws_transition_fade_own(world, item->entity, &rel->tint, &rel->alpha);
        }

        box_transition_send_mesh(world, s, d, p, item->entity);
    }
}

/** Send the shading records of the glyphs of a Text, as box_draws_text_update writes them: a
 * glyph in a colored span renders with the color of the span, which no Rgb transition changes.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param item The text and its nodes
 */
static void box_transition_send_glyphs(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    const box_transition_material_t *item)
{
    const TextSlots *ts = ecs_get(world, item->entity, TextSlots);
    const TextGlyphs *tg = ecs_get(world, item->entity, TextGlyphs);
    const TextSpans *spans = ecs_get(world, item->entity, TextSpans);
    const Text *text = spans && ecs_vec_count(&spans->spans)
        ? ecs_get(world, item->entity, Text) : NULL;
    int32_t length = text && text->value ? (int32_t)strlen(text->value) : 0;
    box_draw_transition_material_t base, span;
    box_draw_material_t material, m;
    box_draw_tint_t tint;
    Rgb rgb;
    Opacity opacity;
    int32_t i, count = ts && tg ? ecs_vec_count(&ts->slots) : 0;
    uint32_t offset;
    if (!count || !ecs_has(world, item->entity, Text)) {
        goto done;
    }

    if (ecs_vec_count(&tg->glyphs) < count) {
        count = ecs_vec_count(&tg->glyphs);
    }

    box_draws_text_material(world, item->entity, &tint, &material);
    box_draws_transition_target(&base, &material, BOX_DRAW_TRANSITION_GLYPH);
    ecs_os_memcpy(base.nodes, item->nodes, ECS_SIZEOF(base.nodes));
    box_transitions_fade_target(s, item->entity, p->time, &base);
    for (i = 0; i < count; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        const box_text_glyph_t *g = ecs_vec_get_t(&tg->glyphs, box_text_glyph_t, i);
        const rgba *color = length ? box_text_span_color(spans, length, g->offset) : NULL;
        if (!box_transitions_shading_data(d, p, at->batch, at->slot, &offset)) {
            continue;
        }

        if (!color) {
            box_transition_send_material(s, p, BOX_RENDER_TRANSITION_SHADING, &base, offset);
            continue;
        }

        box_draws_glyph_span_material(&material, color, &m, &rgb, &opacity);
        box_draws_transition_target(&span, &m, BOX_DRAW_TRANSITION_GLYPH);
        ecs_os_memcpy(span.nodes, item->nodes, ECS_SIZEOF(span.nodes));
        box_transitions_fade_target(s, item->entity, p->time, &span);
        span.nodes[BOX_RENDER_TRANSITION_RGB] = 0;
        span.opacity_scale = (float)color->a / 255.0f;
        box_transition_send_material(s, p, BOX_RENDER_TRANSITION_SHADING, &span, offset);
    }
done:
    return;
}

/** Send the shading record of the shadow of a 2D shape an active Tint or Opacity transition
 * reaches, when the batch of the shadow uploaded its shading into this packet.
 *
 * An Rgb or other material transition of the shape does not change its shadow, and a shadow
 * whose BoxShadow transitions gets its tints and opacity from its box shadow record instead.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 * @param e The entity
 * @param ds The DrawSlot of the entity
 * @param material Material components of its shape
 */
static void box_transition_send_box_shadow(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p,
    ecs_entity_t e,
    const DrawSlot *ds,
    const box_draw_material_t *material)
{
    const BoxShadowSlot *bs = d->box_shadows ? ecs_get(world, e, BoxShadowSlot) : NULL;
    box_draw_transition_material_t target;
    uint32_t offset;
    if (!bs || bs->batch < 0 || !ecs_map_get(&s->fade_index, e) ||
        box_transitions_box_shadow_of(s, e) ||
        !box_transitions_shading_data(d, p, bs->batch, bs->slot, &offset) ||
        !box_draws_box_shadow_target(world, d, e, ds, material, &target))
    {
        goto done;
    }

    box_transitions_fade_target(s, e, p->time, &target);
    if (target.fade_node) {
        box_transition_send_material(s, p, BOX_RENDER_TRANSITION_SHADING, &target, offset);
    }
done:
    return;
}

/** Send a material record for every slot of the material table an active material transition
 * patches and that the table uploaded into this packet, and a shading record for the 2D shape,
 * its shadow and the glyphs of every drawn entity it patches whose batch uploaded its shading.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 */
static void box_transition_send_materials(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_draw_transition_material_t target;
    box_draw_material_t material;
    uint32_t offset;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->material_slots); i ++) {
        const box_transition_material_t *item = ecs_vec_get_t(
            &s->material_slots, box_transition_material_t, i);
        if (!ecs_is_alive(world, item->entity) ||
            !box_draws_material_data(d, item->slot, p->frame, &offset))
        {
            continue;
        }

        box_draws_material_of(world, item->entity, d->materials.epoch, &material);
        box_draws_material_epoch(&d->materials, &material);
        material.water_waves = material.waves
            ? box_draws_water_waves_find(&d->materials, item->entity) : 0u;
        if (material.caustics) {
            box_draws_water_caustics_find(&d->materials, item->entity, material.caustics_frame);
        }

        box_draws_transition_target(&target, &material, BOX_DRAW_TRANSITION_TABLE);
        ecs_os_memcpy(target.nodes, item->nodes, ECS_SIZEOF(target.nodes));
        box_transition_send_material(s, p, BOX_RENDER_TRANSITION_MATERIAL, &target, offset);
    }

    for (i = 0; i < ecs_vec_count(&s->shadings); i ++) {
        const box_transition_material_t *item = ecs_vec_get_t(
            &s->shadings, box_transition_material_t, i);
        if (!ecs_is_alive(world, item->entity)) {
            continue;
        }

        const DrawSlot *ds = ecs_get(world, item->entity, DrawSlot);
        if (ds && ecs_has(world, item->entity, Shape2D) &&
            box_transitions_shading_data(d, p, ds->batch, ds->slot, &offset))
        {
            box_draws_shape_material(world, item->entity, ds, &material);
            box_draws_transition_target(&target, &material, BOX_DRAW_TRANSITION_SHAPE);
            ecs_os_memcpy(target.nodes, item->nodes, ECS_SIZEOF(target.nodes));
            box_transitions_fade_target(s, item->entity, p->time, &target);
            box_transition_send_material(s, p, BOX_RENDER_TRANSITION_SHADING, &target, offset);
            box_transition_send_box_shadow(world, s, d, p, item->entity, ds, &material);
        }

        box_transition_send_glyphs(world, s, d, p, item);
    }
}

/** Send the pose of every node the walk of this frame found, parent nodes first, and a slot
 * record for every transform it moves that a batch uploaded into the packet.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 */
static void box_transition_send_poses(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&s->items);
    ecs_vec_set_count_t(NULL, &s->rels, mat4, count);
    ecs_vec_set_count_t(NULL, &s->ranges, box_draw_transition_range_t,
        d->shadow_record ? count : 0);
    if (d->shadow_record) {
        box_transition_casting(world, s, d, count);
    }

    for (i = 0; i < count; i ++) {
        const box_transition_item_t *item = ecs_vec_get_t(
            &s->items, box_transition_item_t, i);
        vec4 *rel = *ecs_vec_get_t(&s->rels, mat4, i);
        vec4 *up = item->up >= 0 ? *ecs_vec_get_t(&s->rels, mat4, item->up) : NULL;
        box_draw_transition_range_t *range = d->shadow_record &&
            *ecs_vec_get_t(&s->casting, bool, i)
            ? ecs_vec_get_t(&s->ranges, box_draw_transition_range_t, i) : NULL;
        const box_draw_transition_range_t *up_range = range && item->up >= 0
            ? ecs_vec_get_t(&s->ranges, box_draw_transition_range_t, item->up) : NULL;
        if (!ecs_is_alive(world, item->entity)) {
            glm_mat4_identity(rel);
            if (range) {
                box_draws_range_point(GLM_MAT4_IDENTITY, range);
            }

            continue;
        }

        if (item->is_node) {
            mat4 parent;
            if (item->parent) {
                box_transition_send_pose(world, s, p, item, up, NULL, up_range, range);
            } else if (box_draws_parent_world(world, item->entity, p->origin, parent)) {
                box_transition_send_pose(world, s, p, item, parent, NULL, NULL, range);
            } else {
                box_transition_send_pose(world, s, p, item, parent, p->origin, NULL, range);
            }

            glm_mat4_identity(rel);
        } else {
            mat4 local;
            box_draws_target_local(world, item->entity, local);
            glm_mul(up, local, rel);
            if (range && up_range) {
                *range = *up_range;
            } else if (range) {
                box_draws_range_point(GLM_MAT4_IDENTITY, range);
            }
        }

        box_transition_send_slots(world, s, d, p, item, rel, range);
    }

    box_transition_shadows(s, d);
}

/** Send the records that patch the batches of this frame, after the batches were sent: the
 * fades, the materials and shadings, the poses and slots, and the box shadows, which read the
 * poses.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTransitionSlots(
    ecs_iter_t *it)
{
    box_render_transitions_t *s = it->ctx;
    ecs_world_t *world = it->world;
    box_render_draws_t *d = s->draws;
    if (!d) {
        goto done;
    }

    if (ecs_vec_count(&s->fade_items)) {
        box_transition_send_fades(world, s, d, box_render_packet(world));
    }

    if (ecs_vec_count(&s->material_slots) || ecs_vec_count(&s->shadings)) {
        box_transition_send_materials(world, s, d, box_render_packet(world));
    }

    if (ecs_vec_count(&s->items) || ecs_vec_count(&s->shadow_last)) {
        box_transition_send_poses(world, s, d, box_render_packet(world));
    }

    if (ecs_vec_count(&s->box_shadows)) {
        box_transitions_send_box_shadows(world, s, d, box_render_packet(world));
    }
done:
    return;
}

/** Free the state of the transition extract.
 *
 * @param ctx The state
 */
void box_transitions_release(
    void *ctx)
{
    box_render_transitions_t *s = ctx;
    if (-- s->refs) {
        goto done;
    }

    ecs_map_fini(&s->moving);
    ecs_map_fini(&s->tagged);
    ecs_map_fini(&s->tagged_material);
    ecs_map_fini(&s->visited);
    ecs_map_fini(&s->fading);
    ecs_map_fini(&s->fade_index);
    ecs_map_fini(&s->tagged_opacity);
    ecs_map_fini(&s->box_shadow_index);
    ecs_map_fini(&s->tagged_box_shadow);
    ecs_map_fini(&s->light_index);
    ecs_vec_fini_t(NULL, &s->lights, box_render_light_transition_t);
    ecs_vec_fini_t(NULL, &s->box_shadows, box_transition_box_shadow_t);
    ecs_vec_fini_t(NULL, &s->fade_items, box_transition_item_t);
    ecs_vec_fini_t(NULL, &s->fade_rels, box_transition_fade_rel_t);
    ecs_vec_fini_t(NULL, &s->material_slots, box_transition_material_t);
    ecs_vec_fini_t(NULL, &s->shadings, box_transition_material_t);
    ecs_vec_fini_t(NULL, &s->instances, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->nodes, box_transition_node_t);
    ecs_vec_fini_t(NULL, &s->free, uint32_t);
    ecs_vec_fini_t(NULL, &s->dirty, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->gone, uint32_t);
    ecs_vec_fini_t(NULL, &s->revisit, uint32_t);
    ecs_vec_fini_t(NULL, &s->revisit_last, uint32_t);
    ecs_vec_fini_t(NULL, &s->running, int32_t);
    ecs_vec_fini_t(NULL, &s->items, box_transition_item_t);
    ecs_vec_fini_t(NULL, &s->stack, int32_t);
    ecs_vec_fini_t(NULL, &s->rels, mat4);
    ecs_vec_fini_t(NULL, &s->tag_add, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->tag_remove, ecs_entity_t);
    ecs_vec_fini_t(NULL, &s->ranges, box_draw_transition_range_t);
    ecs_vec_fini_t(NULL, &s->casting, bool);
    ecs_vec_fini_t(NULL, &s->shadow_regions, box_render_shadow_region_t);
    ecs_vec_fini_t(NULL, &s->shadow_last, box_render_shadow_region_t);
    ecs_os_free(s);
done:
    return;
}

box_render_transitions_t* box_render_transitions_get(
    const ecs_world_t *world)
{
    const RenderTransitionState *t = ecs_id(RenderTransitionState)
        ? ecs_singleton_get(world, RenderTransitionState) : NULL;

    return t ? t->state : NULL;
}

uint32_t box_render_transitions_node(
    const box_render_transitions_t *s,
    ecs_entity_t entity)
{
    const TransitionNode *own = s && entity && ecs_is_alive(s->world, entity)
        ? ecs_get(s->world, entity, TransitionNode) : NULL;
    const box_transition_node_t *node = own ? box_transitions_node_state(s, own->id) : NULL;

    return node && node->entity == entity ? own->id : 0;
}

bool box_render_transitions_leaving(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t entity,
    box_render_transition_component_t component)
{
    return s && s->leaving && box_transition_leaving_node(world, s, entity, component, 0) != 0;
}

bool box_render_transitions_active(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component,
    double time)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);
    const box_transition_slot_t *slot = n && (n->live & (1u << component)) && time < n->until
        ? &n->slots[component] : NULL;

    return slot && slot->present &&
        box_render_transition_running(slot->start, slot->delay, slot->duration, time);
}

bool box_render_transitions_node_active(
    const box_render_transitions_t *s,
    uint32_t node,
    double time)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);
    bool result = false;
    int32_t c;
    for (c = 0; n && n->live && c < BOX_RENDER_TRANSITION_COMPONENTS && !result; c ++) {
        result = box_render_transitions_active(
            s, node, (box_render_transition_component_t)c, time);
    }

    return result;
}

int32_t box_render_transitions_material_visited(
    const box_render_transitions_t *s)
{
    return ecs_map_count(&s->visited);
}

void box_render_transitions_init(
    ecs_world_t *world)
{
    box_render_transitions_t *s = ecs_os_calloc_t(box_render_transitions_t);
    ecs_entity_t scope = ecs_get_scope(world);
    ecs_entity_t transition = ecs_entity(world, { .name = "transition", .parent = scope });
    ecs_add_id(world, transition, EcsModule);
    ecs_set_scope(world, transition);
    ecs_set_name_prefix(world, "Transition");
    ECS_TAG_DEFINE(world, TransitionTransform);
    ecs_add_pair(world, TransitionTransform, EcsOnInstantiate, EcsDontInherit);
    ECS_TAG_DEFINE(world, TransitionMaterial);
    ecs_add_pair(world, TransitionMaterial, EcsOnInstantiate, EcsDontInherit);
    ECS_TAG_DEFINE(world, TransitionOpacity);
    ecs_add_pair(world, TransitionOpacity, EcsOnInstantiate, EcsDontInherit);
    ECS_TAG_DEFINE(world, TransitionBoxShadow);
    ecs_add_pair(world, TransitionBoxShadow, EcsOnInstantiate, EcsDontInherit);
    box_transitions_node_define(world);
    ecs_set_name_prefix(world, "RenderTransition");
    ECS_COMPONENT_DEFINE(world, RenderTransitionState);
    ecs_set_name_prefix(world, NULL);
    ecs_set_scope(world, scope);
    s->world = world;
    ecs_map_init(&s->moving, NULL);
    ecs_map_init(&s->tagged, NULL);
    ecs_map_init(&s->tagged_material, NULL);
    ecs_map_init(&s->visited, NULL);
    ecs_map_init(&s->fading, NULL);
    ecs_map_init(&s->fade_index, NULL);
    ecs_map_init(&s->tagged_opacity, NULL);
    ecs_map_init(&s->box_shadow_index, NULL);
    ecs_map_init(&s->tagged_box_shadow, NULL);
    ecs_map_init(&s->light_index, NULL);
    ecs_vec_init_t(NULL, &s->lights, box_render_light_transition_t, 0);
    ecs_vec_init_t(NULL, &s->box_shadows, box_transition_box_shadow_t, 0);
    ecs_vec_init_t(NULL, &s->fade_items, box_transition_item_t, 0);
    ecs_vec_init_t(NULL, &s->fade_rels, box_transition_fade_rel_t, 0);
    ecs_vec_init_t(NULL, &s->material_slots, box_transition_material_t, 0);
    ecs_vec_init_t(NULL, &s->shadings, box_transition_material_t, 0);
    ecs_vec_init_t(NULL, &s->instances, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->nodes, box_transition_node_t, 0);
    ecs_vec_init_t(NULL, &s->free, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->gone, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->revisit, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->revisit_last, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->running, int32_t, 0);
    ecs_vec_init_t(NULL, &s->items, box_transition_item_t, 0);
    ecs_vec_init_t(NULL, &s->stack, int32_t, 0);
    ecs_vec_init_t(NULL, &s->rels, mat4, 0);
    ecs_vec_init_t(NULL, &s->tag_add, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->tag_remove, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &s->ranges, box_draw_transition_range_t, 0);
    ecs_vec_init_t(NULL, &s->casting, bool, 0);
    ecs_vec_init_t(NULL, &s->shadow_regions, box_render_shadow_region_t, 0);
    ecs_vec_init_t(NULL, &s->shadow_last, box_render_shadow_region_t, 0);
    s->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Transitions" }),
        .terms = {{
            .id = ecs_id(Transition),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA,
            .inout = EcsIn
        }},
        .flags = EcsQueryMatchPrefab,
        .cache_kind = EcsQueryCacheNone
    });

    s->rescan = true;
    s->refs = 1;
    box_transitions_observers(world, s);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Transitions" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTransitions,
        .ctx = s,
        .ctx_free = box_transitions_release,
        .immediate = true
    });
    ecs_singleton_set(world, RenderTransitionState, { .state = s });
}

void box_render_transitions_slots_init(
    ecs_world_t *world)
{
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_assert(s != NULL, ECS_INTERNAL_ERROR, NULL);
    s->draws = box_draws_state(world);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TransitionSlots" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTransitionSlots,
        .ctx = s
    });
}
