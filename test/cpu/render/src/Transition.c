/**
 * @file Transition.c
 * @brief Tests for the transition extract and resolver: the change detection of the game thread,
 * the records it sends and the values the render thread evaluates on the packet clock.
 *
 * The cases render into the null backend and step the world with a fixed delta, never the wall
 * clock. Rendered values are read after box_render_sync with the test only accessors of
 * render_packet.h, rendered transforms from the patched copy of the last packet.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"
#include "transition_test.h"
#include "spatial.h"

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

/** Interrupt a transition twice while the ring is held, so the packets of the frames between,
 * two of them with a start record, are queued together when the resolver sees the interrupts.
 *
 * The held ring accepts packets until all of them are in flight, so the frames it holds stay
 * at most in_flight. RenderStatus is read as the submit of the last frame wrote it, since
 * box_render_sync renders the held packets.
 *
 * @param in_flight Packets in flight
 * @param held Frames the ring holds, from 2 to 4 and at most in_flight
 */
static void tr_interrupt(
    int32_t in_flight,
    int32_t held)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_singleton_set(world, RenderSettings, { .in_flight = in_flight });
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frames(world, 2);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    box_render_pause(world, true);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, TR_DELTA);
    ecs_set(world, e, Position, {8, 0, 0});
    int32_t i;
    for (i = 1; i < held; i ++) {
        ecs_progress(world, TR_DELTA);
    }

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_bool(status->threaded, true);
    test_assert(status->submitted - status->rendered > 1);
    test_int(status->submitted - status->rendered, held);

    box_render_pause(world, false);
    box_render_sync(world);
    test_flt(tr_position(world, e).x, 1.5f + (8.0f - 1.5f) * 0.25f * (float)(held - 2));
    test_int(box_render_transition_test_active(world), 1);

    tr_frames(world, 6 - held);
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
    tr_interrupt(BOX_RENDER_IN_FLIGHT, BOX_RENDER_IN_FLIGHT);
}

void Transition_interrupt_in_flight_max(void) {
    // engine_core/transition
    tr_interrupt(BOX_RENDER_IN_FLIGHT_MAX, 4);
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
    box_render_sync(world);
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
    box_render_sync(world);
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
    box_render_sync(world);
    test_int(box_render_transition_test_active(world), 1);

    ecs_delete(world, e);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_DESTROY), 1);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_disable_entity_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 2.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 5);
    test_flt(tr_position(world, e).x, 2);
    test_int(box_render_transition_test_active(world), 1);

    ecs_enable(world, e, false);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_DESTROY), 1);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);
    tr_frame(world);

    ecs_enable(world, e, true);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_CREATE), 1);
    test_assert(box_render_transition_test_node(world, e) != 0);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);
    test_flt(ecs_get(world, e, Position)->x, 4);

    tr_fini(world, null);
}

void Transition_disabled_write_no_animation(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frames(world, 2);

    ecs_enable(world, e, false);
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_node(world, e), 0);

    ecs_enable(world, e, true);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

void Transition_enable_animates_again(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f, .delay = 0.5f });
    ecs_set(world, e, Rgb, {0, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    uint32_t node = box_render_transition_test_node(world, e);
    test_assert(node != 0);

    ecs_set(world, e, Position, {4, 0, 0});
    ecs_set(world, e, Rgb, {200, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 0);
    test_int(tr_rgb(world, e).r, 50);
    test_assert(box_render_transition_test_active(world) != 0);

    ecs_enable(world, e, false);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);
    ecs_remove(world, e, Rgb);

    ecs_enable(world, e, true);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, e), node);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);
    Rgb rgb;
    test_bool(box_render_transition_test_value(world, e, ecs_id(Rgb), &rgb), false);

    ecs_set(world, e, Position, {8, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 1);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 1);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 1);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 5);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 6);
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 8);
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

void Transition_clone_gets_own_node(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t a = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, a) != 0);

    ecs_entity_t b = ecs_clone(world, 0, a, true);
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, b) != 0);
    test_assert(box_render_transition_test_node(world, a) !=
        box_render_transition_test_node(world, b));

    ecs_set(world, a, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, a).x, 1);
    test_flt(tr_position(world, b).x, 0);

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

    ecs_entity_t q = ecs_lookup(world, "box.render.Transitions");
    test_assert(q != 0);
    const ecs_query_t *query = ecs_query_get(world, q);
    test_assert(query != NULL);
    test_int(ecs_query_count(query).tables, 0);

    tr_fini(world, null);
}

void Transition_spec_change_keeps_running(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 2);

    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 2.0f });
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_flt(tr_position(world, e).x, 3);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 3.5f);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 3);

    tr_fini(world, null);
}

void Transition_new_map_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t source = ecs_new(world);
    box_transition_set(world, source, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });

    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    ecs_set(world, e, Rgb, {0, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    ecs_set(world, e, Rgb, {200, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);
    test_int(tr_rgb(world, e).r, 50);

    ecs_set_id(world, e, ecs_id(Transition), sizeof(Transition),
        ecs_get(world, source, Transition));
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

void Transition_invalid_entry_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    int level = ecs_log_set_level(-4);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = -1.0f });
    ecs_log_set_level(level);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);

    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    test_int(box_render_transition_test_active(world), 0);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 2);

    tr_fini(world, null);
}

void Transition_active_agrees(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 0.5f, .delay = 0.25f });
    ecs_entity_t d = tr_entity(world, &(TransitionSpec){ .delay = 0.5f });
    tr_frame(world);
    const box_render_transitions_t *s = box_render_transitions_get(world);
    uint32_t node_e = box_render_transition_test_node(world, e);
    uint32_t node_d = box_render_transition_test_node(world, d);
    int32_t i, active = 0;
    for (i = 0; i < 16; i ++) {
        if (i == 1 || i == 3 || i == 9) {
            ecs_set(world, e, Position, {(float)i, 0, 0});
        }

        if (i == 2) {
            ecs_set(world, d, Position, {1, 0, 0});
        }

        tr_frame(world);
        bool active_e = box_render_transitions_active(
            s, node_e, BOX_RENDER_TRANSITION_POSITION, null->time);
        bool active_d = box_render_transitions_node_active(s, node_d, null->time);
        test_int(box_render_transition_test_active(world), active_e + active_d);
        test_bool(active_e, tr_position(world, e).x != ecs_get(world, e, Position)->x);
        test_bool(active_d, tr_position(world, d).x != ecs_get(world, d, Position)->x);
        active += active_e + active_d;
    }

    test_assert(active > 0);
    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

