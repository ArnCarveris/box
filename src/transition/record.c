/**
 * @file transition/record.c
 * @brief Starts and replaces the transitions of an entity when a value is assigned.
 *
 * A record outlives its transition and keeps the displayed value, so the next
 * assignment knows where to start. Assigning the displayed value produces a
 * record that is finished right away.
 */

#include "transition_internal.h"

/** Read the parameters a Transition map gives a key.
 *
 * @param map The map
 * @param key The component or member entity
 * @return The parameters, NULL when the map does not cover the key
 */
static const TransitionParams* box_transition_params(
    const Transition *map,
    ecs_entity_t key)
{
    const TransitionParams *result = NULL;
    if (!ecs_map_is_init(map)) {
        goto done;
    }

    ecs_map_val_t *value = ecs_map_get(map, key);
    if (!value) {
        goto done;
    }

    result = ECS_SIZEOF(TransitionParams) > ECS_SIZEOF(ecs_map_val_t) ?
        (const TransitionParams*)(uintptr_t)*value :
        (const TransitionParams*)value;
done:
    return result;
}

/** Time the world has been running.
 *
 * @param world The world
 * @return Seconds since the first frame
 */
static double box_transition_now(
    const ecs_world_t *world)
{
    return ecs_get_world_info(world)->world_time_total;
}

/** Collect the floats a component currently shows.
 *
 * @param state The transitions of the entity
 * @param component The component
 * @param type Its float layout
 * @param value Its authored value
 * @param now The time to evaluate at
 * @param out Receives type->count floats
 */
static void box_transition_current(
    const TransitionState *state,
    ecs_entity_t component,
    const TransitionType *type,
    const void *value,
    double now,
    float *out)
{
    box_transition_read(type->slots, type->count, value, out);

    const box_transition_t *records = ecs_vec_first_t(
        &state->records, box_transition_t);
    int32_t i, count = ecs_vec_count(&state->records);
    for (i = 0; i < count; i ++) {
        if (records[i].component != component) {
            continue;
        }

        float displayed[BOX_TRANSITION_MAX];
        int32_t j;

        box_transition_eval(&records[i], now, displayed);
        for (j = 0; j < records[i].count; j ++) {
            out[records[i].index + j] = displayed[j];
        }
    }
}

/** Drop every record of a component.
 *
 * @param state The transitions of the entity
 * @param component The component
 */
static void box_transition_drop(
    TransitionState *state,
    ecs_entity_t component)
{
    box_transition_t *records = ecs_vec_first_t(&state->records, box_transition_t);
    int32_t i = ecs_vec_count(&state->records);
    while (i > 0) {
        i --;
        if (records[i].component == component) {
            ecs_vec_remove_t(&state->records, box_transition_t, i);
        }
    }
}

/** Append one record for a range of floats of a component.
 *
 * @param state The transitions of the entity
 * @param component The component
 * @param type Its float layout
 * @param index First float of the range
 * @param count Number of floats of the range
 * @param params Duration, delay and timing of the range
 * @param from The floats the entity shows
 * @param to The floats that were assigned
 * @param now The time the transition starts at
 */
static void box_transition_push(
    TransitionState *state,
    ecs_entity_t component,
    const TransitionType *type,
    int32_t index,
    int32_t count,
    const TransitionParams *params,
    const float *from,
    const float *to,
    double now)
{
    box_transition_t *record = ecs_vec_append_t(
        NULL, &state->records, box_transition_t);

    memset(record, 0, sizeof(box_transition_t));
    record->component = component;
    record->start = now;
    record->timing = params->timing_function;
    record->index = (uint8_t)index;
    record->count = (uint8_t)count;

    bool moves = false;
    int32_t i;
    for (i = 0; i < count; i ++) {
        record->from[i] = from[index + i];
        record->to[i] = to[index + i];
        record->slots[i] = type->slots[index + i];
        moves = moves || record->from[i] != record->to[i];
    }

    if (moves) {
        record->duration = params->duration;
        record->delay = params->delay;
    }
}

/** Recompute when the transitions of an entity end and whether a view lags.
 *
 * @param world The world
 * @param e The entity
 * @param state The transitions of the entity
 * @param now The current time
 */
static void box_transition_refresh(
    const ecs_world_t *world,
    ecs_entity_t e,
    TransitionState *state,
    double now)
{
    const box_transition_t *records = ecs_vec_first_t(
        &state->records, box_transition_t);
    double until = 0;
    int32_t i, count = ecs_vec_count(&state->records);
    for (i = 0; i < count; i ++) {
        double end = records[i].start + (double)records[i].delay +
            (double)records[i].duration;
        if (end > until) {
            until = end;
        }
    }

    state->until = until;
    if (until > now) {
        state->dirty = true;
        state->published = ecs_has_id(world, e,
            ecs_pair(ecs_id(TransitionValue), EcsWildcard));
    }
}

void box_transition_assign(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component,
    TransitionState *state)
{
    const Transition *map = ecs_get(world, e, Transition);
    const TransitionType *type = ecs_get(world, component, TransitionType);
    const void *value = map && type ? ecs_get_id(world, e, component) : NULL;
    if (!value) {
        goto done;
    }

    const TransitionParams *params[BOX_TRANSITION_MAX] = {0};
    const TransitionParams *whole = box_transition_params(map, type->key);
    bool governed = whole != NULL;
    int32_t i;
    for (i = 0; i < type->count; i ++) {
        params[i] = whole;
    }

    for (i = 0; i < type->member_count; i ++) {
        const TransitionParams *member = box_transition_params(
            map, type->members[i].member);
        if (!member) {
            continue;
        }

        int32_t j;
        for (j = 0; j < type->members[i].count; j ++) {
            params[type->members[i].index + j] = member;
        }

        governed = true;
    }

    if (!governed) {
        goto done;
    }

    double now = box_transition_now(world);
    float current[BOX_TRANSITION_MAX];
    float target[BOX_TRANSITION_MAX];

    box_transition_current(state, component, type, value, now, current);
    box_transition_read(type->slots, type->count, value, target);
    box_transition_drop(state, component);

    i = 0;
    while (i < type->count) {
        int32_t first = i;
        while (i < type->count && params[i] == params[first]) {
            i ++;
        }

        if (params[first]) {
            box_transition_push(state, component, type, first, i - first,
                params[first], current, target, now);
        }
    }

    box_transition_refresh(world, e, state, now);
done:
    return;
}

