/**
 * @file Synth.c
 * @brief Benchmarks for the synth of the audio core: 64 looping synth voices on the master,
 * one item per sample of the output, with the patch of three layers, the same with two LFOs or
<<<<<<< HEAD
 * one of FM, with curved envelopes, with a driven filter, with subs, hard synced under an
 * LFO, with pink noise, with drift or with the drive of the mix before the filter, 32
=======
 * one of FM, with curved envelopes, with a driven filter or the ladder filter, with subs, hard
 * synced under an LFO, with pink noise or with the drive of the mix before the filter, 32
>>>>>>> 19bc97c75d2d915bc010a3e9ceb596ea04e3d217
 * voices of a pluck string, steady or under a vibrato, and 8 of a unison pluck of 4.
 *
 * Each iteration renders one quantum of 128 stereo frames, so the time per item is the time
 * per sample of the whole mix of 64 voices.
 */

#include <core.h>
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

<<<<<<< HEAD
/** Extra of bench_synth_core: a drift of 15 cents moves the pitch of every layer and the
 * cutoff, and the oscillators start at free phases */
#define BENCH_DRIFT 16u
=======
/** Extra of bench_synth_core: the filter is the transistor ladder */
#define BENCH_LADDER 16u
>>>>>>> 19bc97c75d2d915bc010a3e9ceb596ea04e3d217

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
<<<<<<< HEAD
 * @param extra BENCH_SUB, BENCH_SYNC, BENCH_PITCH, BENCH_PINK and BENCH_DRIFT, or 0
=======
 * @param extra BENCH_SUB, BENCH_SYNC, BENCH_PITCH, BENCH_PINK and BENCH_LADDER, or 0
>>>>>>> 19bc97c75d2d915bc010a3e9ceb596ea04e3d217
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
            .sync = 12 };
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