/** Allocations made through the OS API since the counter was installed */
static int32_t tr_allocs;

/** malloc of the OS API before the counter */
static ecs_os_api_malloc_t tr_malloc_prev;

/** realloc of the OS API before the counter */
static ecs_os_api_realloc_t tr_realloc_prev;

/** calloc of the OS API before the counter */
static ecs_os_api_calloc_t tr_calloc_prev;

/** Count an allocation and forward it to the previous malloc.
 *
 * @param size Bytes
 * @return The memory
 */
static void* tr_malloc(
    ecs_size_t size)
{
    tr_allocs ++;

    return tr_malloc_prev(size);
}

/** Count an allocation and forward it to the previous realloc.
 *
 * @param ptr The memory to grow
 * @param size Bytes
 * @return The memory
 */
static void* tr_realloc(
    void *ptr,
    ecs_size_t size)
{
    tr_allocs ++;

    return tr_realloc_prev(ptr, size);
}

/** Count an allocation and forward it to the previous calloc.
 *
 * @param size Bytes
 * @return The zeroed memory
 */
static void* tr_calloc(
    ecs_size_t size)
{
    tr_allocs ++;

    return tr_calloc_prev(size);
}

/** Add a start record of Position to a packet.
 *
 * @param p The packet
 * @param node The node id
 * @param x The target x
 */
static void tr_start(
    box_render_packet_t *p,
    uint32_t node,
    float x)
{
    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_START, node);
    Position v = { x, 0, 0 };
    r->component = BOX_RENDER_TRANSITION_POSITION;
    r->duration = 1.0f;
    r->start = p->time;
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &v, r->value);
}

void Transition_resolver_no_allocation(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    box_render_transition_table_t t;
    box_render_packet_t p;
    box_render_transition_table_init(&t);
    box_render_packet_init(&p);
    uint32_t node;
    for (node = 1; node <= 8; node ++) {
        box_render_packet_transition(&p, BOX_RENDER_TRANSITION_CREATE, node);
        tr_start(&p, node, 0);
    }

    box_render_transition_resolve(&t, &p);
    box_render_packet_reset(&p);
    for (node = 1; node <= 8; node ++) {
        tr_start(&p, node, 4);
    }

    box_render_packet_reset(&p);
    tr_malloc_prev = ecs_os_api.malloc_;
    tr_realloc_prev = ecs_os_api.realloc_;
    tr_calloc_prev = ecs_os_api.calloc_;
    ecs_os_api.malloc_ = tr_malloc;
    ecs_os_api.realloc_ = tr_realloc;
    ecs_os_api.calloc_ = tr_calloc;
    tr_allocs = 0;

    int32_t frame;
    for (frame = 0; frame < 8; frame ++) {
        p.time = frame * (double)TR_DELTA;
        if (frame == 0 || frame == 2) {
            for (node = 1; node <= 8; node ++) {
                tr_start(&p, node, (float)(frame + 4));
            }
        }

        if (frame == 3) {
            box_render_packet_transition(&p, BOX_RENDER_TRANSITION_DESTROY, 8);
            box_render_packet_transition(&p, BOX_RENDER_TRANSITION_CREATE, 8);
            tr_start(&p, 8, 1);
            tr_start(&p, 8, 2);
        }

        box_render_transition_resolve(&t, &p);
        test_assert(box_render_transition_table_active(&t) > 0 || frame > 5);
        box_render_packet_reset(&p);
    }

    ecs_os_api.malloc_ = tr_malloc_prev;
    ecs_os_api.realloc_ = tr_realloc_prev;
    ecs_os_api.calloc_ = tr_calloc_prev;
    test_int(tr_allocs, 0);
    test_int(box_render_transition_table_active(&t), 0);

    Position v;
    test_assert(box_render_transition_table_value(&t, 1, BOX_RENDER_TRANSITION_POSITION, &v));
    test_flt(v.x, 6);
    test_assert(box_render_transition_table_value(&t, 8, BOX_RENDER_TRANSITION_POSITION, &v));
    test_flt(v.x, 2);

    box_render_packet_fini(&p);
    box_render_transition_table_fini(&t);
    ecs_fini(world);
}

/** Compute the transform a shape renders with at a pose, as box_draws_transform writes it.
 *
 * @param world The world
 * @param e The entity, whose shape gives the instance scale
 * @param parent World matrix of the parent, NULL for none
 * @param p Local position
 * @param r Local rotation, NULL for none
 * @param s Local scale, NULL for none
 * @param out Receives the transform
 */
static void tr_pose(
    ecs_world_t *world,
    ecs_entity_t e,
    vec4 *parent,
    const Position *p,
    const Rotation *r,
    const Scale *s,
    box_render_transform_t *out)
{
    WorldTransform wt;
    mat4 local;
    box_transform_local(p, r, s, local);
    if (parent) {
        glm_mul(parent, local, wt.m);
    } else {
        glm_mat4_copy(local, wt.m);
    }

    box_draws_transform(out, &wt, ecs_get(world, e, ShapeMesh));
}

/** Check that two transforms are equal within float rounding.
 *
 * @param a A transform
 * @param b A transform
 */
static void tr_test_transform(
    const box_render_transform_t *a,
    const box_render_transform_t *b)
{
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(isfinite(a->rows[r][c]));
            test_assert(fabsf(a->rows[r][c] - b->rows[r][c]) < 1e-4f);
        }
    }
}