/** A component a Transition covers was assigned.
 *
 * The entities are walked backwards because adding the private state moves an
 * entity out of the table the iterator walks, which shifts the entries after
 * it but leaves the ones before it in place.
 *
 * @param it Iterator over the entities that were assigned
 */
static void box_transition_on_set(
    ecs_iter_t *it)
{
    ecs_entity_t component = ecs_field_id(it, 0);
    int32_t i = it->count;
    while (i > 0) {
        i --;

        ecs_entity_t e = it->entities[i];
        TransitionState *state = ecs_get_mut(it->world, e, TransitionState);
        if (state) {
            box_transition_assign(it->world, e, component, state);
        } else {
            ecs_add(it->stage, e, TransitionState);
        }
    }
}

/** Record what every value a map covers currently shows.
 *
 * @param world The world
 * @param e The entity
 * @param state The transitions of the entity
 */
static void box_transition_baseline(
    ecs_world_t *world,
    ecs_entity_t e,
    TransitionState *state)
{
    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i;
    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (id & ECS_ID_FLAGS_MASK) {
            continue;
        }

        if (ecs_has(world, id, TransitionType)) {
            box_transition_assign(world, e, id, state);
        }
    }
}

/** An entity gained or changed a map: keep the private state in step with it.
 *
 * A map that is filled after it is added, which is what setting one from C
 * does, has no keys yet when the state appears, so the values are recorded
 * again once the map itself is set.
 *
 * @param it Iterator over the entities whose Transition changed
 */
static void box_transition_on_map(
    ecs_iter_t *it)
{
    int32_t i = it->count;
    while (i > 0) {
        i --;

        ecs_entity_t e = it->entities[i];
        TransitionState *state = ecs_get_mut(it->world, e, TransitionState);
        if (state) {
            box_transition_baseline(it->world, e, state);
        } else {
            ecs_add(it->stage, e, TransitionState);
        }
    }
}

/** An entity gained the private state: record what every covered value shows.
 *
 * @param it Iterator over the entities that gained the state
 */
static void box_transition_on_add(
    ecs_iter_t *it)
{
    TransitionState *state = ecs_field(it, TransitionState, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_transition_baseline(it->world, it->entities[i], &state[i]);
    }
}

/** The map of an entity was removed: it has no transitions left.
 *
 * @param it Iterator over the entities that lost their Transition
 */
static void box_transition_on_remove(
    ecs_iter_t *it)
{
    if (it->other_table && !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    int32_t i = it->count;
    while (i > 0) {
        i --;
        ecs_remove(it->stage, it->entities[i], TransitionState);
    }

done:
    return;
}

/** Write the value a transition shows into the pairs that ask for it.
 *
 * @param world The world
 * @param e The entity
 * @param state The transitions of the entity
 * @param now The current time
 */
static void box_transition_publish(
    ecs_world_t *world,
    ecs_entity_t e,
    const TransitionState *state,
    double now)
{
    const box_transition_t *records = ecs_vec_first_t(
        &state->records, box_transition_t);
    int32_t i, count = ecs_vec_count(&state->records);
    for (i = 0; i < count; i ++) {
        ecs_id_t pair = ecs_pair(ecs_id(TransitionValue), records[i].component);
        TransitionValue *out = ecs_get_mut_id(world, e, pair);
        if (!out) {
            continue;
        }

        float displayed[BOX_TRANSITION_MAX];
        int32_t j;

        box_transition_eval(&records[i], now, displayed);
        for (j = 0; j < records[i].count; j ++) {
            out->value[records[i].index + j] = displayed[j];
        }
    }
}

/** Write the values of the frame into the pairs that asked for them.
 *
 * The system runs after Extract, so the pair holds what the frame just showed.
 * Whether a transition still moves is the business of Extract, which clears
 * the flag once a view has shown the end of it.
 *
 * @param it System iterator
 */
static void BoxTransitionPublish(
    ecs_iter_t *it)
{
    const TransitionState *state = ecs_field(it, TransitionState, 0);
    double now = box_transition_now(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (state[i].published) {
            box_transition_publish(it->world, it->entities[i], &state[i], now);
        }
    }
}

void box_transition_watch(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_observer(world, {
        .query.terms = {
            { .id = component, .inout = EcsIn },
            { .id = ecs_id(TransitionState), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = box_transition_on_set
    });
}

void box_transition_records_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TransitionState) }},
        .events = { EcsOnAdd },
        .callback = box_transition_on_add
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Transition), .inout = EcsIn }},
        .events = { EcsOnAdd, EcsOnSet },
        .callback = box_transition_on_map
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Transition) }},
        .events = { EcsOnRemove },
        .callback = box_transition_on_remove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxTransitionPublish" }),
        .phase = EcsOnStore,
        .query.terms = {{ .id = ecs_id(TransitionState), .inout = EcsIn }},
        .callback = BoxTransitionPublish
    });
}
