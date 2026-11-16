/**
 * @file render/transition_light.c
 * @brief Transitions of lights on the game thread: the lights whose PointLight or SpotLight
 * runs or leaves, or whose Rgb runs, in a frame, which the light extract keeps lit and patches.
 *
 * The light buffers hold the targets, so a change of intensity or color does not render a
 * shadow tile again; a listed light uploads its buffer every frame with a light record, which
 * the resolver writes again from the values its nodes render.
 */

#include "transitions.h"

/** Check whether a light slot of a node runs or leaves at a time.
 *
 * @param s The state of the extract
 * @param node The node id
 * @param component The component
 * @param time World time, the time of the packet
 * @return Whether the resolver renders a value other than the target, or the node leaves it
 */
static bool box_transition_light_runs(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component,
    double time)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);
    const box_transition_slot_t *slot = n ? &n->slots[component] : NULL;

    return slot && slot->present && (slot->leaving ||
        box_render_transition_running(slot->start, slot->delay, slot->duration, time));
}

/** Get the slot of a component of a node.
 *
 * @param s The state of the extract
 * @param node The node id
 * @param component The component
 * @return The slot, NULL when the node is not in use
 */
static const box_transition_slot_t* box_transition_light_slot(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component)
{
    const box_transition_node_t *n = box_transitions_node_state(s, node);

    return n ? &n->slots[component] : NULL;
}

/** Add an entity to the light transitions of this frame when it is a light, or a light that
 * leaves, and its light or its Rgb renders a transition.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity
 * @param time World time, the time of the packet
 */
static void box_transition_light_add(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e,
    double time)
{
    box_render_light_transition_t lt = { .entity = e, .rgb = {255, 255, 255} };
    box_render_transition_component_t component = BOX_RENDER_TRANSITION_COMPONENTS;
    const box_transition_slot_t *slot = NULL;
    const DirectionalLight *directional = NULL;
    const void *value = NULL;
    const Rgb *rgb;
    uint32_t node, rgb_node;
    if (ecs_has_id(world, e, EcsPrefab) || ecs_map_get(&s->light_index, e)) {
        goto done;
    }

    if ((value = ecs_get(world, e, PointLight))) {
        lt.kind = 1;
        component = BOX_RENDER_TRANSITION_POINT_LIGHT;
    } else if ((value = ecs_get(world, e, SpotLight))) {
        lt.kind = 2;
        component = BOX_RENDER_TRANSITION_SPOT_LIGHT;
    } else if ((directional = ecs_get(world, e, DirectionalLight))) {
        lt.kind = 0;
    } else if (box_render_transitions_leaving(world, s, e, BOX_RENDER_TRANSITION_POINT_LIGHT)) {
        lt.kind = 1;
        lt.leaving = true;
        component = BOX_RENDER_TRANSITION_POINT_LIGHT;
    } else if (box_render_transitions_leaving(world, s, e, BOX_RENDER_TRANSITION_SPOT_LIGHT)) {
        lt.kind = 2;
        lt.leaving = true;
        component = BOX_RENDER_TRANSITION_SPOT_LIGHT;
    } else {
        goto done;
    }

    if (component != BOX_RENDER_TRANSITION_COMPONENTS) {
        node = box_transitions_owner(world, s, e, component);
        lt.node = box_transition_light_runs(s, node, component, time) ? node : 0;
        slot = lt.node ? box_transition_light_slot(s, lt.node, component) : NULL;
    }

    rgb_node = box_transitions_owner(world, s, e, BOX_RENDER_TRANSITION_RGB);
    lt.rgb_node = box_render_transitions_active(s, rgb_node, BOX_RENDER_TRANSITION_RGB, time)
        ? rgb_node : 0;
    if ((!lt.node && !lt.rgb_node) || (lt.leaving && !slot)) {
        goto done;
    }

    if (lt.leaving) {
        ecs_os_memcpy(lt.value, slot->value, BOX_RENDER_TRANSITION_VALUE);
    } else if (value) {
        box_render_transition_pack(component, value, lt.value);
    } else {
        ecs_os_memcpy(lt.value, &directional->intensity, ECS_SIZEOF(float));
    }

    if (lt.kind) {
        ecs_os_memcpy(&lt.range, ECS_OFFSET(lt.value, offsetof(PointLight, range)),
            ECS_SIZEOF(float));
        if (slot) {
            lt.range = glm_max(lt.range, slot->hi[1]);
        }
    }

    if (lt.kind == 2) {
        ecs_os_memcpy(&lt.outer, ECS_OFFSET(lt.value, offsetof(SpotLight, outer_angle)),
            ECS_SIZEOF(float));
        if (slot) {
            lt.outer = glm_max(lt.outer, slot->hi[3]);
        }
    }

    rgb = ecs_get(world, e, Rgb);
    if (rgb) {
        lt.rgb = *rgb;
        lt.has_rgb = true;
    }

    *ecs_vec_append_t(NULL, &s->lights, box_render_light_transition_t) = lt;
    ecs_map_insert(&s->light_index, e, (ecs_map_val_t)ecs_vec_count(&s->lights));
done:
    return;
}