/** Check the transform the shape of an entity renders with at a pose.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param parent World matrix of the parent, NULL for none
 * @param p Local position
 * @param r Local rotation, NULL for none
 * @param s Local scale, NULL for none
 */
static void tr_test_pose(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    vec4 *parent,
    const Position *p,
    const Rotation *r,
    const Scale *s)
{
    box_render_transform_t rendered, expected;
    test_assert(tr_rendered(world, null, e, &rendered));
    tr_pose(world, e, parent, p, r, s, &expected);
    tr_test_transform(&rendered, &expected);
}

/** Get the local matrix of a position.
 *
 * @param x X
 * @param y Y
 * @param z Z
 * @param out Receives the matrix
 */
static void tr_at(
    float x,
    float y,
    float z,
    mat4 out)
{
    box_transform_local(&(Position){x, y, z}, NULL, NULL, out);
}

void Transition_transform_position(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_assert(!tr_batch(world, e)->key.dynamic_transform);

    ecs_set(world, e, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionTransform));
        test_assert(tr_batch(world, e)->key.dynamic_transform);
        tr_test_pose(world, null, e, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

void Transition_disable_drawn_entity(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    ecs_set(world, e, Rgb, {0, 0, 0});
    tr_frames(world, 3);
    test_assert(ecs_has_id(world, e, TransitionTransform));
    test_int(box_render_transition_test_active(world), 1);

    ecs_enable(world, e, false);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, e), 0);
    test_int(box_render_transition_test_active(world), 0);
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    tr_frame(world);

    ecs_enable(world, e, true);
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, e) != 0);
    test_int(box_render_transition_test_active(world), 0);
    test_int(tr_rgb(world, e).r, 0);
    tr_test_static(world, null, e);
    tr_frame(world);
    tr_test_static(world, null, e);

    tr_fini(world, null);
}

void Transition_transform_rotation(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 1);
    ecs_set(world, e, Rotation, {0, 0, 0});
    box_transition_set(world, e, ecs_id(Rotation), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Rotation, {0.4f, (float)GLM_PI_2, 0.2f});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float t = (float)i / 4.0f;
        tr_frame(world);
        tr_test_pose(world, null, e, NULL, &(Position){1, 0, 0},
            &(Rotation){0.4f * t, (float)GLM_PI_2 * t, 0.2f * t}, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

void Transition_transform_scale(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_set(world, e, Scale, {1, 1, 1});
    box_transition_set(world, e, ecs_id(Scale), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Scale, {3, 5, 1});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float t = (float)i / 4.0f;
        tr_frame(world);
        tr_test_pose(world, null, e, NULL, &(Position){0, 0, 0}, NULL,
            &(Scale){1 + 2 * t, 1 + 4 * t, 1});
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

void Transition_transform_end_matches_target(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t plain = tr_shape(world, 0, 0);
    ecs_set(world, e, Rotation, {0, 0, 0});
    ecs_set(world, plain, Rotation, {0, 0, 0});
    ecs_set(world, e, Scale, {1, 1, 1});
    ecs_set(world, plain, Scale, {1, 1, 1});
    TransitionSpec spec = { .duration = 1.0f, .timing_function = EaseInOut };
    box_transition_set(world, e, ecs_id(Position), &spec);
    box_transition_set(world, e, ecs_id(Rotation), &spec);
    box_transition_set(world, e, ecs_id(Scale), &spec);
    tr_frame(world);

    ecs_entity_t both[2] = { e, plain };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_set(world, both[i], Position, {0.3f, -1.7f, 2.9f});
        ecs_set(world, both[i], Rotation, {0.71f, -2.3f, 0.13f});
        ecs_set(world, both[i], Scale, {0.37f, 1.9f, 3.1f});
    }

    for (i = 0; i < 4; i ++) {
        box_render_transform_t rendered, plain_rendered, target;
        tr_frame(world);
        test_assert(tr_rendered(world, null, e, &rendered));
        test_assert(!tr_rendered(world, null, plain, &plain_rendered));
        tr_target(world, plain, &target);
        test_assert(!ecs_os_memcmp_t(&plain_rendered, &target, box_render_transform_t));
        test_assert(ecs_os_memcmp_t(&rendered, &target, box_render_transform_t) != 0);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    box_render_transform_t a, b;
    test_assert(!tr_rendered(world, null, e, &a));
    test_assert(!tr_rendered(world, null, plain, &b));
    test_assert(!ecs_os_memcmp_t(&a, &b, box_render_transform_t));
    test_assert(!ecs_os_memcmp_t(ecs_get(world, e, WorldTransform),
        ecs_get(world, plain, WorldTransform), WorldTransform));
    tr_fini(world, null);
}

/** Check that a child follows a parent whose Position transitions, for a ChildOf or a Parent
 * hierarchy.
 *
 * @param non_fragmenting Whether the child has a Parent component instead of a ChildOf pair
 */
static void tr_child_follows(
    bool non_fragmenting)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child;
    if (non_fragmenting) {
        child = ecs_new(world);
        ecs_set(world, child, EcsParent, {parent});
        ecs_set(world, child, Box, {1, 1, 1});
    } else {
        child = tr_shape(world, parent, 0);
    }

    ecs_set(world, child, Position, {0, 1, 0});
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, parent, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        mat4 m;
        tr_frame(world);
        test_assert(ecs_has_id(world, child, TransitionTransform));
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, child, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 4);
    tr_fini(world, null);
}

void Transition_transform_child_childof(void) {
    // engine_core/transition
    tr_child_follows(false);
}

void Transition_transform_child_parent(void) {
    // engine_core/transition
    tr_child_follows(true);
}

