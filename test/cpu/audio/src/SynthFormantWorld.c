/**
 * @file SynthFormantWorld.c
 * @brief SynthFilter.kind SynthFormant in a world, apart from the SynthFormant suite: its
 * vowel from a script, the vowel depths of SynthLfo and SynthFilterEnvelope, the patch they
 * compile to and their recompiles.
 */

#include "world.h"

/** A script sets SynthFormant with a vowel, an LFO on the vowel and a filter envelope on it
 * with an amount of 0; they reach the patch, a change compiles the sound again, and it plays. */
void SynthFormantWorld_component(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_formant.flecs");
    ecs_entity_t talk, wah, e = ecs_new(world);
    const AudioSynth *synth;
    ecs_add(world, e, SynthFilter);
    test_flt(ecs_get(world, e, SynthFilter)->vowel, 0);

    test_assert(ecs_script_run(world, "formant", code, NULL) == 0);
    ecs_os_free(code);
    talk = ecs_lookup(world, "Talk");
    wah = ecs_lookup(world, "Wah");
    test_assert(talk && wah);
    test_int(ecs_get(world, talk, SynthFilter)->kind, SynthFormant);
    test_flt(ecs_get(world, talk, SynthFilter)->vowel, 1);
    test_flt(ecs_get(world, talk, SynthLfo)->vowel, 1);
    test_flt(ecs_get(world, wah, SynthFilterEnvelope)->vowel, 3);
    audio_run(world, 1);
    synth = ecs_get(world, talk, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_FORMANT);
    test_flt(synth->patch.vowel, 1);
    test_int(synth->patch.lfos, 1);
    test_flt(synth->patch.lfo[0].vowel, 1);
    test_flt(synth->patch.filter_vowel, 0);
    synth = ecs_get(world, wah, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_FORMANT);
    test_flt(synth->patch.filter_vowel, 3);
    test_flt(synth->patch.filter_amount, 0);
    test_flt(synth->patch.filter_envelope.attack, 0.1f);

    ecs_set(world, talk, SynthFilter, { .kind = SynthFormant, .vowel = 2.5f });
    audio_run(world, 1);
    synth = ecs_get(world, talk, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.vowel, 2.5f);

    ecs_set(world, wah, SynthFilterEnvelope, { .attack = 0.1f, .vowel = -1, .amount = 2 });
    audio_run(world, 1);
    synth = ecs_get(world, wah, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.filter_vowel, -1);
    test_flt(synth->patch.filter_amount, 0);

    audio_source(world, talk, 0.5f, true, 0);
    audio_run(world, 20);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.02f);
    ecs_fini(world);
}

/** A filter envelope with a vowel of 0 on a formant filter compiles no envelope, as one with
 * an amount of 0 on the other kinds, and its amount moves nothing. */
void SynthFormantWorld_envelope_off(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const AudioSynth *synth;
    ecs_set(world, e, SynthSound, {0});
    ecs_set(world, e, SynthEnvelope, { .sustain = 1, .hold = 0.2f, .release = 0.1f });
    ecs_set(world, e, SynthOscillator, { .wave = SynthSaw, .frequency = 110, .level = 0.5f });
    ecs_set(world, e, SynthFilter, { .kind = SynthFormant, .vowel = 3 });
    ecs_set(world, e, SynthFilterEnvelope, { .decay = 0.2f, .amount = 3 });
    audio_run(world, 1);
    synth = ecs_get(world, e, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_FORMANT);
    test_flt(synth->patch.vowel, 3);
    test_flt(synth->patch.filter_amount, 0);
    test_flt(synth->patch.filter_vowel, 0);
    ecs_fini(world);
}
