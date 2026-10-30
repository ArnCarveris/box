/**
 * @file Transition.c
 * @brief Tests for the transition extract and resolver: the change detection of the game thread,
 * the records it sends and the values the render thread evaluates on the packet clock.
 *
 * The cases render into the null backend and step the world with a fixed delta, never the wall
 * clock. Rendered values are read with the test only accessors of render_packet.h after
 * box_render_sync. Nothing patches instance data yet.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Seconds every frame advances the world, a power of two so the clock is exact */
#define TR_DELTA (0.25f)

/** Create a world that renders into the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* tr_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Delete a world and its null backend.
 *
 * @param world The world
 * @param null The backend
 */
static void tr_fini(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_fini(world);
    box_render_null_free(null);
}

/** Run frames with the fixed delta.
 *
 * @param world The world
 * @param count Frames to run
 */
static void tr_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, TR_DELTA);
    }
}

/** Run one frame and wait until the render thread resolved its packet.
 *
 * @param world The world
 */
static void tr_frame(
    ecs_world_t *world)
{
    ecs_progress(world, TR_DELTA);
    box_render_sync(world);
}

/** Read the rendered Position of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The value the resolver computed for the last packet
 */
static Position tr_position(
    ecs_world_t *world,
    ecs_entity_t e)
{
    Position result = {0};
    box_render_sync(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Position), &result));

    return result;
}

/** Read the rendered Rgb of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The value the resolver computed for the last packet
 */
static Rgb tr_rgb(
    ecs_world_t *world,
    ecs_entity_t e)
{
    Rgb result = {0};
    box_render_sync(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Rgb), &result));

    return result;
}

/** Create an entity at the origin with a transition of its Position.
 *
 * @param world The world
 * @param spec The transition of Position
 * @return The entity
 */
static ecs_entity_t tr_entity(
    ecs_world_t *world,
    const TransitionSpec *spec)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {0, 0, 0});
    box_transition_set(world, result, ecs_id(Position), spec);

    return result;
}

/** Count the records of an op in the last packet.
 *
 * @param null The backend
 * @param op The op
 * @return The records
 */
static int32_t tr_records(
    const box_render_null_t *null,
    box_render_transition_op_t op)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.transitions); i ++) {
        const box_render_transition_record_t *r = ecs_vec_get_t(
            &null->last.transitions, box_render_transition_record_t, i);
        result += r->op == op;
    }

    return result;
}

void Transition_target_on_game_thread(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Position) }} });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    test_flt(ecs_get(world, e, Position)->x, 4);

    int32_t i;
    for (i = 0; i < 5; i ++) {
        tr_frame(world);
        test_flt(ecs_get(world, e, Position)->x, 4);

        int32_t found = 0;
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const Position *p = ecs_field(&it, Position, 0);
            int32_t j;
            for (j = 0; j < it.count; j ++) {
                if (it.entities[j] == e) {
                    test_flt(p[j].x, 4);
                    found ++;
                }
            }
        }

        test_int(found, 1);
        if (i < 3) {
            test_assert(tr_position(world, e).x < 4);
        }
    }

    test_flt(tr_position(world, e).x, 4);
    ecs_query_fini(q);
    tr_fini(world, null);
}

void Transition_values_float(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TimingFunction fns[] = { Linear, EaseIn, EaseOut, EaseInOut };
    ecs_entity_t e[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        e[i] = tr_entity(world, &(TransitionSpec){
            .duration = 1.0f, .timing_function = fns[i] });
    }

    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        ecs_set(world, e[i], Position, {4, -2, 0});
    }

    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        Position v = tr_position(world, e[i]);
        test_flt(v.x, 0);
        test_flt(v.y, 0);
    }

    tr_frame(world);
    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        float t = box_transition_ease(fns[i], 0.5f);
        Position v = tr_position(world, e[i]);
        test_flt(v.x, 4.0f * t);
        test_flt(v.y, -2.0f * t);
        test_flt(v.z, 0);
    }

    tr_frame(world);
    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        Position v = tr_position(world, e[i]);
        test_flt(v.x, 4);
        test_flt(v.y, -2);
    }

    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

