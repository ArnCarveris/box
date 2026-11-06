/**
 * @file SynthBowedWorld.c
 * @brief SynthBowed in a world, apart from the SynthBowed suite: the defaults of SynthBow, the
 * patch a script compiles to, its recompile and a bowed sound that plays.
 */

#include "world.h"

/** A SynthBow starts at its defaults; a script sets it on a SynthBowed layer, its fields and a
 * route to the bow reach the patch, a layer without one takes the defaults, a change compiles
 * the sound again, and the bowed string plays. */
void SynthBowedWorld_component(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_bowed.flecs");
    ecs_entity_t violin, plain, e = ecs_new(world);
    const SynthBow *bow;
    const AudioSynth *synth;
    ecs_add(world, e, SynthBow);
    bow = ecs_get(world, e, SynthBow);
    test_flt(bow->pressure, 0.5f);
    test_flt(bow->speed, 0.5f);
    test_flt(bow->position, 0.12f);
    test_flt(bow->brightness, 0.5f);
    test_flt(bow->noise, 0.1f);
    test_flt(bow->velocity, 0);
    test_int(SynthBowed, BOX_AUDIO_SYNTH_BOWED);
    test_int(SynthTargetBowPressure, BOX_AUDIO_SYNTH_TO_BOW_PRESSURE);
    test_int(SynthTargetBowSpeed, BOX_AUDIO_SYNTH_TO_BOW_SPEED);
    test_int(SynthTargetBowPosition, BOX_AUDIO_SYNTH_TO_BOW_POSITION);

    test_assert(ecs_script_run(world, "bowed", code, NULL) == 0);
    ecs_os_free(code);
    violin = ecs_lookup(world, "Violin");
    plain = ecs_lookup(world, "Plain");
    test_assert(violin && plain);
    audio_run(world, 1);
    synth = ecs_get(world, violin, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.layers, 1);
    test_int(synth->patch.layer[0].wave, BOX_AUDIO_SYNTH_BOWED);
    test_flt(synth->patch.layer[0].bow_pressure, 0.45f);
    test_flt(synth->patch.layer[0].bow_speed, 0.6f);
    test_flt(synth->patch.layer[0].bow_position, 0.1f);
    test_flt(synth->patch.layer[0].brightness, 0.6f);
    test_flt(synth->patch.layer[0].bow_noise, 0.15f);
    test_flt(synth->patch.layer[0].bow_velocity, 0.5f);
    test_int(synth->patch.routes, 1);
    test_int(synth->patch.route[0].source, BOX_AUDIO_SYNTH_FROM_PRESSURE);
    test_int(synth->patch.route[0].target, BOX_AUDIO_SYNTH_TO_BOW_PRESSURE);
    test_flt(synth->patch.route[0].amount, 0.4f);
    synth = ecs_get(world, plain, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.layer[0].bow_pressure, 0.5f);
    test_flt(synth->patch.layer[0].bow_speed, 0.5f);
    test_flt(synth->patch.layer[0].bow_position, 0.12f);
    test_flt(synth->patch.layer[0].brightness, 0.5f);
    test_flt(synth->patch.layer[0].bow_noise, 0.1f);

    ecs_set(world, plain, SynthBow, { .pressure = 0.7f, .speed = 0.4f, .position = 0.2f,
        .brightness = 0.3f });
    audio_run(world, 1);
    synth = ecs_get(world, plain, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.layer[0].bow_pressure, 0.7f);
    test_flt(synth->patch.layer[0].bow_position, 0.2f);
    test_flt(synth->patch.layer[0].brightness, 0.3f);

    audio_source(world, plain, 0.5f, true, 0);
    audio_run(world, 20);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.01f);
    ecs_fini(world);
}
