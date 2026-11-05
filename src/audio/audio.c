/**
 * @file audio/audio.c
 * @brief Registers the box.audio components, the play and capture functions and their script
 * functions, and writes the WAV files of captures. Sound files are in sample.c, music streams
 * in music.c.
 */

#define BOX_AUDIO_IMPL
#include "module.h"
#include "../jobs/jobs.h"
#include <stdio.h>
#include <math.h>

/** Singleton through which the functions of the module find its state in a world */
typedef struct AudioModuleState {
    box_audio_module_t *state;  /**< The state */
} AudioModuleState;

static ECS_COMPONENT_DECLARE(AudioModuleState);
ECS_TAG_DECLARE(AudioListener);
ECS_COMPONENT_DECLARE(AudioSoundId);
ECS_COMPONENT_DECLARE(AudioBusState);
ECS_COMPONENT_DECLARE(AudioVoice);
ECS_COMPONENT_DECLARE(AudioEffectState);
ECS_COMPONENT_DECLARE(AudioModulatorState);
ECS_COMPONENT_DECLARE(AudioSample);
ECS_COMPONENT_DECLARE(AudioMusic);
ECS_COMPONENT_DECLARE(AudioSynth);
ECS_COMPONENT_DECLARE(AudioWavetable);
ECS_COMPONENT_DECLARE(AudioInstrumentState);
ECS_COMPONENT_DECLARE(AudioTrackState);