void Transition_values_rgb(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TimingFunction fns[] = { Linear, EaseIn, EaseOut, EaseInOut };
    ecs_entity_t e[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        e[i] = ecs_new(world);
        ecs_set(world, e[i], Rgb, {0, 200, 10});
        box_transition_set(world, e[i], ecs_id(Rgb), &(TransitionSpec){
            .duration = 1.0f, .timing_function = fns[i] });
    }

    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        ecs_set(world, e[i], Rgb, {255, 100, 10});
    }

    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        Rgb v = tr_rgb(world, e[i]);
        test_int(v.r, 0);
        test_int(v.g, 200);
        test_int(v.b, 10);
    }

    tr_frame(world);
    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        float t = box_transition_ease(fns[i], 0.5f);
        Rgb v = tr_rgb(world, e[i]);
        test_int(v.r, (uint8_t)(255.0f * t + 0.5f));
        test_int(v.g, (uint8_t)(200.0f - 100.0f * t + 0.5f));
        test_int(v.b, 10);
    }

    tr_frame(world);
    tr_frame(world);
    for (i = 0; i < 4; i ++) {
        Rgb v = tr_rgb(world, e[i]);
        test_int(v.r, 255);
        test_int(v.g, 100);
        test_int(v.b, 10);
    }

    tr_fini(world, null);
}

void Transition_delay_holds(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f, .delay = 0.5f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    test_int(box_render_transition_test_active(world), 1);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 1);
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_spawn_no_animation(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    tr_frame(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {3, 2, 1});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    Position v = tr_position(world, e);
    test_flt(v.x, 3);
    test_flt(v.y, 2);
    test_flt(v.z, 1);
    test_int(box_render_transition_test_active(world), 0);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_CREATE), 1);

    tr_fini(world, null);
}

void Transition_add_transition_no_animation(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    tr_frames(world, 2);

    ecs_set(world, e, Position, {5, 0, 0});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_flt(tr_position(world, e).x, 5);
    test_int(box_render_transition_test_active(world), 0);

    tr_frame(world);
    test_flt(tr_position(world, e).x, 5);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/** Interrupt a transition at its midpoint, reading the values only at the end, so the packets
 * of the frames between are in flight together.
 *
 * @param in_flight Packets in flight
 * @param main_thread Whether the main thread renders
 */
static void tr_interrupt(
    int32_t in_flight,
    bool main_thread)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = in_flight, .main_thread = main_thread });
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frames(world, 2);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frames(world, 1);
    tr_frames(world, 1);
    test_flt(tr_position(world, e).x, 1.5f);

    ecs_set(world, e, Position, {8, 0, 0});
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 1.0f + (8.0f - 1.0f) * 0.5f);

    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 8);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_interrupt_midpoint(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 1);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 2);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 1.5f);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 1);
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 0);

    tr_fini(world, null);
}

void Transition_interrupt_in_flight_default(void) {
    // engine_core/transition
    tr_interrupt(BOX_RENDER_IN_FLIGHT, false);
}

void Transition_interrupt_in_flight_max(void) {
    // engine_core/transition
    tr_interrupt(BOX_RENDER_IN_FLIGHT_MAX, false);
}

void Transition_query_write_starts(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Position) }} });
    tr_frame(world);

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        Position *p = ecs_field(&it, Position, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            p[i].x = 4;
        }
    }

    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 1);
    test_flt(tr_position(world, e).x, 0);
    test_int(box_render_transition_test_active(world), 1);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 1);

    ecs_query_fini(q);
    tr_fini(world, null);
}

void Transition_equal_write_nothing(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_set(world, e, Position, {2, 0, 0});
    tr_frame(world);

    ecs_set(world, e, Position, {2, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);
    test_flt(tr_position(world, e).x, 2);

    tr_fini(world, null);
}

void Transition_remove_transition_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_int(box_render_transition_test_active(world), 1);

    ecs_remove(world, e, Transition);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_DESTROY), 1);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);
    test_flt(ecs_get(world, e, Position)->x, 4);

    tr_fini(world, null);
}