void Transition_transform_child_surface(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 1, 0});
    ecs_set(world, child, EllipsoidShell, {
        .radius = {1, 2, 0.5f},
        .longitude_start = 0, .longitude_end = 2 * (float)GLM_PI,
        .latitude_start = -(float)GLM_PI_2, .latitude_end = (float)GLM_PI_2 });
    ecs_set(world, child, Rgb, {20, 100, 200});
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_assert(ecs_get(world, child, ShapeMesh)->surface.p[0][0] != 0.0f);

    ecs_set(world, parent, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        mat4 m;
        box_render_surface_t surface;
        tr_frame(world);
        test_assert(ecs_has_id(world, child, TransitionTransform));
        box_draw_batch_t *b = tr_batch(world, child);
        test_assert(b->key.dynamic_transform);
        box_draws_surface(&surface, ecs_get(world, child, ShapeMesh));
        test_assert(surface.p[0][0] != 0.0f);
        test_assert(!ecs_os_memcmp_t(box_draws_slot_surface(b,
            ecs_get(world, child, DrawSlot)->slot), &surface, box_render_surface_t));
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, child, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 4);
    tr_fini(world, null);
}

void Transition_transform_parent_of_node(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t root = tr_shape(world, 0, 0);
    ecs_set(world, root, Position, {1, 2, 0});
    ecs_set(world, root, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_set(world, root, Scale, {2, 2, 2});
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, EcsParent, {root});
    ecs_set(world, node, Box, {1, 1, 1});
    ecs_set(world, node, Position, {0, 0, 0});
    ecs_entity_t leaf = ecs_new(world);
    ecs_set(world, leaf, EcsParent, {node});
    ecs_set(world, leaf, Box, {1, 1, 1});
    ecs_set(world, leaf, Position, {0, 1, 0});
    box_transition_set(world, node, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    mat4 r;
    box_transform_local(&(Position){1, 2, 0}, &(Rotation){0, (float)GLM_PI_2, 0},
        &(Scale){2, 2, 2}, r);
    ecs_set(world, node, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        mat4 n, rn;
        tr_frame(world);
        test_assert(ecs_has_id(world, node, TransitionTransform));
        test_assert(ecs_has_id(world, leaf, TransitionTransform));
        test_assert(!ecs_has_id(world, root, TransitionTransform));
        tr_at((float)i, 0, 0, n);
        glm_mul(r, n, rn);
        tr_test_pose(world, null, node, r, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, leaf, rn, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, root);
    tr_test_static(world, null, node);
    tr_test_static(world, null, leaf);
    tr_fini(world, null);
}

void Transition_transform_reparent_into(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t other = tr_shape(world, 0, 0);
    ecs_set(world, other, Position, {0, 1, 0});
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    test_assert(!tr_batch(world, other)->key.dynamic_transform);

    ecs_set(world, parent, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        tr_frame(world);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_static(world, null, other);
    }

    ecs_add_pair(world, other, EcsChildOf, parent);
    for (i = 2; i < 4; i ++) {
        mat4 m;
        tr_frame(world);
        test_assert(ecs_has_id(world, other, TransitionTransform));
        test_assert(tr_batch(world, other)->key.dynamic_transform);
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, other, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, other);
    test_flt(ecs_get(world, other, WorldTransform)->m[3][0], 4);
    test_flt(ecs_get(world, other, WorldTransform)->m[3][1], 1);
    tr_frame(world);
    tr_test_static(world, null, other);
    tr_fini(world, null);
}

void Transition_transform_reparent_out(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, parent, 0);
    ecs_set(world, child, Position, {0, 1, 0});
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, parent, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        mat4 m;
        tr_frame(world);
        test_assert(ecs_has_id(world, child, TransitionTransform));
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, child, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    ecs_remove_pair(world, child, EcsChildOf, parent);
    for (i = 2; i < 4; i ++) {
        box_render_transform_t target;
        tr_frame(world);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        test_assert(tr_batch(world, parent)->key.dynamic_transform);
        tr_test_static(world, null, child);
        test_assert(!tr_batch(world, child)->key.dynamic_transform);
        tr_target(world, child, &target);
        test_flt(target.rows[1][3], 1);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 0);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][1], 1);
    tr_frame(world);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void Transition_transform_nested(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, parent, 0);
    ecs_entity_t leaf = tr_shape(world, child, 1);
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    box_transition_set(world, child, ecs_id(Position), &(TransitionSpec){ .duration = 0.5f });
    tr_frame(world);

    ecs_set(world, parent, Position, {4, 0, 0});
    ecs_set(world, child, Position, {0, 2, 0});
    float child_y[4] = { 0, 1, 2, 2 };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        mat4 p, c, pc;
        tr_frame(world);
        tr_at((float)i, 0, 0, p);
        tr_at(0, child_y[i], 0, c);
        glm_mul(p, c, pc);
        tr_test_pose(world, null, parent, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, child, p, &(Position){0, child_y[i], 0}, NULL, NULL);
        tr_test_pose(world, null, leaf, pc, &(Position){1, 0, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    tr_test_static(world, null, leaf);
    tr_fini(world, null);
}

void Transition_transform_scale_zero(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, parent, 0);
    ecs_set(world, parent, Scale, {1, 1, 1});
    ecs_set(world, child, Position, {0, 1, 0});
    box_transition_set(world, parent, ecs_id(Scale), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, parent, Scale, {0, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float s = 1.0f - (float)i / 4.0f;
        mat4 m;
        tr_frame(world);
        box_transform_local(&(Position){0, 0, 0}, NULL, &(Scale){s, s, s}, m);
        tr_test_pose(world, null, child, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, child);

    ecs_set(world, parent, Scale, {1, 1, 1});
    for (i = 0; i < 4; i ++) {
        float s = (float)i / 4.0f;
        mat4 m;
        tr_frame(world);
        box_transform_local(&(Position){0, 0, 0}, NULL, &(Scale){s, s, s}, m);
        tr_test_pose(world, null, child, m, &(Position){0, 1, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void Transition_transform_static_rekeys(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .compute = true});
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    box_draw_batch_t *b = tr_batch(world, e);
    test_assert(!b->key.dynamic_transform);
    test_assert(b->key.bake);

    ecs_set(world, e, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        b = tr_batch(world, e);
        test_assert(b->key.dynamic_transform);
        test_assert(!b->key.bake);
    }

    tr_frame(world);
    b = tr_batch(world, e);
    test_assert(!b->key.dynamic_transform);
    test_assert(b->key.bake);
    tr_test_static(world, null, e);
    test_flt(box_draws_slot_transform(b, ecs_get(world, e, DrawSlot)->slot)->rows[0][3], 4);

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

void Transition_transform_delete_entity(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, e, 1);
    ecs_entity_t other = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    box_transition_set(world, other, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    ecs_set(world, other, Position, {0, 4, 0});
    tr_frame(world);
    tr_frame(world);
    ecs_delete(world, e);
    test_assert(!ecs_is_alive(world, child));

    int32_t i;
    for (i = 2; i < 4; i ++) {
        tr_frame(world);
        tr_test_pose(world, null, other, NULL, &(Position){0, (float)i, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, other);
    test_int(box_render_transition_test_active(world), 0);
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_POSE), 0);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SLOT), 0);
    tr_fini(world, null);
}

void Transition_transform_delete_child(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t a = tr_shape(world, e, 1);
    ecs_entity_t b = tr_shape(world, e, 2);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    tr_frame(world);
    ecs_delete(world, a);

    int32_t i;
    for (i = 2; i < 4; i ++) {
        mat4 m;
        tr_frame(world);
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, e, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, b, m, &(Position){2, 0, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_test_static(world, null, b);
    tr_fini(world, null);
}

void Transition_transform_batch_moved(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, e, 1);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frame(world);
    tr_frame(world);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t child_batch = ecs_get(world, child, DrawSlot)->batch;
    ecs_add(world, e, NoShadow);
    ecs_set(world, child, BackFaceMode, {BackFacePlain});

    int32_t i;
    for (i = 2; i < 4; i ++) {
        mat4 m;
        tr_frame(world);
        test_assert(ecs_get(world, e, DrawSlot)->batch != batch);
        test_assert(ecs_get(world, child, DrawSlot)->batch != child_batch);
        tr_at((float)i, 0, 0, m);
        tr_test_pose(world, null, e, NULL, &(Position){(float)i, 0, 0}, NULL, NULL);
        tr_test_pose(world, null, child, m, &(Position){1, 0, 0}, NULL, NULL);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void Transition_transform_prefab_instances(void) {
    // engine_core/transition
    enum { COUNT = 64 };
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {10, 200, 30});
    box_transition_set(world, prefab, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t instances[COUNT];
    int32_t i, j;
    for (i = 0; i < COUNT; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i, 0, 0});
    }

    tr_frame(world);
    for (i = 0; i < COUNT; i ++) {
        ecs_set(world, instances[i], Position, {(float)i, 4, 0});
    }

    for (j = 0; j < 4; j ++) {
        tr_frame(world);
        test_int(box_render_transition_test_active(world), COUNT);
        test_assert(!ecs_has_id(world, prefab, TransitionTransform));
        for (i = 0; i < COUNT; i ++) {
            tr_test_pose(world, null, instances[i], NULL, &(Position){(float)i, (float)j, 0},
                NULL, NULL);
        }
    }

    test_int(tr_records(null, BOX_RENDER_TRANSITION_POSE), COUNT);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SLOT), COUNT);
    tr_frame(world);
    for (i = 0; i < COUNT; i ++) {
        tr_test_static(world, null, instances[i]);
    }

    tr_fini(world, null);
}

void Transition_transform_targets_on_game_thread(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, e, 0);
    ecs_set(world, child, Position, {0, 3, 0});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        vec3 min, max;
        tr_frame(world);
        test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 4);
        test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 4);
        test_flt(ecs_get(world, child, WorldTransform)->m[3][1], 3);
        test_assert(box_spatial_bounds(world, e, NULL, min, max));
        test_flt((min[0] + max[0]) * 0.5f, 4);
        test_assert(box_spatial_bounds(world, child, NULL, min, max));
        test_flt((min[0] + max[0]) * 0.5f, 4);

        box_spatial_hit_t hit = {0};
        box_spatial_ray_t ray = { .origin = {4, 0, 10}, .dir = {0, 0, -1} };
        test_assert(box_spatial_ray(world, &ray, &hit));
        test_uint(hit.entity, e);
        if (i < 3) {
            box_spatial_ray_t old = { .origin = {(float)i, 0, 10}, .dir = {0, 0, -1} };
            hit = (box_spatial_hit_t){0};
            test_assert(!box_spatial_ray(world, &old, &hit));
        }
    }

    tr_fini(world, null);
}