/** Construct AudioBus values: unity volume and width to the master, centered, no sends.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_bus_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioBus *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioBus){ .volume = 1.0f, .width = 1.0f };
    }
}

/** Construct AudioSource values: a sound that plays at once at unity volume and pitch.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_source_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSource *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSource){ .volume = 1.0f, .pitch = 1.0f, .playing = true };
    }
}

/** Construct AudioStream values: a stream that loops and plays at once at unity volume.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_stream_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioStream *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioStream){ .volume = 1.0f, .loop = true, .playing = true };
    }
}

/** Construct AudioSpatial values: full volume within 1, silent from 50, rolloff 1.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_spatial_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSpatial *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSpatial){
            .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f };
    }
}

/** Construct Reverb values: a decay of 1.5 seconds, half damped, 20 ms of predelay, a middle
 * size, fully wet.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_reverb_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Reverb *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Reverb){
            .decay = 1.5f, .damping = 0.5f, .predelay = 0.02f, .size = 0.5f, .mix = 1.0f };
    }
}

/** Construct Equalizer values: every band off, at 100, 400, 2000 and 8000 Hz, flat, at a q
 * of 0.7071.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_equalizer_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    static const float frequencies[BOX_AUDIO_EQ_BANDS] = { 100, 400, 2000, 8000 };
    Equalizer *values = ptr;
    int32_t i, b;
    (void)ti;
    for (i = 0; i < count; i ++) {
        for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
            values[i].bands[b] = (AudioEqBand){
                .kind = AudioEqOff, .frequency = frequencies[b], .gain = 0, .q = 0.7071f };
        }
    }
}

/** Construct AudioFilter values: a lowpass at 1000 Hz and 12 dB per octave without
 * resonance, fully wet.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_filter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioFilter *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioFilter){
            .kind = SynthLowPass, .cutoff = 1000.0f, .slope = SynthSlope12, .mix = 1.0f };
    }
}

/** Construct Compressor values: -18 dB at 4:1 with a knee of 6 dB, an attack of 10 ms and a
 * release of 150 ms, fully wet, keyed on its own input.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_compressor_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Compressor *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Compressor){ .threshold = -18.0f, .ratio = 4.0f, .attack = 0.01f,
            .release = 0.15f, .knee = 6.0f, .makeup = 0, .mix = 1.0f, .sidechain = 0 };
    }
}

/** Construct Delay values: a quarter of a second, not synced, a feedback of 0.35, a mix of
 * 0.3, no ping pong, damped at 6000 Hz.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_delay_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Delay *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Delay){ .time = 0.25f, .feedback = 0.35f, .mix = 0.3f,
            .damping = 6000.0f };
    }
}

/** Construct Phaser values: 4 stages swept at 0.5 Hz by a depth of 0.7 around 800 Hz,
 * without feedback, at a mix of 0.5 and the right channel half a half turn later.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_phaser_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Phaser *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Phaser){ .stages = 4, .rate = 0.5f, .depth = 0.7f, .center = 800.0f,
            .feedback = 0, .mix = 0.5f, .stereo = 0.5f };
    }
}

/** Construct Chorus values: 2 voices at 0.8 Hz, 12 ms moved by 3 ms, a mix of 0.5, a
 * spread of 0.5 and no feedback.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_chorus_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Chorus *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Chorus){ .voices = 2, .rate = 0.8f, .depth = 3.0f, .delay = 12.0f,
            .mix = 0.5f, .spread = 0.5f, .feedback = 0.0f };
    }
}

/** Construct Saturator values: the tanh clipper at a drive of 0.3, symmetric, without the
 * filters, the sample and hold and the bit crusher, fully wet.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_saturator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Saturator *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Saturator){ .drive = 0.3f, .asymmetry = 0, .bits = 16.0f, .mix = 1.0f,
            .mode = SaturatorTanh, .low_cut = 0, .high_cut = 0, .tone = 0, .rate = 0 };
    }
}

/** Construct TransientShaper values: an attack and a sustain of 0, which pass the signal
 * unchanged.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_transient_shaper_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TransientShaper *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TransientShaper){ .attack = 0, .sustain = 0 };
    }
}

/** Construct Gate values: -50 dB, an attack of 1 ms, a hold of 50 ms, a release of 100 ms
 * and a range of -80 dB.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_gate_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Gate *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Gate){ .threshold = -50.0f, .attack = 0.001f, .hold = 0.05f,
            .release = 0.1f, .range = -80.0f };
    }
}

/** Construct SynthOscillator values: a sine at 440 Hz at unity level, without a sweep, heard,
 * with an index of 1 for when it modulates, a pulse width of 0.5 for when it is a square, and
 * no sub, one octave under for when it has one.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_oscillator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthOscillator *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthOscillator){
            .wave = SynthSine, .frequency = 440.0f, .level = 1.0f, .index = 1.0f,
            .pulse_width = 0.5f, .sub_octave = 1 };
    }
}

/** Construct SynthUnison values: 3 voices over 10 cents, in the center.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_unison_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthUnison *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthUnison){ .voices = 3, .detune = 10.0f, .width = 0.0f };
    }
}

/** Construct SynthString values: a decay of 2 seconds, a brightness of 0.5 and a hardness of
 * 0.8.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_string_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthString *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthString){ .decay = 2.0f, .brightness = 0.5f, .hardness = 0.8f };
    }
}

/** Construct Wavetable values: WavetableBasic, without a sound, frames of 2048 samples.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_audio_wavetable_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Wavetable *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Wavetable){ .kind = WavetableBasic,
            .frame_size = BOX_AUDIO_WAVETABLE_FRAME_SIZE };
    }
}

/** Construct SynthFilter values: a lowpass at 1000 Hz and 12 dB per octave without
 * resonance, with the feedback and the tune of a comb at their defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_filter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthFilter *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthFilter){ .kind = SynthLowPass, .cutoff = 1000.0f,
            .feedback = 0.5f, .tune = 1.0f };
    }
}

/** Construct SynthDrive values: a drive of 0.3, symmetric.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_drive_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthDrive *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthDrive){ .drive = 0.3f, .asymmetry = 0 };
    }
}

/** Construct SynthLfo values: a sine at 5 Hz from phase 0 without a delay, at depth 0 on
 * every destination.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_lfo_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthLfo *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthLfo){ .wave = SynthLfoSine, .rate = 5.0f };
    }
}

/** Construct AudioSettings values: unity master volume, not muted.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_settings_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSettings *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSettings){ .master_volume = 1.0f };
    }
}

/** Construct Transport values: 120 quarter notes per minute in 4/4, stopped at beat 0,
 * without a loop or a metronome, 0.15 seconds of lookahead.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_transport_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Transport *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Transport){
            .tempo = 120.0f, .beats_per_bar = 4, .beat_unit = 4, .lookahead = 0.15f };
    }
}

/** Construct Instrument values: no sound, a root of A4, a velocity curve of 2, a gate and a
 * release of 50 ms.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_instrument_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Instrument *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Instrument){
            .root = 69.0f, .velocity_curve = 2.0f, .gate = true, .release = 0.05f };
    }
}

/** Construct Note values: at the start of the clip, at velocity 1.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_note_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Note *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Note){ .velocity = 1.0f };
    }
}

/** Give a curve a copy of its keys of its own, after a copy bit for bit.
 *
 * @param curve The curve
 */
static void box_audio_curve_dup(
    Curve *curve)
{
    if (curve->keys.array) {
        curve->keys = ecs_vec_copy_t(NULL, &curve->keys, CurveKey);
    } else {
        ecs_os_memset_t(&curve->keys, 0, ecs_vec_t);
    }
}

void box_audio_note_dup(
    Note *note)
{
    box_audio_curve_dup(&note->bend);
    box_audio_curve_dup(&note->mod);
    box_audio_curve_dup(&note->pressure);
}

void box_audio_note_fini(
    Note *note)
{
    ecs_vec_fini_t(NULL, &note->bend.keys, CurveKey);
    ecs_vec_fini_t(NULL, &note->mod.keys, CurveKey);
    ecs_vec_fini_t(NULL, &note->pressure.keys, CurveKey);
}

/** Free the notes of a clip and the keys of their curves.
 *
 * @param clip The clip
 */
