/**
 * @file Inject.c
 * @brief Tests for the injection queue: durations, delays, order and reset.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Inject_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Inject_teardown(void)
{
    ecs_fini(w);
}

void Inject_a_press_of_one_frame_releases_the_next_frame(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 1 });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyW].pressed, true);
    test_bool(in->keys[BoxKeyW].state, true);
    ecs_progress(w, 0);
    in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyW].pressed, false);
    test_bool(in->keys[BoxKeyW].state, false);
}

void Inject_a_press_with_a_duration_holds_the_key(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 3 });
    int32_t frame;
    for (frame = 0; frame < 3; frame ++) {
        ecs_progress(w, 0);
        test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, true);
    }

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, false);
}

void Inject_a_down_without_a_duration_stays_down(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        ecs_progress(w, 0);
    }

    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, true);
    test_int(box_input_pending(w), 0);
}

void Inject_delay_postpones_the_event(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyQ, .delay = 2 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyQ].state, false);
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyQ].state, false);
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyQ].pressed, true);
}

void Inject_platform_events_are_applied_before_injected(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .source = InputSourcePlatform });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyW].pressed, true);
    test_bool(in->keys[BoxKeyW].state, false);
}

void Inject_events_of_one_frame_keep_their_order(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    box_input_inject(w, &(InputEvent){
        .kind = InputChar, .code = 'c', .source = InputSourcePlatform });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_int(in->text_len, 3);
    test_int(in->text[0], 'c');
    test_int(in->text[1], 'a');
    test_int(in->text[2], 'b');
}

void Inject_a_sweep_walks_the_cursor_over_frames(void)
{
    // input/device_layer
    int32_t step;
    for (step = 0; step < 4; step ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove,
            .value = {(float)(step * 10), 0},
            .delay = step
        });
    }

    test_int(box_input_pending(w), 4);
    for (step = 0; step < 4; step ++) {
        ecs_progress(w, 0);
        test_flt(ecs_singleton_get(w, Input)->mouse.window.x, (float)(step * 10));
    }

    test_int(box_input_pending(w), 0);
    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.delta.x, 0);
}

void Inject_pending_counts_the_queued_events(void)
{
    // input/device_layer
    test_int(box_input_pending(w), 0);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyA, .source = InputSourcePlatform });
    test_int(box_input_pending(w), 2);
    ecs_progress(w, 0);
    test_int(box_input_pending(w), 0);
}

void Inject_reset_empties_the_queue_and_clears_the_state(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyA, .delay = 4 });
    test_int(box_input_pending(w), 1);
    box_input_reset(w);
    test_int(box_input_pending(w), 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyW].state, false);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyA].state, false);
}