/** Check whether the instances in a table can be lights the walk adds: they have a light, or
 * a slot leaves in this frame, which may be the light of a base they lost.
 *
 * @param world The world
 * @param s The state of the extract
 * @param table The table
 * @return Whether the walk adds the entities of the table
 */
static bool box_transition_light_reads(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    const ecs_table_t *table)
{
    return s->leaving || ecs_table_has_id(world, table, ecs_id(PointLight)) ||
        ecs_table_has_id(world, table, ecs_id(SpotLight)) ||
        ecs_table_has_id(world, table, ecs_id(DirectionalLight));
}

/** Add the lights of the entity of a node and of its IsA instances, down every level.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity of the node
 * @param time World time, the time of the packet
 */
static void box_transition_light_walk(
    const ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_entity_t e,
    double time)
{
    int32_t i;
    ecs_vec_clear(&s->instances);
    *ecs_vec_append_t(NULL, &s->instances, ecs_entity_t) = e;
    while (ecs_vec_count(&s->instances)) {
        ecs_entity_t cur = *ecs_vec_last_t(&s->instances, ecs_entity_t);
        ecs_vec_remove_last(&s->instances);
        box_transition_light_add(world, s, cur, time);
        ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, cur));
        while (ecs_each_next(&it)) {
            bool reads = box_transition_light_reads(world, s, it.table);
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

void box_transitions_lights(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p)
{
    int32_t r, i, count = ecs_vec_count(&s->running);
    ecs_vec_clear(&s->lights);
    if (ecs_map_count(&s->light_index)) {
        ecs_map_clear(&s->light_index);
    }

    for (r = 0; p && r < count; r ++) {
        const box_transition_node_t *node = box_transitions_running(s, r, &i);
        if (!node->entity || !node->live) {
            continue;
        }

        if ((box_transition_light_runs(s, (uint32_t)i, BOX_RENDER_TRANSITION_POINT_LIGHT,
                p->time) ||
            box_transition_light_runs(s, (uint32_t)i, BOX_RENDER_TRANSITION_SPOT_LIGHT,
                p->time) ||
            box_render_transitions_active(s, (uint32_t)i, BOX_RENDER_TRANSITION_RGB,
                p->time)) &&
            ecs_is_alive(world, node->entity))
        {
            box_transition_light_walk(world, s, node->entity, p->time);
        }
    }
}

const box_render_light_transition_t* box_render_transitions_light(
    const box_render_transitions_t *s,
    ecs_entity_t entity)
{
    const ecs_map_val_t *found = s && ecs_map_count(&s->light_index)
        ? ecs_map_get(&s->light_index, entity) : NULL;

    return found ? ecs_vec_get_t(&s->lights, box_render_light_transition_t,
        (int32_t)*found - 1) : NULL;
}

int32_t box_render_transitions_lights(
    const box_render_transitions_t *s,
    const box_render_light_transition_t **out)
{
    int32_t result = s ? ecs_vec_count(&s->lights) : 0;
    *out = result ? ecs_vec_first(&s->lights) : NULL;

    return result;
}