static void box_audio_note_clip_free(
    NoteClip *clip)
{
    Note *notes = ecs_vec_first(&clip->notes);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&clip->notes); i ++) {
        box_audio_note_fini(&notes[i]);
    }

    ecs_vec_fini_t(NULL, &clip->notes, Note);
}

/** Free NoteClip values, their notes and the keys of their curves.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_note_clip_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    NoteClip *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_audio_note_clip_free(&values[i]);
    }
}

/** Copy NoteClip values: the destination gets copies of the notes and of the keys of their
 * curves.
 *
 * @param dst_ptr The destination components
 * @param src_ptr The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_note_clip_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    NoteClip *dst = dst_ptr;
    const NoteClip *src = src_ptr;
    Note *notes;
    int32_t i, k;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (&dst[i] == &src[i]) {
            continue;
        }

        box_audio_note_clip_free(&dst[i]);
        dst[i] = src[i];
        if (src[i].notes.array) {
            dst[i].notes = ecs_vec_copy_t(NULL, &src[i].notes, Note);
        } else {
            ecs_os_memset_t(&dst[i].notes, 0, ecs_vec_t);
        }

        notes = ecs_vec_first(&dst[i].notes);
        for (k = 0; k < ecs_vec_count(&dst[i].notes); k ++) {
            box_audio_note_dup(&notes[k]);
        }
    }
}

/** Move NoteClip values: the destination takes the notes of the source.
 *
 * @param dst_ptr The destination components
 * @param src_ptr The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_note_clip_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    NoteClip *dst = dst_ptr, *src = src_ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (&dst[i] != &src[i]) {
            box_audio_note_clip_free(&dst[i]);
            dst[i] = src[i];
            ecs_os_memset_t(&src[i].notes, 0, ecs_vec_t);
        }
    }
}

/** Construct NoteClip values: 4 beats long, without notes.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_note_clip_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    NoteClip *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (NoteClip){ .length = 4.0f };
    }
}

/** Construct AudioClip values: no sound, from its start, 4 beats long.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_audio_clip_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioClip *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioClip){ .length = 4.0f };
    }
}

/** Construct AutomationClip values: no target, 4 beats long, without keys.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_automation_clip_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AutomationClip *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AutomationClip){ .length = 4.0f };
    }
}

/** Construct AudioModulator values: a sine LFO at 1 Hz of depth 0 without a target, 16 steps,
 * a follower with an attack of 10 ms and a release of 100 ms.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_modulator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioModulator *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioModulator){ .rate = 1.0f, .step_count = 16, .attack = 0.01f,
            .release = 0.1f };
    }
}

/** Construct Track values: no instrument, at priority 1, above the one shots.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_track_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Track *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Track){ .priority = BOX_AUDIO_TRACK_PRIORITY };
    }
}

/** Construct ClipPlacement values: no clip at beat 0, as long as the clip, at unity gain.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_placement_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ClipPlacement *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ClipPlacement){ .gain = 1.0f };
    }
}

/** Copy what box.audio sent for an entity: a copy starts with nothing sent. A prefab
 * instance or a clone gets a sound, a bus or a voice of its own this way.
 *
 * @param dst The copies
 * @param src The originals
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_track_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    ecs_os_memset(dst, 0, ti->size * count);
}

box_audio_module_t* box_audio_module_get(
    const ecs_world_t *world)
{
    const AudioModuleState *s = ecs_id(AudioModuleState) ?
        ecs_singleton_get(world, AudioModuleState) : NULL;

    return s ? s->state : NULL;
}

box_audio_module_stats_t box_audio_module_stats(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_module_stats_t result = {0};
    if (state) {
        result = state->stats;
    }

    return result;
}

int32_t box_audio_module_order(
    const ecs_world_t *world,
    ecs_entity_t *buses,
    int32_t max)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t i, result = 0;
    for (i = 0; state && i < state->order_count && result < max; i ++) {
        if (state->order[i]) {
            buses[result ++] = state->bus_entity[state->order[i]];
        }
    }

    return result;
}

int32_t box_audio_module_sounds(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t i, result = 0;
    for (i = 0; state && i < BOX_AUDIO_SOUND_MAX; i ++) {
        result += state->sound_used[i] != 0;
    }

    return result;
}

int32_t box_audio_module_bus_index(
    const ecs_world_t *world,
    ecs_entity_t bus)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const AudioBusState *s = NULL;
    int32_t result = 0;
    if (!state || !bus || !ecs_is_alive(world, bus)) {
        goto done;
    }

    s = ecs_get(world, bus, AudioBusState);
    if (s && s->index && state->bus_entity[s->index] == bus) {
        result = s->index;
    }

done:
    return result;
}

uint64_t box_audio_module_clock(
    const ecs_world_t *world)
{
    box_audio_transport_t *t = box_audio_output_transport(world);

    return t ? t->clock(t) : 0;
}

ecs_entity_t box_audio_sound_from_pcm(
    ecs_world_t *world,
    ecs_entity_t sound,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    ecs_entity_t result = 0;
    AudioPcm *pcm;
    if (!data || frames <= 0 || (channels != 1 && channels != 2) || sample_rate < 0) {
        ecs_err("box_audio_sound_from_pcm: invalid samples");
        goto done;
    }

    result = sound ? sound : ecs_new(world);
    pcm = ecs_ensure(world, result, AudioPcm);
    ecs_vec_init_if_t(&pcm->samples, float);
    ecs_vec_set_count_t(NULL, &pcm->samples, float, frames * channels);
    ecs_os_memcpy_n(ecs_vec_first_t(&pcm->samples, float), data, float, frames * channels);
    pcm->channels = channels;
    pcm->sample_rate = sample_rate;
    ecs_modified(world, result, AudioPcm);
done:
    return result;
}

/** Whether an entity is a sound that box_audio_play and box_audio_play_at can play.
 *
 * @param world The world
 * @param sound The entity
 * @return Whether it is alive and has AudioPcm, SoundFile or SynthSound
 */