/** Check the glyphs of a text at a pose, as box_draws_glyph writes them.
 *
 * @param world The world
 * @param null The backend
 * @param e The text
 * @param p Local position
 * @param moving Whether the glyphs are patched in a run upload, else in a static batch
 */
static void tr_test_glyphs(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    const Position *p,
    bool moving)
{
    box_render_draws_t *d = box_draws_state(world);
    const TextSlots *ts = ecs_get(world, e, TextSlots);
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    int32_t i, r, count = ecs_vec_count(&ts->slots);
    mat4 m;
    test_assert(count > 0);
    test_int(ecs_vec_count(&tg->glyphs), count);
    box_transform_local(p, NULL, NULL, m);
    for (i = 0; i < count; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        const box_text_glyph_t *g = ecs_vec_get_t(&tg->glyphs, box_text_glyph_t, i);
        box_render_transform_t rendered, expected;
        test_bool(tr_transform_slot(world, null, box_draws_batch(d, at->batch), at->slot,
            &rendered), moving);
        for (r = 0; r < 3; r ++) {
            expected.rows[r][0] = m[0][r] * g->width;
            expected.rows[r][1] = m[1][r] * g->height;
            expected.rows[r][2] = m[2][r];
            expected.rows[r][3] = m[0][r] * g->x + m[1][r] * g->y + m[3][r];
        }

        tr_test_transform(&rendered, &expected);
    }
}

