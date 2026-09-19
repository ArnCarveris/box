/**
 * @file transition/types.c
 * @brief Turns the reflection of a component into the float layout a transition uses.
 *
 * A transitionable component is a struct of floats and bytes, up to four of
 * them. The layout records where each one lives so the interpolation reads and
 * writes the component without consulting reflection again, and which member
 * entity covers which floats so a Transition can key a single member.
 */

#include "transition_internal.h"

/** Append one float of a component to a layout.
 *
 * @param out The layout
 * @param offset Byte offset of the value in the component
 * @param kind Primitive kind at that offset
 * @return 0 when the value was appended, nonzero when it cannot be interpolated
 */
static int box_transition_add_slot(
    TransitionType *out,
    uint16_t offset,
    ecs_primitive_kind_t kind)
{
    int result = -1;
    if (out->count >= BOX_TRANSITION_MAX) {
        goto done;
    }

    if (kind == EcsF32) {
        out->slots[out->count].kind = BoxTransitionF32;
    } else if (kind == EcsU8 || kind == EcsByte) {
        out->slots[out->count].kind = BoxTransitionU8;
    } else {
        goto done;
    }

    out->slots[out->count].offset = offset;
    out->count ++;
    result = 0;
done:
    return result;
}

/** Append the member entity of the key type that covers a range of floats.
 *
 * @param world The world
 * @param key The type whose members a Transition may name
 * @param name Name of the member
 * @param index First float of the member
 * @param out The layout
 */
static void box_transition_add_member(
    const ecs_world_t *world,
    ecs_entity_t key,
    const char *name,
    uint8_t index,
    TransitionType *out)
{
    if (out->member_count >= BOX_TRANSITION_MEMBER_MAX) {
        goto done;
    }

    ecs_entity_t member = ecs_lookup_child(world, key, name);
    if (!member) {
        goto done;
    }

    out->members[out->member_count].member = member;
    out->members[out->member_count].index = index;
    out->members[out->member_count].count = (uint8_t)(out->count - index);
    out->member_count ++;
done:
    return;
}

/** Walk a reflected type and append every float it is made of.
 *
 * @param world The world
 * @param type The type to walk
 * @param key The type whose members a Transition may name, 0 below the top level
 * @param base Byte offset of the type inside the component
 * @param out The layout
 * @return 0 when every value can be interpolated, nonzero when one cannot
 */
static int box_transition_walk(
    const ecs_world_t *world,
    ecs_entity_t type,
    ecs_entity_t key,
    uint16_t base,
    TransitionType *out)
{
    int result = 0;
    const EcsPrimitive *primitive = ecs_get(world, type, EcsPrimitive);
    if (primitive) {
        result = box_transition_add_slot(out, base, primitive->kind);
        goto done;
    }

    const EcsStruct *type_struct = ecs_get(world, type, EcsStruct);
    if (!type_struct) {
        result = -1;
        goto done;
    }

    const ecs_member_t *members = ecs_vec_first_t(&type_struct->members, ecs_member_t);
    int32_t i, count = ecs_vec_count(&type_struct->members);
    for (i = 0; i < count && !result; i ++) {
        uint8_t index = out->count;
        if (members[i].count > 1) {
            result = -1;
            continue;
        }

        result = box_transition_walk(world, members[i].type, 0,
            (uint16_t)(base + members[i].offset), out);
        if (!result && key) {
            box_transition_add_member(world, key, members[i].name, index, out);
        }
    }

done:
    return result;
}

int box_transition_layout(
    const ecs_world_t *world,
    ecs_entity_t component,
    ecs_entity_t key,
    TransitionType *out)
{
    memset(out, 0, sizeof(TransitionType));
    out->key = key;

    int result = box_transition_walk(world, component, key, 0, out);
    if (!result && !out->count) {
        result = -1;
    }

    return result;
}

/** A component gained the Transitionable tag: give it a float layout.
 *
 * @param it Iterator over the component entities
 */
static void box_transition_on_transitionable(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t component = it->entities[i];
        if (ecs_has(it->world, component, TransitionType)) {
            continue;
        }

        box_transition_add_type(it->world, it->stage, component, component);
    }
}

void box_transition_add_type(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t component,
    ecs_entity_t key)
{
    TransitionType type;
    if (box_transition_layout(world, component, key, &type)) {
        ecs_err("component %s cannot be interpolated by a transition",
            ecs_get_name(world, component));
        goto done;
    }

    ecs_set_ptr(stage, component, TransitionType, &type);
    box_transition_watch(world, component);
done:
    return;
}

void box_transition_types_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = Transitionable }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd },
        .callback = box_transition_on_transitionable
    });
}