static bool box_audio_is_sound(
    ecs_world_t *world,
    ecs_entity_t sound)
{
    return sound && ecs_is_alive(world, sound) && (ecs_has(world, sound, AudioPcm) ||
        ecs_has(world, sound, SoundFile) || ecs_has(world, sound, SynthSound));
}

bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume)
{
    box_audio_module_t *state = box_audio_module_get(world);
    bool result = false;
    if (!state || !box_audio_is_sound(world, sound)) {
        goto done;
    }

    box_audio_play_t *play = ecs_vec_append_t(NULL, &state->plays, box_audio_play_t);
    *play = (box_audio_play_t){ .sound = sound, .volume = volume };
    result = true;
done:
    return result;
}

bool box_audio_play_quantized(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume,
    AudioQuantize quantize)
{
    box_audio_module_t *state = box_audio_module_get(world);
    bool result = false;
    if (!state || !box_audio_is_sound(world, sound)) {
        goto done;
    }

    box_audio_play_t *play = ecs_vec_append_t(NULL, &state->plays, box_audio_play_t);
    *play = (box_audio_play_t){ .sound = sound, .volume = volume,
        .quantize = quantize == AudioQuantizeBeat || quantize == AudioQuantizeBar ?
            quantize : AudioQuantizeNone };
    result = true;
done:
    return result;
}

bool box_audio_play_at(
    ecs_world_t *world,
    ecs_entity_t sound,
    const vec3 position,
    float volume)
{
    box_audio_module_t *state = box_audio_module_get(world);
    bool result = false;
    if (!state || !position || !box_audio_is_sound(world, sound)) {
        goto done;
    }

    box_audio_play_t *play = ecs_vec_append_t(NULL, &state->plays, box_audio_play_t);
    *play = (box_audio_play_t){ .sound = sound, .volume = volume, .spatial = true,
        .position = { position[0], position[1], position[2] } };
    result = true;
done:
    return result;
}

int box_audio_capture(
    ecs_world_t *world,
    float seconds,
    const char *path)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int result = -1;
    if (!state || !path || !path[0]) {
        ecs_err("box.audio: a capture needs a file");
        goto done;
    }

    if (!(seconds > 0) || seconds > BOX_AUDIO_CAPTURE_MAX) {
        ecs_err("box.audio: a capture takes more than 0 and at most %.0f seconds",
            (double)BOX_AUDIO_CAPTURE_MAX);
        goto done;
    }

    if (state->capture.path) {
        ecs_err("box.audio: a capture is already recording");
        goto done;
    }

    state->capture.seconds = seconds;
    state->capture.path = ecs_os_strdup(path);
    state->capture.serial = 0;
    result = 0;
done:
    return result;
}

bool box_audio_capture_pending(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state && state->capture.path;
}

/** Write a little endian 16 bit value.
 *
 * @param out The bytes
 * @param value The value
 */
static void box_audio_wav_u16(
    uint8_t *out,
    uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
}

/** Write a little endian 32 bit value.
 *
 * @param out The bytes
 * @param value The value
 */
static void box_audio_wav_u32(
    uint8_t *out,
    uint32_t value)
{
    box_audio_wav_u16(out, value & 0xFFFF);
    box_audio_wav_u16(out + 2, value >> 16);
}

/** Convert a sample to 16 bits, clamped and rounded to the nearest.
 *
 * @param x The sample
 * @return The 16 bit sample
 */
static int16_t box_audio_wav_sample(
    float x)
{
    float v = x != x ? 0.0f : (x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x));

    return (int16_t)floorf(v * 32767.0f + 0.5f);
}