void Transition_transform_text_and_2d(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t text = ecs_new(world);
    ecs_set(world, text, Position, {0, 1, 0});
    ecs_set(world, text, Text, {
        .value = "move", .size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")
    });
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {10, 20});
    TransitionSpec spec = { .duration = 1.0f };
    box_transition_set(world, text, ecs_id(Position), &spec);
    box_transition_set(world, rect, ecs_id(Position), &spec);
    tr_frame(world);
    tr_frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextGlyphs)->glyphs), 4);

    ecs_set(world, text, Position, {40, 1, 0});
    ecs_set(world, rect, Position, {0, 80, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(ecs_has_id(world, text, TransitionTransform));
        test_assert(ecs_has_id(world, rect, TransitionTransform));
        tr_test_glyphs(world, null, text, &(Position){10.0f * (float)i, 1, 0}, true);
        tr_test_pose(world, null, rect, NULL, &(Position){0, 20.0f * (float)i, 0}, NULL, NULL);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, text, TransitionTransform));
    tr_test_static(world, null, rect);
    tr_test_glyphs(world, null, text, &(Position){40, 1, 0}, false);
    tr_fini(world, null);
}

void Transition_record_sizes(void) {
    // engine_core/transition
    test_int(ECS_SIZEOF(box_render_transform_t), 48);
    test_int(ECS_SIZEOF(box_render_mesh_shading_t), 16);
    test_int(ECS_SIZEOF(box_render_shading_t), 64);
    test_int(ECS_SIZEOF(box_render_material_t), 192);
}

void Transition_idle_no_work(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_entity_t shapes[64];
    int32_t i, frame;
    for (i = 0; i < 64; i ++) {
        shapes[i] = ecs_new(world);
        if (i % 2) {
            ecs_add(world, shapes[i], DynamicTransform);
        }

        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
        ecs_set(world, shapes[i], Rgb, {200, 100, 50});
    }

    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 8, 0});
    ecs_set(world, rect, Rect, {10, 20});
    for (frame = 0; frame < 4; frame ++) {
        for (i = 0; i < 64; i ++) {
            ecs_set(world, shapes[i], Position, {(float)i, (float)frame, 0});
            ecs_set(world, shapes[i], Rgb, {(uint8_t)(frame * 10), 100, 50});
        }

        tr_frame(world);
        test_int(box_render_transition_test_active(world), 0);
        test_int(box_render_transitions_material_visited(s), 0);
        test_int(ecs_vec_count(&null->last.transitions), 0);
    }

    test_int(null->transitions, 0);
    for (i = 0; i < 64; i ++) {
        test_int(box_render_transition_test_node(world, shapes[i]), 0);
        test_assert(!ecs_has_id(world, shapes[i], TransitionTransform));
        test_assert(!ecs_has_id(world, shapes[i], TransitionMaterial));
        test_assert(!ecs_has_id(world, shapes[i], TransitionOpacity));
    }

    ecs_entity_t q = ecs_lookup(world, "box.render.Transitions");
    test_assert(q != 0);
    test_int(ecs_query_count(ecs_query_get(world, q)).tables, 0);

    ecs_entity_t extract = ecs_lookup(world, "box.render.extract.Transitions");
    ecs_entity_t slots = ecs_lookup(world, "box.render.extract.TransitionSlots");
    test_assert(extract != 0);
    test_assert(slots != 0);
    box_render_transition_table_t t;
    box_render_packet_t p;
    box_render_transition_table_init(&t);
    box_render_packet_init(&p);
    tr_malloc_prev = ecs_os_api.malloc_;
    tr_realloc_prev = ecs_os_api.realloc_;
    tr_calloc_prev = ecs_os_api.calloc_;
    ecs_os_api.malloc_ = tr_malloc;
    ecs_os_api.realloc_ = tr_realloc;
    ecs_os_api.calloc_ = tr_calloc;
    tr_allocs = 0;
    for (frame = 0; frame < 8; frame ++) {
        ecs_run(world, extract, 0, NULL);
        ecs_run(world, slots, 0, NULL);
        box_render_transition_resolve(&t, &p);
    }

    ecs_os_api.malloc_ = tr_malloc_prev;
    ecs_os_api.realloc_ = tr_realloc_prev;
    ecs_os_api.calloc_ = tr_calloc_prev;
    test_int(tr_allocs, 0);
    test_int(box_render_transition_table_active(&t), 0);
    test_int(ecs_vec_count(&p.transitions), 0);
    box_render_packet_fini(&p);
    box_render_transition_table_fini(&t);

    tr_fini(world, null);
}

/** Handles of the custom curve of the CubicBezier cases, a CSS "emphasized decelerate" */
static const TransitionBezier tr_bezier = { 0.2f, 0.0f, 0.0f, 1.0f };

/** Eased progress of a CubicBezier.
 *
 * @param bezier The handles
 * @param t Linear progress
 * @return The eased progress
 */
static float tr_ease(
    const TransitionBezier *bezier,
    float t)
{
    return box_transition_ease_bezier(CubicBezier, bezier, t);
}

/** Get the last start record of the last packet.
 *
 * @param null The backend
 * @return The record, NULL when the packet has none
 */
static const box_render_transition_record_t* tr_last_start(
    const box_render_null_t *null)
{
    const box_render_transition_record_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.transitions); i ++) {
        const box_render_transition_record_t *r = ecs_vec_get_t(
            &null->last.transitions, box_render_transition_record_t, i);
        if (r->op == BOX_RENDER_TRANSITION_START) {
            result = r;
        }
    }

    return result;
}

/* A move along {0.2, 0, 0, 1} sends its handles in the start record and renders the start, the
 * eased values at a quarter, half and three quarters, which are ahead of linear, and the end */
