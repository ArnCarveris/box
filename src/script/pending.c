/**
 * @file script/pending.c
 * @brief Queue of pending template instances and the budgeted drain.
 *
 * A rebuild scans the cached query of pending instances, keys every entry with
 * its squared distance to the active camera and turns the vector into a min
 * heap in linear time. A frame pops the nearest entries until it runs out of
 * count or milliseconds.
 */

#include "script_internal.h"
#include "../render/render_internal.h"

/** Swap two entries of the queue.
 *
 * @param a The first entry
 * @param b The second entry
 */
static void box_script_swap(
    box_script_entry_t *a,
    box_script_entry_t *b)
{
    box_script_entry_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/** Move an entry down until its children are not nearer than it is.
 *
 * @param entries The heap
 * @param count Entries in the heap
 * @param index The entry to move
 */
static void box_script_sift_down(
    box_script_entry_t *entries,
    int32_t count,
    int32_t index)
{
    int32_t root = index;
    bool moved = true;
    while (moved) {
        int32_t left = root * 2 + 1;
        int32_t right = left + 1;
        int32_t min = root;
        if (left < count && entries[left].key < entries[min].key) {
            min = left;
        }

        if (right < count && entries[right].key < entries[min].key) {
            min = right;
        }

        moved = min != root;
        if (moved) {
            box_script_swap(&entries[root], &entries[min]);
            root = min;
        }
    }
}

/** Turn the queue into a min heap in linear time.
 *
 * @param state The state
 */
static void box_script_heapify(
    box_script_state_t *state)
{
    int32_t count = ecs_vec_count(&state->queue);
    box_script_entry_t *entries = ecs_vec_first_t(&state->queue, box_script_entry_t);
    int32_t i;
    for (i = count / 2 - 1; i >= 0; i --) {
        box_script_sift_down(entries, count, i);
    }
}

/** Take the entry nearest to the camera off the queue.
 *
 * @param state The state
 * @param out Receives the entry
 * @return true when the queue held an entry
 */
static bool box_script_pop(
    box_script_state_t *state,
    box_script_entry_t *out)
{
    bool result = false;
    int32_t count = ecs_vec_count(&state->queue);
    if (count) {
        box_script_entry_t *entries = ecs_vec_first_t(&state->queue, box_script_entry_t);
        *out = entries[0];
        entries[0] = entries[count - 1];
        ecs_vec_remove_last(&state->queue);
        box_script_sift_down(entries, count - 1, 0);
        result = true;
    }

    return result;
}

/** Squared distance between two points.
 *
 * @param a The first point
 * @param b The second point
 * @return The squared distance
 */
static double box_script_dist2(
    vec3 a,
    vec3 b)
{
    vec3 d = vec3_sub(a, b);

    return (double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z;
}

/** Get the position of the active camera.
 *
 * @param world The world
 * @param out Receives the position
 * @return true when the world has an active camera
 */
static bool box_script_eye(
    ecs_world_t *world,
    vec3 *out)
{
    bool result = false;
    ecs_entity_t camera = box_render_active_camera(world);
    if (!camera) {
        goto done;
    }

    const WorldTransform *wt = ecs_get(world, camera, WorldTransform);
    if (wt) {
        *out = vec3_new(wt->m.v[12], wt->m.v[13], wt->m.v[14]);
        result = true;
        goto done;
    }

    const Position *p = ecs_get(world, camera, Position);
    if (p) {
        *out = vec3_new(p->x, p->y, p->z);
        result = true;
    }

done:
    return result;
}

/** Key an instance with its squared distance to the camera.
 *
 * The world position wins over the local one, so an instance under a parent is
 * ordered by where it ends up; an instance without a position sorts first.
 *
 * @param state The state
 * @param p The local position of the instance, NULL when it has none
 * @param wt The world transform of the instance, NULL when it has none
 * @return The heap key
 */
static double box_script_key(
    const box_script_state_t *state,
    const Position *p,
    const WorldTransform *wt)
{
    double result = 0;
    if (!state->has_eye) {
        goto done;
    }

    if (wt) {
        result = box_script_dist2(state->eye,
            vec3_new(wt->m.v[12], wt->m.v[13], wt->m.v[14]));
    } else if (p) {
        result = box_script_dist2(state->eye, vec3_new(p->x, p->y, p->z));
    }

done:
    return result;
}

/** Collect every pending instance and order the queue.
 *
 * @param world The world
 * @param state The state
 * @param settings The settings
 */
static void box_script_rebuild(
    ecs_world_t *world,
    box_script_state_t *state,
    const ScriptSettings *settings)
{
    ecs_vec_clear(&state->queue);
    bool by_distance = settings->nearest_first && state->has_eye;
    double seq = 0;
    ecs_iter_t it = ecs_query_iter(world, state->query);
    while (ecs_query_next(&it)) {
        ecs_entity_t template_ = ecs_pair_second(world, ecs_field_id(&it, 0));
        const Position *p = ecs_field(&it, Position, 1);
        const WorldTransform *wt = ecs_field(&it, WorldTransform, 2);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_script_entry_t *e = ecs_vec_append_t(
                NULL, &state->queue, box_script_entry_t);
            e->instance = it.entities[i];
            e->template_ = template_;
            e->key = by_distance
                ? box_script_key(state, p ? &p[i] : NULL, wt ? &wt[i] : NULL)
                : seq;
            seq += 1;
        }
    }

    box_script_heapify(state);
    state->dirty = false;
    state->age = 0;
}

/** Rebuild the queue when it went stale, the camera moved or it aged out.
 *
 * @param world The world
 * @param state The state
 * @param settings The settings
 */
static void box_script_refresh(
    ecs_world_t *world,
    box_script_state_t *state,
    const ScriptSettings *settings)
{
    vec3 eye = vec3_new(0, 0, 0);
    bool has_eye = box_script_eye(world, &eye);
    double moved = has_eye && state->has_eye
        ? box_script_dist2(eye, state->eye)
        : 0;
    double threshold = settings->reorder_distance * settings->reorder_distance;
    bool stale = state->dirty || has_eye != state->has_eye || moved > threshold ||
        (settings->reorder_interval > 0 && state->age >= settings->reorder_interval);
    state->eye = eye;
    state->has_eye = has_eye;
    if (stale && (state->dirty || ecs_vec_count(&state->queue))) {
        box_script_rebuild(world, state, settings);
    }
}

/** Test whether a drain used up its milliseconds.
 *
 * @param start The start of the drain
 * @param budget_ms The budget, 0 for no limit
 * @param done Instances expanded so far
 * @return true when the drain must stop
 */
static bool box_script_over_budget(
    const ecs_time_t *start,
    double budget_ms,
    int32_t done)
{
    bool result = false;
    if (done > 0 && budget_ms > 0) {
        ecs_time_t t = *start;
        result = ecs_time_measure(&t) * 1000.0 >= budget_ms;
    }

    return result;
}

/** Expand the nearest pending instances until a budget runs out.
 *
 * @param world The world
 * @param state The state
 * @param limit Instances this call may expand, 0 for no limit
 * @param budget_ms Milliseconds the drain may take, 0 for no limit
 * @param start The start of the frame
 * @return Instances expanded
 */
static int32_t box_script_expand(
    ecs_world_t *world,
    box_script_state_t *state,
    int32_t limit,
    double budget_ms,
    const ecs_time_t *start)
{
    int32_t done = 0;
    box_script_entry_t entry;
    while (!box_script_over_budget(start, budget_ms, done)) {
        if (limit > 0 && done >= limit) {
            break;
        }

        if (!box_script_pop(state, &entry)) {
            break;
        }

        if (!ecs_script_template_update(world, entry.instance, entry.template_)) {
            done ++;
        }
    }

    return done;
}

box_script_state_t* box_script_state_new(
    ecs_world_t *world)
{
    box_script_state_t *result = ecs_os_calloc_t(box_script_state_t);
    ecs_vec_init_t(NULL, &result->queue, box_script_entry_t, 0);
    result->dirty = true;
    result->query = ecs_query(world, {
        .terms = {
            { .id = ecs_pair(EcsScriptTemplatePending, EcsWildcard) },
            { .id = ecs_id(Position), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional }
        }
    });

    return result;
}

void box_script_state_free(
    box_script_state_t *state)
{
    if (state->query) {
        ecs_query_fini(state->query);
    }

    ecs_vec_fini_t(NULL, &state->queue, box_script_entry_t);
    ecs_os_free(state);
}

void box_script_drain(
    ecs_world_t *world,
    box_script_state_t *state,
    const ScriptSettings *settings,
    ScriptStats *stats)
{
    ecs_time_t start;
    ecs_os_get_time(&start);
    bool all = !settings->enabled;
    int32_t limit = all ? 0 : settings->templates_per_frame;
    double budget = all ? 0 : settings->budget_ms;
    int32_t done = 0;
    int32_t round = 1;
    while (round > 0) {
        box_script_refresh(world, state, settings);
        round = box_script_expand(world, state, limit, budget, &start);
        done += round;
        if (!all || !state->dirty) {
            round = 0;
        }
    }

    double elapsed = ecs_time_measure(&start) * 1000.0;
    if (elapsed > state->worst_ms) {
        state->worst_ms = elapsed;
    }

    state->age ++;
    stats->pending = ecs_vec_count(&state->queue);
    stats->instantiated = done;
    stats->worst_frame_ms = state->worst_ms;
}
