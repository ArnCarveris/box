/**
 * @file Synth.c
 * @brief Benchmarks for the synth of the audio core: 64 looping synth voices on the master,
 * one item per sample of the output, with the patch of three layers, the same with two LFOs or
 * one of FM, with curved envelopes, with a driven filter, the ladder filter or the formant
 * filter with its vowel under an LFO and the filter envelope, with subs, hard synced under an
 * LFO, with pink noise, with drift or with the drive of the mix before the filter, 32 voices of
 * a pluck string, steady or under a vibrato, 8 of a unison pluck of 4, and 64 of a patch of
 * wavetables whose positions an LFO and the envelope move.
 *
 * Each iteration renders one quantum of 128 stereo frames, so the time per item is the time
 * per sample of the whole mix of 64 voices.
 */

#include <core.h>
#include <module.h>
#include <bake_bench.h>

/** Sample rate of the benchmarks */
#define BENCH_RATE 48000

/** Extra of bench_synth_core: the saw has a sub an octave under, the square two octaves */
#define BENCH_SUB 1u

/** Extra of bench_synth_core: the saw and the square are hard synced, under an LFO of the
 * ratio */
#define BENCH_SYNC 2u

/** Extra of bench_synth_core: a curved pitch envelope of the root, with a sync sweep, moves
 * every layer, the synced saw and square of BENCH_SYNC when it is set too */
#define BENCH_PITCH 4u

/** Extra of bench_synth_core: the noise is pink */
#define BENCH_PINK 8u

/** Extra of bench_synth_core: the filter is the transistor ladder */
#define BENCH_LADDER 16u

/** Extra of bench_synth_core: the filter is the formant filter, its vowel moved by the filter
 * envelope and by an LFO */
#define BENCH_FORMANT 64u

/** Extra of bench_synth_core: a drift of 15 cents moves the pitch of every layer and the
 * cutoff, and the oscillators start at free phases */
#define BENCH_DRIFT 32u

/** Create a core with 64 looping voices of a patch of three layers: a saw that sweeps up, a
 * square with its own envelope and noise, into a lowpass.
 *
 * With fm the patch is four sines instead: two carriers, each phase modulated by a modulator
 * with its own envelope, one of them with feedback.
 *
 * With lfo two LFOs move it: a sine on the pitch and the level of every layer, and a triangle
 * on the cutoff and on the pulse width of the square.
 *
 * @param envelope Whether a filter envelope moves the cutoff during the whole envelope
 * @param slope A box_audio_synth_slope_t
 * @param fm Whether the patch is the one of phase modulation
 * @param lfo Whether two LFOs move the patch
 * @param curved Whether the patch envelope and the envelope of the square are curved
 * @param drive Drive of the filter, 0..1
 * @param extra BENCH_SUB, BENCH_SYNC, BENCH_PITCH, BENCH_PINK, BENCH_LADDER, BENCH_FORMANT
 *   and BENCH_DRIFT, or 0
 * @param pre Drive of the mix of the layers before the filter, 0..1, negative for none; its
 *   asymmetry is 0.3
 * @return The core
 */
static box_audio_core_t* bench_synth_core(
    bool envelope,
    uint32_t slope,
    bool fm,
    bool lfo,
    bool curved,
    float drive,
    uint32_t extra,
    float pre)
{
    static uint8_t bytes[8192];
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_synth_patch_t p = {0};
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 3;
    p.seed = 3;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.1f, .sustain = 0.6f, .hold = 2.0f, .release = 0.2f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 400;
    p.resonance = 0.6f;
    p.slope = slope;
    p.drive = drive;
    if (pre >= 0) {
        p.has_drive = 1;
        p.pre_drive = pre;
        p.pre_asymmetry = 0.3f;
    }

    p.filter_amount = envelope ? 4.5f : 0;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 1.0f, .decay = 0.5f, .sustain = 0.7f, .hold = 0.6f, .release = 0.2f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 220, .sweep = 2.0f, .level = 0.4f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 330,
        .level = 0.2f, .has_envelope = 1,
        .envelope = { .attack = 0.005f, .decay = 0.3f, .sustain = 0.3f, .hold = 2.0f,
            .release = 0.1f } };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f };
    if (curved) {
        p.envelope.curve = 4;
        p.envelope.attack_curve = 1.5f;
        p.layer[1].envelope.curve = 4;
        p.layer[1].envelope.attack_curve = 1.5f;
    }

    if (extra & BENCH_LADDER) {
        p.model = BOX_AUDIO_SYNTH_LADDER;
    }

    if (extra & BENCH_FORMANT) {
        p.filter = BOX_AUDIO_SYNTH_FORMANT;
        p.vowel = 1;
        p.filter_vowel = envelope ? 2 : 0;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
            .vowel = 1 };
    }

    if (extra & BENCH_SUB) {
        p.layer[0].sub = 0.6f;
        p.layer[0].sub_octave = 1;
        p.layer[1].sub = 0.4f;
        p.layer[1].sub_octave = 2;
    }

    if (extra & BENCH_SYNC) {
        p.layer[0].sync = 2.5f;
        p.layer[1].sync = 3.2f;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 0.5f,
            .hard_sync = 12 };
    }

    if (extra & BENCH_PITCH) {
        p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 12, .decay = 1.5f,
            .curve = 4, .release_amount = -5, .sync = 12 };
    }

    if (extra & BENCH_PINK) {
        p.layer[2].color = BOX_AUDIO_SYNTH_PINK;
    }

    if (extra & BENCH_DRIFT) {
        p.drift = 15;
        p.free_phase = 1;
    }

    if (fm) {
        box_audio_synth_envelope_t decay = {
            .attack = 0.002f, .decay = 1.5f, .sustain = 0.2f, .hold = 2.0f, .release = 0.1f };
        p.layers = 4;
        p.layer[0] = (box_audio_synth_layer_t){ .frequency = 220, .level = 0.4f };
        p.layer[1] = (box_audio_synth_layer_t){ .frequency = 308, .level = 1,
            .modulates = 1, .index = 3, .has_envelope = 1, .envelope = decay };
        p.layer[2] = (box_audio_synth_layer_t){ .frequency = 440, .level = 0.2f };
        p.layer[3] = (box_audio_synth_layer_t){ .frequency = 440, .level = 1,
            .modulates = 3, .index = 1.5f, .feedback = 0.8f, .has_envelope = 1,
            .envelope = decay };
    }

    if (lfo) {
        p.lfos = 2;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5.5f,
            .delay = 0.1f, .fade = 0.2f, .pitch = 15, .amplitude = 0.1f };
        p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 0.3f,
            .random_phase = 1, .cutoff = 1.5f, .pulse_width = 0.3f };
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_synth(&w, 1, &p);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        box_audio_write_play(&w, i, 1, 0, 0.02f, -0.9f + 0.03f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP, 0);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Create a core with looping voices of a pluck string through a lowpass, which ring for 4
 * seconds, each at its own pitch, that take every string of BOX_AUDIO_SYNTH_STRINGS: one each,
 * or with unison a string per voice, detuned across the whole width, in stereo.
 *
 * With vibrato a sine LFO of 25 cents at 5.5 Hz moves the pitch of every string.
 *
 * @param voices Unison voices of the pluck, a string each
 * @param vibrato Whether an LFO moves the pitch of the strings
 * @return The core
 */