void Transition_bezier_values(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);

    ecs_set(world, e, Position, {4, -2, 0});
    tr_frame(world);
    const box_render_transition_record_t *r = tr_last_start(null);
    test_assert(r != NULL);
    test_int(r->timing_function, CubicBezier);
    test_flt(r->bezier.x1, 0.2f);
    test_flt(r->bezier.y1, 0);
    test_flt(r->bezier.x2, 0);
    test_flt(r->bezier.y2, 1);
    Position v = tr_position(world, e);
    test_flt(v.x, 0);
    test_flt(v.y, 0);

    int32_t i;
    for (i = 1; i < 4; i ++) {
        tr_frame(world);
        float t = tr_ease(&tr_bezier, (float)i / 4.0f);
        v = tr_position(world, e);
        test_flt(v.x, 4.0f * t);
        test_flt(v.y, -2.0f * t);
        test_flt(v.z, 0);
    }

    test_assert(tr_ease(&tr_bezier, 0.5f) > 0.5f);
    test_assert(tr_ease(&tr_bezier, 0.5f) < 1.0f);

    tr_frame(world);
    v = tr_position(world, e);
    test_flt(v.x, 4);
    test_flt(v.y, -2);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/* The script of the docs parses the handles into the spec, and Opacity renders 1 at the start,
 * the eased value in the middle and 0 at the end */
void Transition_bezier_script(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    test_assert(ecs_script_run(world, "panel",
        "using box.material\n"
        "using box.transition\n"
        "panel {\n"
        "  Opacity: {1}\n"
        "  Transition: [Opacity: {duration: 0.3, timing_function: CubicBezier, "
            "bezier: {0.2, 0, 0, 1}}]\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "panel");
    test_assert(e != 0);
    const TransitionSpec *spec = box_transition_get(world, e, ecs_id(Opacity));
    test_assert(spec != NULL);
    test_int(spec->timing_function, CubicBezier);
    test_flt(spec->duration, 0.3f);
    test_flt(spec->bezier.x1, 0.2f);
    test_flt(spec->bezier.y1, 0);
    test_flt(spec->bezier.x2, 0);
    test_flt(spec->bezier.y2, 1);
    test_bool(box_transition_valid(world, ecs_id(Opacity), spec), true);
    tr_frame(world);

    ecs_set(world, e, Opacity, {0.0f});
    tr_frame(world);
    Opacity o = {0};
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    test_flt(o.value, 1);

    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    float t = tr_ease(&tr_bezier, (float)(0.25 / (double)0.3f));
    test_assert(fabsf(o.value - (1.0f - t)) < 1e-5f);
    test_assert(o.value > 0.0f);

    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    test_flt(o.value, 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/* A CubicBezier with the handles of EaseInOut renders the same Position and Rgb every frame as
 * the preset, and the ease functions agree for every preset */
void Transition_bezier_matches_preset(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t preset = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    ecs_entity_t custom = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier,
        .bezier = { 0.42f, 0.0f, 0.58f, 1.0f } });
    ecs_entity_t rgb[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        rgb[i] = ecs_new(world);
        ecs_set(world, rgb[i], Rgb, {0, 200, 10});
    }

    box_transition_set(world, rgb[0], ecs_id(Rgb), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    box_transition_set(world, rgb[1], ecs_id(Rgb), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier,
        .bezier = { 0.42f, 0.0f, 0.58f, 1.0f } });
    tr_frame(world);

    ecs_set(world, preset, Position, {4, -2, 1});
    ecs_set(world, custom, Position, {4, -2, 1});
    ecs_set(world, rgb[0], Rgb, {255, 100, 10});
    ecs_set(world, rgb[1], Rgb, {255, 100, 10});
    for (i = 0; i < 6; i ++) {
        tr_frame(world);
        Position a = tr_position(world, preset);
        Position b = tr_position(world, custom);
        test_flt(a.x, b.x);
        test_flt(a.y, b.y);
        test_flt(a.z, b.z);
        Rgb ca = tr_rgb(world, rgb[0]);
        Rgb cb = tr_rgb(world, rgb[1]);
        test_int(ca.r, cb.r);
        test_int(ca.g, cb.g);
        test_int(ca.b, cb.b);
    }

    for (i = 0; i <= 16; i ++) {
        float t = (float)i / 16.0f;
        test_flt(box_transition_ease(EaseInOut, t),
            tr_ease(&(TransitionBezier){ 0.42f, 0.0f, 0.58f, 1.0f }, t));
        test_flt(box_transition_ease(EaseIn, t),
            tr_ease(&(TransitionBezier){ 0.42f, 0.0f, 1.0f, 1.0f }, t));
        test_flt(box_transition_ease(EaseOut, t),
            tr_ease(&(TransitionBezier){ 0.0f, 0.0f, 0.58f, 1.0f }, t));
        test_flt(box_transition_ease_bezier(EaseInOut, &tr_bezier, t),
            box_transition_ease(EaseInOut, t));
    }

    test_flt(box_transition_ease_bezier(CubicBezier, NULL, 0.25f), 0.25f);
    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

/* A y above 1 moves Position past its target and a y below 0 back past its start, and both end
 * on the target; the resolver extrapolates for progress outside 0..1 */
void Transition_bezier_overshoot_float(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier over = { 0.3f, 1.8f, 0.7f, 1.8f };
    TransitionBezier under = { 0.3f, -0.8f, 0.7f, -0.8f };
    ecs_entity_t e[2];
    e[0] = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = over });
    e[1] = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = under });
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e[0], ecs_id(Position))), true);
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e[1], ecs_id(Position))), true);
    tr_frame(world);

    ecs_set(world, e[0], Position, {4, 0, 0});
    ecs_set(world, e[1], Position, {4, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e[0]).x, 0);
    test_flt(tr_position(world, e[1]).x, 0);

    tr_frame(world);
    tr_frame(world);
    float to = tr_ease(&over, 0.5f);
    float tu = tr_ease(&under, 0.5f);
    test_assert(to > 1.0f);
    test_assert(tu < 0.0f);
    Position a = tr_position(world, e[0]);
    Position b = tr_position(world, e[1]);
    test_flt(a.x, 4.0f * to);
    test_assert(a.x > 4.0f);
    test_flt(b.x, 4.0f * tu);
    test_assert(b.x < 0.0f);
    test_int(box_render_transition_test_active(world), 2);

    tr_frames(world, 2);
    test_flt(tr_position(world, e[0]).x, 4);
    test_flt(tr_position(world, e[1]).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    uint8_t from[BOX_RENDER_TRANSITION_VALUE] = {0}, target[BOX_RENDER_TRANSITION_VALUE] = {0};
    Position out = {0};
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &(Position){0, 0, 0}, from);
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &(Position){4, 2, 0}, target);
    uint8_t mixed[BOX_RENDER_TRANSITION_VALUE];
    box_render_transition_mix(BOX_RENDER_TRANSITION_POSITION, from, target, 1.5f, mixed);
    ecs_os_memcpy(&out, mixed, ECS_SIZEOF(Position));
    test_flt(out.x, 6);
    test_flt(out.y, 3);
    box_render_transition_mix(BOX_RENDER_TRANSITION_POSITION, from, target, -0.5f, mixed);
    ecs_os_memcpy(&out, mixed, ECS_SIZEOF(Position));
    test_flt(out.x, -2);
    test_flt(out.y, -1);

    tr_fini(world, null);
}