int box_audio_wav_write(
    const char *path,
    const float *data,
    int32_t frames,
    int32_t sample_rate)
{
    uint32_t bytes = (uint32_t)frames * 4;
    uint8_t *wav = ecs_os_malloc((ecs_size_t)bytes + 44);
    char *tmp = flecs_asprintf("%s.tmp", path);
    FILE *f = NULL;
    int32_t i;
    int result = -1;
    if (!wav) {
        goto done;
    }

    ecs_os_memcpy(wav, "RIFF", 4);
    box_audio_wav_u32(wav + 4, 36 + bytes);
    ecs_os_memcpy(wav + 8, "WAVEfmt ", 8);
    box_audio_wav_u32(wav + 16, 16);
    box_audio_wav_u16(wav + 20, 1);
    box_audio_wav_u16(wav + 22, 2);
    box_audio_wav_u32(wav + 24, (uint32_t)sample_rate);
    box_audio_wav_u32(wav + 28, (uint32_t)sample_rate * 4);
    box_audio_wav_u16(wav + 32, 4);
    box_audio_wav_u16(wav + 34, 16);
    ecs_os_memcpy(wav + 36, "data", 4);
    box_audio_wav_u32(wav + 40, bytes);
    for (i = 0; i < frames * 2; i ++) {
        box_audio_wav_u16(wav + 44 + i * 2, (uint16_t)box_audio_wav_sample(data[i]));
    }

    f = fopen(tmp, "wb");
    if (!f) {
        goto done;
    }

    if (fwrite(wav, 1, (size_t)bytes + 44, f) != (size_t)bytes + 44) {
        fclose(f);
        remove(tmp);
        goto done;
    }

    fclose(f);
    if (rename(tmp, path)) {
        remove(tmp);
        goto done;
    }

    result = 0;
done:
    if (result) {
        ecs_err("box.audio: cannot write the capture '%s'", path);
    }

    ecs_os_free(wav);
    ecs_os_free(tmp);

    return result;
}

/** The box.audio.play script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The sound and the volume
 * @param result Receives whether the play was queued
 */
static void box_audio_play_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t sound = *(ecs_entity_t*)argv[0].ptr;
    float volume = *(float*)argv[1].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play(ctx->world, sound, volume);
}

/** The box.audio.play_quantized script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The sound, the volume and the grid
 * @param result Receives whether the play was queued
 */
static void box_audio_play_quantized_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t sound = *(ecs_entity_t*)argv[0].ptr;
    float volume = *(float*)argv[1].ptr;
    AudioQuantize quantize = *(AudioQuantize*)argv[2].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play_quantized(ctx->world, sound, volume, quantize);
}

/** The box.audio.play_note script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The instrument, the pitch, the velocity and the duration
 * @param result Receives whether the note was queued
 */
static void box_audio_play_note_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t instrument = *(ecs_entity_t*)argv[0].ptr;
    float pitch = *(float*)argv[1].ptr;
    float velocity = *(float*)argv[2].ptr;
    float duration = *(float*)argv[3].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play_note(ctx->world, instrument, pitch, velocity,
        duration);
}

/** The box.audio.play_at script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The sound, the position and the volume
 * @param result Receives whether the play was queued
 */
static void box_audio_play_at_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t sound = *(ecs_entity_t*)argv[0].ptr;
    const float *position = argv[1].ptr;
    float volume = *(float*)argv[2].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play_at(ctx->world, sound, position, volume);
}

/** The box.audio.capture script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The seconds and the file
 * @param result Receives whether the capture was queued
 */
static void box_audio_capture_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    float seconds = *(float*)argv[0].ptr;
    const char *path = *(const char**)argv[1].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_capture(ctx->world, seconds, path) == 0;
}

/** Free the state of the module once the world deleted its entities.
 *
 * @param world The world
 * @param ctx The state
 */
static void box_audio_module_free(
    ecs_world_t *world,
    void *ctx)
{
    box_audio_module_t *state = ctx;
    (void)world;
    ecs_os_free(state->writer.data);
    ecs_map_fini(&state->voices);
    ecs_map_fini(&state->spread_notes);
    ecs_vec_fini_t(NULL, &state->oneshots, box_audio_oneshot_t);
    ecs_vec_fini_t(NULL, &state->plays, box_audio_play_t);
    ecs_vec_fini_t(NULL, &state->notes, box_audio_note_t);
    ecs_vec_fini_t(NULL, &state->stops, uint32_t);
    ecs_vec_fini_t(NULL, &state->releases, uint32_t);
    box_audio_music_fini(state);
    ecs_vec_fini_t(NULL, &state->moved, ecs_entity_t);
    box_audio_sample_fini(state);
    box_audio_sequencer_fini(state);
    box_audio_automation_fini(state);
    box_audio_modulators_fini(state);
    box_audio_timeline_fini(state);
    ecs_os_free(state->capture.path);
    ecs_os_free(state);
}

/** Give SynthWavetable its full name. The reflection of a C enum strips the name of the enum
 * from its constants, which would leave it as "table" in scripts.
 *
 * @param world The world
 */