void Transition_remove_component_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_int(box_render_transition_test_active(world), 1);

    ecs_remove(world, e, Position);
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, e) != 0);
    test_int(box_render_transition_test_active(world), 0);
    test_flt(tr_position(world, e).x, 4);

    ecs_set(world, e, Position, {-1, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, -1);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_delete_entity_drops(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_int(box_render_transition_test_active(world), 1);

    ecs_delete(world, e);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_DESTROY), 1);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_node_reuse(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t a = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_set(world, a, Rgb, {10, 20, 30});
    box_transition_set(world, a, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, a, Position, {4, 0, 0});
    ecs_set(world, a, Rgb, {200, 20, 30});
    tr_frame(world);
    uint32_t node = box_render_transition_test_node(world, a);
    test_assert(node != 0);

    ecs_delete(world, a);
    tr_frame(world);

    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {7, 0, 0});
    box_transition_set(world, b, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_int(box_render_transition_test_node(world, b), node);
    test_flt(tr_position(world, b).x, 7);
    test_int(box_render_transition_test_active(world), 0);

    Rgb rgb;
    test_bool(box_render_transition_test_value(world, b, ecs_id(Rgb), &rgb), false);

    tr_fini(world, null);
}

void Transition_remove_entry_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_set(world, e, Rgb, {0, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    ecs_set(world, e, Rgb, {200, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);
    test_int(tr_rgb(world, e).r, 50);

    box_transition_remove(world, e, ecs_id(Position));
    test_assert(box_transition_get(world, e, ecs_id(Position)) == NULL);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(tr_rgb(world, e).r, 100);
    test_int(box_render_transition_test_active(world), 1);

    tr_frames(world, 2);
    test_int(tr_rgb(world, e).r, 200);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_prefab_instances_own_state(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_transition_set(world, prefab, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, b, Position, {0, 0, 0});
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, prefab) != 0);
    test_assert(box_render_transition_test_node(world, a) != 0);
    test_assert(box_render_transition_test_node(world, b) != 0);
    test_assert(box_render_transition_test_node(world, a) !=
        box_render_transition_test_node(world, b));

    ecs_set(world, a, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, a).x, 1);
    test_flt(tr_position(world, b).x, 0);

    box_transition_set(world, a, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    box_transition_remove(world, a, ecs_id(Position));
    tr_frame(world);
    test_flt(tr_position(world, a).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_time_paused(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    ecs_set_time_scale(world, 0);
    tr_frames(world, 8);
    test_flt(tr_position(world, e).x, 1);
    test_int(box_render_transition_test_active(world), 1);

    ecs_set_time_scale(world, 1);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 2);

    tr_fini(world, null);
}

void Transition_time_scale_half(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_set_time_scale(world, 0.5f);
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);

    tr_frames(world, 4);
    test_flt(tr_position(world, e).x, 2);
    test_int(box_render_transition_test_active(world), 1);

    tr_frames(world, 3);
    test_assert(tr_position(world, e).x < 4);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/** Run a sequence of changes and record the rendered x after every frame.
 *
 * @param main_thread Whether the main thread renders
 * @param out Receives the values of 12 frames
 */
static void tr_sequence(
    bool main_thread,
    float *out)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = BOX_RENDER_IN_FLIGHT, .main_thread = main_thread });
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .delay = 0.25f, .timing_function = EaseInOut });
    int32_t i;
    for (i = 0; i < 12; i ++) {
        if (i == 1) {
            ecs_set(world, e, Position, {4, 0, 0});
        } else if (i == 4) {
            ecs_set(world, e, Position, {-2, 1, 0});
        }

        tr_frame(world);
        out[i] = tr_position(world, e).x;
    }

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_bool(status->threaded, !main_thread);
    tr_fini(world, null);
}

void Transition_main_thread_same_values(void) {
    // engine_core/transition
    float threaded[12], main_thread[12];
    tr_sequence(false, threaded);
    tr_sequence(true, main_thread);

    int32_t i;
    for (i = 0; i < 12; i ++) {
        test_flt(main_thread[i], threaded[i]);
    }

    test_assert(threaded[5] != threaded[11]);
    test_flt(threaded[11], -2);
}

void Transition_no_transition_no_records(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {0, 0, 0});
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Rgb, {(uint8_t)(i * 10), 0, 0});
        tr_frame(world);
        test_int(box_render_transition_test_active(world), 0);
    }

    test_int(null->packets, 6);
    test_int(null->transitions, 0);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    test_int(box_render_transition_test_node(world, e), 0);

    ecs_entity_t q = ecs_lookup(world, "box.render.RenderTransitions");
    test_assert(q != 0);
    const ecs_query_t *query = ecs_query_get(world, q);
    test_assert(query != NULL);
    test_int(ecs_query_count(query).tables, 0);

    tr_fini(world, null);
}