/* An overshooting Rgb clamps a field at 255 or 0 when it is rounded, while a field with room
 * passes its target or goes back past its start */
void Transition_bezier_overshoot_rgb(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier beziers[2] = {
        { 0.3f, 1.8f, 0.7f, 1.8f },
        { 0.3f, -0.8f, 0.7f, -0.8f }
    };
    ecs_entity_t e[2];
    int32_t i, f;
    for (i = 0; i < 2; i ++) {
        e[i] = ecs_new(world);
        ecs_set(world, e[i], Rgb, {0, 200, 10});
        box_transition_set(world, e[i], ecs_id(Rgb), &(TransitionSpec){
            .duration = 1.0f, .timing_function = CubicBezier, .bezier = beziers[i] });
    }

    tr_frame(world);
    for (i = 0; i < 2; i ++) {
        ecs_set(world, e[i], Rgb, {255, 100, 10});
    }

    tr_frame(world);
    for (f = 1; f < 4; f ++) {
        tr_frame(world);
        for (i = 0; i < 2; i ++) {
            float t = tr_ease(&beziers[i], (float)f / 4.0f);
            float r = glm_clamp(255.0f * t, 0.0f, 255.0f);
            float g = glm_clamp(200.0f - 100.0f * t, 0.0f, 255.0f);
            Rgb v = tr_rgb(world, e[i]);
            test_int(v.r, (uint8_t)(r + 0.5f));
            test_int(v.g, (uint8_t)(g + 0.5f));
            test_int(v.b, 10);
        }

        if (f == 2) {
            test_assert(tr_ease(&beziers[0], 0.5f) > 1.0f);
            test_assert(tr_ease(&beziers[1], 0.5f) < 0.0f);
            Rgb over = tr_rgb(world, e[0]);
            Rgb under = tr_rgb(world, e[1]);
            test_int(over.r, 255);
            test_assert(over.g < 100);
            test_int(under.r, 0);
            test_assert(under.g > 200);
        }
    }

    tr_frames(world, 2);
    for (i = 0; i < 2; i ++) {
        Rgb v = tr_rgb(world, e[i]);
        test_int(v.r, 255);
        test_int(v.g, 100);
        test_int(v.b, 10);
    }

    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

/* Handles with an x outside 0..1 or that are not finite make an entry invalid, from C and from
 * script: the change snaps without a start record. The other timing functions ignore the handles,
 * and a valid curve set afterwards animates again */
void Transition_bezier_invalid_x(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier invalid[] = {
        { 1.5f, 0.0f, 0.5f, 1.0f },
        { 0.5f, 0.0f, -0.1f, 1.0f },
        { -0.01f, 0.0f, 1.0f, 1.0f },
        { 0.5f, 0.0f, NAN, 1.0f },
        { 0.5f, INFINITY, 0.5f, 1.0f }
    };
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(invalid) / sizeof(invalid[0])); i ++) {
        test_bool(box_transition_bezier_valid(&invalid[i]), false);
        test_bool(box_transition_valid(world, ecs_id(Position), &(TransitionSpec){
            .duration = 1.0f, .timing_function = CubicBezier, .bezier = invalid[i] }), false);
        test_bool(box_transition_valid(world, ecs_id(Position), &(TransitionSpec){
            .duration = 1.0f, .timing_function = EaseOut, .bezier = invalid[i] }), true);
    }

    test_bool(box_transition_bezier_valid(&(TransitionBezier){ 0, -3, 1, 4 }), true);
    test_bool(box_transition_bezier_valid(&(TransitionBezier){ 0, 0, 0, 0 }), true);
    test_bool(box_transition_bezier_valid(NULL), false);

    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    int level = ecs_log_set_level(-4);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = invalid[0] });
    ecs_log_set_level(level);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);

    level = ecs_log_set_level(-4);
    test_assert(ecs_script_run(world, "invalid",
        "using box.transform\n"
        "using box.transition\n"
        "bad {\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: CubicBezier, "
            "bezier: {0.2, 0, 1.2, 1}}]\n"
        "}\n", NULL) == 0);
    ecs_log_set_level(level);
    ecs_entity_t bad = ecs_lookup(world, "bad");
    test_assert(bad != 0);
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, bad, ecs_id(Position))), false);
    tr_frame(world);
    ecs_set(world, bad, Position, {4, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);

    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 4.0f * tr_ease(&tr_bezier, 0.5f));

    tr_fini(world, null);
}

/* A custom transition interrupted halfway starts from the value it renders and runs the whole
 * curve again to the new target */
void Transition_bezier_interrupt(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    float mid = 4.0f * tr_ease(&tr_bezier, 0.5f);
    test_flt(tr_position(world, e).x, mid);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4.0f * tr_ease(&tr_bezier, 0.75f));
    float from = 4.0f * tr_ease(&tr_bezier, 0.75f);
    int32_t i;
    for (i = 1; i < 4; i ++) {
        tr_frame(world);
        float t = tr_ease(&tr_bezier, (float)i / 4.0f);
        test_flt(tr_position(world, e).x, from - from * t);
        test_int(box_render_transition_test_active(world), 1);
    }

    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}