static void box_audio_synth_wave_names(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_lookup_child(world, ecs_id(SynthWave), "table");
    ecs_assert(c != 0, ECS_INTERNAL_ERROR, NULL);
    ecs_set_name(world, c, "SynthWavetable");
    /* Setting the value again stores the new name in the constants of the enum */
    ecs_set_pair_second(world, c, EcsConstant, ecs_i32_t, {SynthWavetable});
}

/** Register the script functions of the module.
 *
 * @param world The world
 */
static void box_audio_functions_init(
    ecs_world_t *world)
{
    ecs_function(world, {
        .name = "play",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "sound", .type = ecs_id(ecs_entity_t) },
            { .name = "volume", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_audio_play_function
    });

    ecs_function(world, {
        .name = "play_quantized",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "sound", .type = ecs_id(ecs_entity_t) },
            { .name = "volume", .type = ecs_id(ecs_f32_t) },
            { .name = "quantize", .type = ecs_id(AudioQuantize) }
        },
        .callback = box_audio_play_quantized_function
    });

    ecs_function(world, {
        .name = "play_note",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "instrument", .type = ecs_id(ecs_entity_t) },
            { .name = "pitch", .type = ecs_id(ecs_f32_t) },
            { .name = "velocity", .type = ecs_id(ecs_f32_t) },
            { .name = "duration", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_audio_play_note_function
    });

    ecs_function(world, {
        .name = "play_at",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "sound", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "volume", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_audio_play_at_function
    });

    ecs_function(world, {
        .name = "capture",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "seconds", .type = ecs_id(ecs_f32_t) },
            { .name = "path", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_audio_capture_function
    });
}

void BoxAudioImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxAudio);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxJobs);
    ECS_IMPORT(world, BoxCurve);

    ECS_META_COMPONENT(world, AudioState);
    ECS_META_COMPONENT(world, AudioChannelKind);
    ECS_META_COMPONENT(world, AudioSend);
    ECS_META_COMPONENT(world, AudioBus);
    box_owned_hooks(world, AudioBus, BOX_OWNED_VEC(AudioBus, sends, AudioSend));
    ecs_set_hooks(world, AudioBus, { .ctor = box_audio_bus_ctor });
    ECS_META_COMPONENT(world, AudioMeter);
    ECS_META_COMPONENT(world, AudioOutput);
    ECS_META_COMPONENT(world, AudioPcm);
    box_owned_hooks(world, AudioPcm, BOX_OWNED_VEC(AudioPcm, samples, float));
    ECS_META_COMPONENT(world, SoundFile);
    box_owned_hooks(world, SoundFile, BOX_OWNED_STR(SoundFile, path));
    ECS_META_COMPONENT(world, SoundState);
    ECS_META_COMPONENT(world, SoundStatus);
    box_owned_hooks(world, SoundStatus, BOX_OWNED_STR(SoundStatus, error));
    ECS_META_COMPONENT(world, AudioSource);
    ecs_set_hooks(world, AudioSource, { .ctor = box_audio_source_ctor });
    ECS_META_COMPONENT(world, AudioStream);
    box_owned_hooks(world, AudioStream, BOX_OWNED_STR(AudioStream, path));
    ecs_set_hooks(world, AudioStream, { .ctor = box_audio_stream_ctor });
    ECS_META_COMPONENT(world, AudioStreamState);
    ECS_META_COMPONENT(world, AudioStreamStatus);
    box_owned_hooks(world, AudioStreamStatus, BOX_OWNED_STR(AudioStreamStatus, error));
    ECS_TAG_DEFINE(world, AudioListener);
    ECS_META_COMPONENT(world, AudioSpatial);
    ecs_set_hooks(world, AudioSpatial, { .ctor = box_audio_spatial_ctor });
    ECS_META_COMPONENT(world, Reverb);
    ecs_set_hooks(world, Reverb, { .ctor = box_audio_reverb_ctor });
    ECS_META_COMPONENT(world, AudioEqKind);
    ECS_META_COMPONENT(world, AudioEqBand);
    ECS_META_COMPONENT(world, Equalizer);
    ecs_set_hooks(world, Equalizer, { .ctor = box_audio_equalizer_ctor });
    ECS_META_COMPONENT(world, SynthWave);
    box_audio_synth_wave_names(world);
    ECS_META_COMPONENT(world, SynthFilterKind);
    ECS_META_COMPONENT(world, SynthFilterSlope);
    ECS_META_COMPONENT(world, SynthFilterModel);
    ECS_META_COMPONENT(world, SynthModulation);
    ECS_META_COMPONENT(world, SynthNoiseColor);
    ECS_META_COMPONENT(world, SynthOscillator);
    ecs_set_hooks(world, SynthOscillator, { .ctor = box_audio_synth_oscillator_ctor });
    ECS_META_COMPONENT(world, SynthUnison);
    ecs_set_hooks(world, SynthUnison, { .ctor = box_audio_synth_unison_ctor });
    ECS_META_COMPONENT(world, SynthString);
    ecs_set_hooks(world, SynthString, { .ctor = box_audio_synth_string_ctor });
    ECS_META_COMPONENT(world, WavetableKind);
    ECS_META_COMPONENT(world, Wavetable);
    ecs_set_hooks(world, Wavetable, { .ctor = box_audio_wavetable_ctor });
    ECS_META_COMPONENT(world, SynthTable);
    ECS_META_COMPONENT(world, SynthEnvelope);
    ECS_META_COMPONENT(world, SynthFilter);
    ecs_set_hooks(world, SynthFilter, { .ctor = box_audio_synth_filter_ctor });
    ECS_META_COMPONENT(world, AudioFilter);
    ecs_set_hooks(world, AudioFilter, { .ctor = box_audio_filter_ctor });
    ECS_META_COMPONENT(world, Compressor);
    ecs_set_hooks(world, Compressor, { .ctor = box_audio_compressor_ctor });
    ECS_META_COMPONENT(world, Gate);
    ecs_set_hooks(world, Gate, { .ctor = box_audio_gate_ctor });
    ECS_META_COMPONENT(world, Delay);
    ecs_set_hooks(world, Delay, { .ctor = box_audio_delay_ctor });
    ECS_META_COMPONENT(world, Phaser);
    ecs_set_hooks(world, Phaser, { .ctor = box_audio_phaser_ctor });
    ECS_META_COMPONENT(world, Chorus);
    ecs_set_hooks(world, Chorus, { .ctor = box_audio_chorus_ctor });
    ECS_META_COMPONENT(world, SaturatorMode);
    ECS_META_COMPONENT(world, Saturator);
    ecs_set_hooks(world, Saturator, { .ctor = box_audio_saturator_ctor });
    ECS_META_COMPONENT(world, TransientShaper);
    ecs_set_hooks(world, TransientShaper, { .ctor = box_audio_transient_shaper_ctor });
    ECS_META_COMPONENT(world, SynthFilterEnvelope);
    ECS_META_COMPONENT(world, SynthPitchEnvelope);
    ECS_META_COMPONENT(world, SynthGlide);
    ECS_META_COMPONENT(world, SynthDrive);
    ecs_set_hooks(world, SynthDrive, { .ctor = box_audio_synth_drive_ctor });
    ECS_META_COMPONENT(world, SynthLfoWave);
    ECS_META_COMPONENT(world, SynthLfo);
    ecs_set_hooks(world, SynthLfo, { .ctor = box_audio_synth_lfo_ctor });
    ECS_META_COMPONENT(world, SynthModSource);
    ECS_META_COMPONENT(world, SynthModTarget);
    ECS_META_COMPONENT(world, SynthModRoute);
    ECS_META_COMPONENT(world, SynthModEnvelope);
    ECS_META_COMPONENT(world, SynthSound);
    ECS_META_COMPONENT(world, AudioSettings);
    ecs_set_hooks(world, AudioSettings, { .ctor = box_audio_settings_ctor });
    ECS_META_COMPONENT(world, AudioStatus);
    ECS_META_COMPONENT(world, Transport);
    ECS_META_COMPONENT(world, AudioQuantize);
    ecs_set_hooks(world, Transport, { .ctor = box_audio_transport_ctor });
    ECS_META_COMPONENT(world, InstrumentSpreadMode);
    ECS_META_COMPONENT(world, Instrument);
    ecs_set_hooks(world, Instrument, { .ctor = box_audio_instrument_ctor });
    ECS_META_COMPONENT(world, Note);
    box_owned_hooks(world, Note, BOX_OWNED_CURVE(Note, bend), BOX_OWNED_CURVE(Note, mod),
        BOX_OWNED_CURVE(Note, pressure));
    ecs_set_hooks(world, Note, { .ctor = box_audio_note_ctor });
    ECS_META_COMPONENT(world, NoteClip);
    ecs_set_hooks(world, NoteClip, {
        .ctor = box_audio_note_clip_ctor,
        .dtor = box_audio_note_clip_dtor,
        .copy = box_audio_note_clip_copy,
        .move = box_audio_note_clip_move
    });
    ECS_META_COMPONENT(world, AudioClip);
    ecs_set_hooks(world, AudioClip, { .ctor = box_audio_audio_clip_ctor });
    ECS_META_COMPONENT(world, AutomationClip);
    box_owned_hooks(world, AutomationClip, BOX_OWNED_STR(AutomationClip, member),
        BOX_OWNED_CURVE(AutomationClip, curve));
    ecs_set_hooks(world, AutomationClip, { .ctor = box_audio_automation_clip_ctor });
    ECS_META_COMPONENT(world, AudioModSource);
    ECS_META_COMPONENT(world, AudioModWave);
    ECS_META_COMPONENT(world, AudioModScale);
    ECS_META_COMPONENT(world, AudioModulator);
    box_owned_hooks(world, AudioModulator, BOX_OWNED_STR(AudioModulator, member));
    ecs_set_hooks(world, AudioModulator, { .ctor = box_audio_modulator_ctor });
    ECS_META_COMPONENT(world, TrackSwingGrid);
    ECS_META_COMPONENT(world, TrackHumanize);
    ECS_META_COMPONENT(world, Track);
    ecs_set_hooks(world, Track, { .ctor = box_audio_track_ctor });
    ECS_META_COMPONENT(world, ClipPlacement);
    ecs_set_hooks(world, ClipPlacement, { .ctor = box_audio_placement_ctor });

    ECS_COMPONENT_DEFINE(world, AudioSoundId);
    ECS_COMPONENT_DEFINE(world, AudioBusState);
    ECS_COMPONENT_DEFINE(world, AudioVoice);
    ECS_COMPONENT_DEFINE(world, AudioEffectState);
    ECS_COMPONENT_DEFINE(world, AudioModulatorState);
    ECS_COMPONENT_DEFINE(world, AudioSample);
    ECS_COMPONENT_DEFINE(world, AudioMusic);
    ECS_COMPONENT_DEFINE(world, AudioSynth);
    ECS_COMPONENT_DEFINE(world, AudioWavetable);
    ECS_COMPONENT_DEFINE(world, AudioInstrumentState);
    ECS_COMPONENT_DEFINE(world, AudioTrackState);
    ecs_set_hooks(world, AudioTrackState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioSoundId, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioInstrumentState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioSynth, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioWavetable, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioBusState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioVoice, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioEffectState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioModulatorState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioSample, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioMusic, { .copy = box_audio_track_copy });
    ecs_add_pair(world, ecs_id(AudioPcm), EcsWith, ecs_id(AudioSoundId));
    ecs_add_pair(world, ecs_id(AudioBus), EcsWith, ecs_id(AudioBusState));
    ecs_add_pair(world, ecs_id(AudioSource), EcsWith, ecs_id(AudioVoice));
    ecs_add_pair(world, ecs_id(Reverb), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Equalizer), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(AudioFilter), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Compressor), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Gate), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Delay), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Phaser), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Chorus), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(Saturator), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(TransientShaper), EcsWith, ecs_id(AudioEffectState));
    ecs_add_pair(world, ecs_id(AudioModulator), EcsWith, ecs_id(AudioModulatorState));
    ecs_add_pair(world, ecs_id(SoundFile), EcsWith, ecs_id(AudioSample));
    ecs_add_pair(world, ecs_id(AudioStream), EcsWith, ecs_id(AudioMusic));
    ecs_add_pair(world, ecs_id(SynthSound), EcsWith, ecs_id(AudioSoundId));
    ecs_add_pair(world, ecs_id(SynthSound), EcsWith, ecs_id(AudioSynth));
    ecs_add_pair(world, ecs_id(Wavetable), EcsWith, ecs_id(AudioWavetable));
    ecs_add_pair(world, ecs_id(Instrument), EcsWith, ecs_id(AudioInstrumentState));
    ecs_add_pair(world, ecs_id(Track), EcsWith, ecs_id(AudioBus));
    ecs_add_pair(world, ecs_id(Track), EcsWith, ecs_id(AudioTrackState));

    ecs_entity_t master = ecs_entity(world, { .name = "master" });
    ecs_singleton_add(world, AudioSettings);
    ecs_singleton_set(world, AudioStatus, {0});
    ecs_singleton_add(world, Transport);

    box_audio_module_t *state = ecs_os_calloc_t(box_audio_module_t);
    state->next_voice = 1;
    state->master = master;
    ecs_os_memset(state->keys, BOX_AUDIO_BUS_NONE, ECS_SIZEOF(state->keys));
    ecs_map_init(&state->voices, NULL);
    ecs_map_init(&state->spread_notes, NULL);
    ecs_vec_init_t(NULL, &state->oneshots, box_audio_oneshot_t, 0);
    ecs_vec_init_t(NULL, &state->plays, box_audio_play_t, 0);
    ecs_vec_init_t(NULL, &state->notes, box_audio_note_t, 0);
    ecs_vec_init_t(NULL, &state->stops, uint32_t, 0);
    ecs_vec_init_t(NULL, &state->releases, uint32_t, 0);
    ecs_vec_init_t(NULL, &state->moved, ecs_entity_t, 0);
    box_audio_timeline_init(state);
    ECS_COMPONENT_DEFINE(world, AudioModuleState);
    ecs_singleton_set(world, AudioModuleState, { .state = state });
    ecs_atfini(world, box_audio_module_free, state);

    box_audio_sync_init(world, state);
    box_audio_sample_init(world, state);
    box_audio_music_init(world, state);
    box_audio_instrument_init(world, state);
    box_audio_synth_init(world);
    box_audio_sequencer_init(world, state);
    box_audio_automation_init(world, state);
    box_audio_modulators_init(world, state);
    box_audio_functions_init(world);
}
