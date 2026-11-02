/**
 * @file Source.c
 * @brief Sources and plays of box.audio, measured through a null output: play and stop, loop,
 * pitch, Disabled sources and buses, prefab instances and the fade of a deleted source.
 */

#include "world.h"

void Source_play_stop(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE * 2);
    ecs_entity_t src = audio_source(world, dc, 1.0f, false, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    ecs_ensure(world, src, AudioSource)->playing = false;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    ecs_ensure(world, src, AudioSource)->playing = true;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

void Source_end_clears_playing(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE / 10);
    ecs_entity_t src = audio_source(world, dc, 1.0f, false, 0);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    test_bool(ecs_get(world, src, AudioSource)->playing, false);

    ecs_ensure(world, src, AudioSource)->playing = true;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

void Source_loop(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE / 20);
    ecs_entity_t src = audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 30);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    ecs_fini(world);
}

void Source_pitch(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, "tone", 480, 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, tone, 1.0f, true, 0);
    audio_run(world, 10);
    double f = audio_frequency(audio_tap_l, audio_tap_count - 4800, audio_tap_count);
    test_assert(fabs(f - 480) < 1);

    ecs_ensure(world, src, AudioSource)->pitch = 2.0f;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 10);
    f = audio_frequency(audio_tap_l, audio_tap_count - 4800, audio_tap_count);
    test_assert(fabs(f - 960) < 2);
    ecs_fini(world);
}

void Source_volume(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, dc, 0.5f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_ensure(world, src, AudioSource)->volume = 1.5f;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.75f);
    ecs_fini(world);
}

void Source_disabled_source(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    ecs_enable(world, src, false);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);

    ecs_enable(world, src, true);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

void Source_disabled_bus(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1.0f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, a);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    ecs_enable(world, a, false);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);

    ecs_enable(world, a, true);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

void Source_change_bus(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 0.5f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    ecs_set(world, src, AudioOutput, { .bus = a });
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_remove(world, src, AudioOutput);
    ecs_set(world, dc, AudioOutput, { .bus = a });
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    ecs_fini(world);
}

void Source_prefab_instances(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, AudioSource, {
        .sound = dc, .volume = 0.5f, .pitch = 1.0f, .loop = true, .playing = true });
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);

    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_new_w_pair(world, EcsIsA, prefab);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);

    ecs_ensure(world, i1, AudioSource)->volume = 0.0f;
    ecs_modified(world, i1, AudioSource);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_delete(world, i1);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_fini(world);
}

void Source_deleted_fades(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    int32_t at = audio_tap_count;

    ecs_delete(world, src);
    audio_run(world, 3);
    test_assert(audio_tap_l[at] > 0.4f);
    test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < 0.01f);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

void Source_play_oneshot(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE / 5);
    audio_run(world, 1);

    test_bool(box_audio_play(world, dc, 0.5f), true);
    test_bool(box_audio_play(world, dc, 0.5f), true);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(ecs_count(world, AudioSource), 0);

    audio_run(world, 15);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    test_bool(box_audio_play(world, 0, 1.0f), false);
    ecs_fini(world);
}

void Source_pcm_change(void) {
    // audio/source
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    audio_dc(world, "dc", 0.25f, AUDIO_RATE);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    ecs_fini(world);
}

/** State the transport of Source_drop_before_gesture reports */
static box_audio_state_t source_fake_state;

static box_audio_state_t source_state(
    box_audio_transport_t *t)
{
    (void)t;

    return source_fake_state;
}

void Source_drop_before_gesture(void) {
    // audio/source
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    audio_tap_count = 0;
    box_audio_transport_t *t = box_audio_transport_null_new(&(box_audio_null_desc_t){
        .sample_rate = AUDIO_RATE, .output = audio_world_tap });
    t->state = source_state;
    source_fake_state = BOX_AUDIO_STATE_WAITING;
    box_audio_output_use(world, t);

    ecs_entity_t dc = audio_dc(world, "dc", 0.25f, AUDIO_RATE);
    ecs_entity_t once = audio_source(world, dc, 1.0f, false, 0);
    audio_source(world, dc, 1.0f, true, 0);
    test_bool(box_audio_play(world, dc, 1.0f), true);
    audio_run(world, 5);
    test_int(ecs_singleton_get(world, AudioStatus)->state, AudioWaitingForGesture);
    test_bool(ecs_get(world, once, AudioSource)->playing, false);
    test_int(box_audio_module_stats(world).dropped, 2);

    source_fake_state = BOX_AUDIO_STATE_RUNNING;
    audio_run(world, 5);
    test_int(ecs_singleton_get(world, AudioStatus)->state, AudioRunning);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    test_bool(box_audio_play(world, dc, 1.0f), true);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}
