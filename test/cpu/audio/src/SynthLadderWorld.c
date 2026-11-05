/**
 * @file SynthLadderWorld.c
 * @brief SynthFilter.model in a world, apart from the SynthLadder suite: its default, a
 * script, the patch it compiles to and its recompiles.
 */

#include "world.h"

/** SynthFilter.model defaults to SynthFilterSvf; a script sets SynthFilterLadder, it reaches
 * the patch, a change compiles the sound again, and the ladder plays. */
void SynthLadderWorld_component(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_ladder.flecs");
    ecs_entity_t acid, plain, e = ecs_new(world);
    const AudioSynth *synth;
    ecs_add(world, e, SynthFilter);
    test_int(ecs_get(world, e, SynthFilter)->model, SynthFilterSvf);

    test_assert(ecs_script_run(world, "ladder", code, NULL) == 0);
    ecs_os_free(code);
    acid = ecs_lookup(world, "Acid");
    plain = ecs_lookup(world, "Plain");
    test_assert(acid && plain);
    test_int(ecs_get(world, acid, SynthFilter)->model, SynthFilterLadder);
    test_int(ecs_get(world, plain, SynthFilter)->model, SynthFilterSvf);
    audio_run(world, 1);
    synth = ecs_get(world, acid, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.model, BOX_AUDIO_SYNTH_LADDER);
    test_int(synth->patch.slope, BOX_AUDIO_SYNTH_SLOPE_24);
    synth = ecs_get(world, plain, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.model, BOX_AUDIO_SYNTH_SVF);

    ecs_set(world, plain, SynthFilter, {
        .kind = SynthLowPass, .cutoff = 400, .model = SynthFilterLadder });
    audio_run(world, 1);
    synth = ecs_get(world, plain, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.model, BOX_AUDIO_SYNTH_LADDER);

    audio_source(world, acid, 0.5f, true, 0);
    audio_run(world, 20);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.02f);
    ecs_fini(world);
}
