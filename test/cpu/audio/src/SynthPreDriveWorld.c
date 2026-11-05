/**
 * @file SynthPreDriveWorld.c
 * @brief The SynthDrive component in a world, apart from the SynthPreDrive suite: its defaults, a
 * script, the patch it compiles to and its recompiles.
 */

#include "world.h"

/** SynthDrive defaults to a drive of 0.3 without asymmetry; a script sets it, it reaches the
 * patch, a sound without it has no drive, and an add, a change or a removal compiles the
 * sound again. */
void SynthPreDriveWorld_component(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_drive.flecs");
    ecs_entity_t growl, warm, clean, e = ecs_new(world);
    const SynthDrive *d;
    const AudioSynth *synth;
    ecs_add(world, e, SynthDrive);
    d = ecs_get(world, e, SynthDrive);
    test_flt(d->drive, 0.3f);
    test_flt(d->asymmetry, 0);

    test_assert(ecs_script_run(world, "drive", code, NULL) == 0);
    ecs_os_free(code);
    growl = ecs_lookup(world, "Growl");
    warm = ecs_lookup(world, "Warm");
    clean = ecs_lookup(world, "Clean");
    test_assert(growl && warm && clean);
    test_flt(ecs_get(world, growl, SynthDrive)->drive, 0.8f);
    test_flt(ecs_get(world, growl, SynthDrive)->asymmetry, 0.25f);
    audio_run(world, 1);
    synth = ecs_get(world, growl, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.has_drive, 1);
    test_flt(synth->patch.pre_drive, 0.8f);
    test_flt(synth->patch.pre_asymmetry, 0.25f);
    synth = ecs_get(world, warm, AudioSynth);
    test_int(synth->patch.has_drive, 1);
    test_flt(synth->patch.pre_drive, 0.3f);
    test_flt(synth->patch.pre_asymmetry, 0);
    synth = ecs_get(world, clean, AudioSynth);
    test_int(synth->patch.has_drive, 0);
    test_flt(synth->patch.pre_drive, 0);
    test_flt(synth->patch.pre_asymmetry, 0);

    ecs_set(world, clean, SynthDrive, { .drive = 0.6f, .asymmetry = 0.1f });
    audio_run(world, 1);
    synth = ecs_get(world, clean, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.has_drive, 1);
    test_flt(synth->patch.pre_drive, 0.6f);
    test_flt(synth->patch.pre_asymmetry, 0.1f);
    ecs_set(world, clean, SynthDrive, { .drive = 0.2f });
    audio_run(world, 1);
    test_flt(ecs_get(world, clean, AudioSynth)->patch.pre_drive, 0.2f);
    ecs_remove(world, clean, SynthDrive);
    audio_run(world, 1);
    synth = ecs_get(world, clean, AudioSynth);
    test_int(synth->patch.has_drive, 0);
    test_flt(synth->patch.pre_drive, 0);

    audio_source(world, growl, 0.5f, true, 0);
    audio_run(world, 20);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.05f);
    ecs_fini(world);
}
