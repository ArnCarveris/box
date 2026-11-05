/**
 * @file SynthCombWorld.c
 * @brief SynthFilter.kind SynthComb in a world, apart from the SynthComb suite: the defaults of
 * its feedback and tune, the patch a script compiles to and its recompile.
 */

#include "world.h"

/** A SynthFilter starts with the feedback and the tune of a comb at 0.5 and 1; a script sets
 * SynthComb with them, they reach the patch, a change compiles the sound again, they stay out
 * of the patch of another kind, and the comb plays. */
void SynthCombWorld_component(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_comb.flecs");
    ecs_entity_t pluck, hollow, e = ecs_new(world);
    const AudioSynth *synth;
    ecs_add(world, e, SynthFilter);
    test_flt(ecs_get(world, e, SynthFilter)->feedback, 0.5f);
    test_flt(ecs_get(world, e, SynthFilter)->tune, 1);

    test_assert(ecs_script_run(world, "comb", code, NULL) == 0);
    ecs_os_free(code);
    pluck = ecs_lookup(world, "NoisePluck");
    hollow = ecs_lookup(world, "Hollow");
    test_assert(pluck && hollow);
    test_int(ecs_get(world, pluck, SynthFilter)->kind, SynthComb);
    test_flt(ecs_get(world, pluck, SynthFilter)->feedback, 0.95f);
    test_flt(ecs_get(world, pluck, SynthFilter)->tune, 1);
    audio_run(world, 1);
    synth = ecs_get(world, pluck, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_COMB);
    test_flt(synth->patch.feedback, 0.95f);
    test_flt(synth->patch.tune, 1);
    synth = ecs_get(world, hollow, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_COMB);
    test_flt(synth->patch.feedback, -0.8f);
    test_flt(synth->patch.tune, 2);

    ecs_set(world, hollow, SynthFilter, {
        .kind = SynthComb, .cutoff = 24000, .feedback = 0.3f, .tune = 0.5f });
    audio_run(world, 1);
    synth = ecs_get(world, hollow, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.feedback, 0.3f);
    test_flt(synth->patch.tune, 0.5f);

    ecs_set(world, hollow, SynthFilter, {
        .kind = SynthLowPass, .cutoff = 2000, .feedback = 0.3f, .tune = 0.5f });
    audio_run(world, 1);
    synth = ecs_get(world, hollow, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_LOWPASS);
    test_flt(synth->patch.feedback, 0);
    test_flt(synth->patch.tune, 0);

    ecs_set(world, hollow, SynthFilter, {
        .kind = SynthComb, .cutoff = 24000, .feedback = -0.8f, .tune = 2 });
    audio_source(world, hollow, 0.5f, true, 0);
    audio_run(world, 20);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.02f);
    ecs_fini(world);
}