static box_audio_core_t* bench_pluck_core(
    uint32_t voices,
    bool vibrato)
{
    static uint8_t bytes[8192];
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_synth_patch_t p = {0};
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 1;
    p.seed = 3;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 3500;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 110,
        .level = 0.4f, .decay = 4.0f, .brightness = 0.5f, .hardness = 0.8f, .rings_free = 1,
        .voices = voices, .detune = 12, .width = 1 };
    if (vibrato) {
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){
            .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5.5f, .pitch = 25 };
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_synth(&w, 1, &p);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS / voices; i ++) {
        box_audio_write_play(&w, i, 1, 0, 0.02f, -0.9f + 0.05f * (float)i,
            0.75f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP, 0);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Render quanta of a core until the benchmark ends.
 *
 * @param b The benchmark
 * @param core The core
 */
static void bench_synth_render(
    bench_t *b,
    box_audio_core_t *core)
{
    static float left[BOX_AUDIO_CORE_QUANTUM], right[BOX_AUDIO_CORE_QUANTUM];
    box_audio_event_t events[64];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
    }

    bench_set_items(b, BOX_AUDIO_CORE_QUANTUM);
    while (bench_iter(b)) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
        box_audio_core_read_events(core, events, 64);
        bench_keep(left);
    }
}

void Synth_voices_64(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_filter_envelope(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_filter_24(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, BOX_AUDIO_SYNTH_SLOPE_24, false, false, false,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_ladder(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, BOX_AUDIO_SYNTH_SLOPE_24, false, false, false,
        0, BENCH_LADDER, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_formant(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_FORMANT, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_drive(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        1, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_filter_24_drive(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, BOX_AUDIO_SYNTH_SLOPE_24, false, false, false,
        1, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_pre_drive(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, 0, 0.6f);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_lfo(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, true, false,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_fm(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, true, false, false,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_curved(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, true,
        0, 0, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_sub(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_SUB, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_drift(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_DRIFT, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_sync(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_SYNC, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_pink(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_PINK, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_pitch(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_PITCH, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_pitch_sync(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, BOX_AUDIO_SYNTH_SLOPE_12, false, false, false,
        0, BENCH_SYNC | BENCH_PITCH, -1);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_32_pluck(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_pluck_core(1, false);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_32_pluck_vibrato(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_pluck_core(1, true);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_8_pluck_unison(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_pluck_core(4, false);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

/** Create a core with 64 looping voices of a patch of two wavetable layers of
 * WavetableHarmonics into a lowpass: one whose position the envelope opens and an LFO moves,
 * one that sweeps up between two frames, so every frame mixes two frames of a mip level and,
 * in the upper half of an octave, two levels.
 *
 * @param table Receives the floats of the table, which the caller frees after the core
 * @return The core
 */
static box_audio_core_t* bench_wavetable_core(
    float **table)
{
    static uint8_t bytes[8192];
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_synth_patch_t p = {0};
    box_audio_writer_t w;
    int32_t floats = 0;
    uint32_t i;
    ecs_os_set_api_defaults();
    *table = box_audio_wavetable_kind(WavetableHarmonics, &floats);
    box_audio_core_sound(core, 2, *table, floats, 1, 0);
    p.layers = 2;
    p.seed = 3;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.1f, .sustain = 0.6f, .hold = 2.0f, .release = 0.2f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 2000;
    p.resonance = 0.3f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 110, .level = 0.4f, .table = 2, .position = 4.5f,
        .position_envelope = 12 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 330, .frequency_end = 660, .sweep = 2.0f, .level = 0.2f, .table = 2,
        .position = 20.5f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 0.7f,
        .position = 6 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_synth(&w, 1, &p);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        box_audio_write_play(&w, i, 1, 0, 0.02f, -0.9f + 0.03f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP, 0);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

void Synth_voices_64_wavetable(
    bench_t *b)
{
    // audio/synth
    float *table = NULL;
    box_audio_core_t *core = bench_wavetable_core(&table);
    bench_synth_render(b, core);
    box_audio_core_free(core);
    ecs_os_free(table);
}
