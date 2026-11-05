/**
 * @file SynthDrift.c
 * @brief Analog drift of a synth voice: SynthSound.drift, a smooth seeded random walk of the
 * pitch of each layer and unison voice and of the cutoff, and SynthSound.free_phase, random
 * start phases. Plays on a core, and the sounds of test/assets/audio/synth_drift.flecs on a
 * world with the null output.
 */

#include "world.h"
#include <stdio.h>
#include <string.h>

/** Frames of a window of the measure of the pitch, 50 ms */
#define DRIFT_WINDOW 2400

/** Seconds of the long notes of the tests */
#define DRIFT_SECONDS 4

/** Windows of a long note */
#define DRIFT_WINDOWS (DRIFT_SECONDS * AUDIO_FRAMES / DRIFT_WINDOW)

/** Samples of the renders of a test, left and right */
static float drift_l[DRIFT_SECONDS * AUDIO_FRAMES];
static float drift_r[DRIFT_SECONDS * AUDIO_FRAMES];

/** Cents of each window of a render */
static double drift_cents_l[DRIFT_WINDOWS];
static double drift_cents_r[DRIFT_WINDOWS];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void drift_define(
    box_audio_core_t *core,
    uint32_t sound,
    const box_audio_synth_patch_t *p)
{
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, sound, p));
    box_audio_core_apply(core, w.data, w.size);
}

/** A sine of 1000 Hz at a steady level for a while, with a drift.
 *
 * @param drift Cents of the drift
 * @param hold Seconds at the level
 * @return The patch
 */
static box_audio_synth_patch_t drift_sine(
    float drift,
    float hold)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 3;
    p.drift = drift;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = hold, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 1000, .level = 0.5f };

    return p;
}

/** Play a sound on a fresh voice of a core and render frames of it into drift_l and drift_r,
 * a second at a time.
 *
 * @param core The core
 * @param voice Id of the voice
 * @param sound Id of the sound
 * @param frames Frames, up to DRIFT_SECONDS seconds
 */
static void drift_render(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    int32_t frames)
{
    int32_t at, n;
    audio_play(core, voice, sound, 0, 1, 0, 1, 0);
    for (at = 0; at < frames; at += n) {
        n = frames - at < AUDIO_FRAMES ? frames - at : AUDIO_FRAMES;
        audio_render(core, 0, n);
        memcpy(drift_l + at, audio_l, (size_t)n * sizeof(float));
        memcpy(drift_r + at, audio_r, (size_t)n * sizeof(float));
    }
}

/** Cents of the pitch of each window of DRIFT_WINDOW frames of a render over a frequency.
 *
 * @param x Samples
 * @param windows Windows
 * @param frequency Hz the cents are relative to
 * @param out Receives the cents of each window
 */
static void drift_cents(
    const float *x,
    int32_t windows,
    double frequency,
    double *out)
{
    int32_t w;
    for (w = 0; w < windows; w ++) {
        double f = audio_frequency(x, w * DRIFT_WINDOW, (w + 1) * DRIFT_WINDOW);
        test_assert(f > 0);
        out[w] = 1200 * log2(f / frequency);
    }
}

/** Check the cents of the windows of a render: each within the drift, plus a little for the
 * measure, none over 2 cents from the last, and their range over a cent.
 *
 * @param cents The cents of each window
 * @param windows Windows
 * @param drift Cents of the drift
 */
static void drift_check(
    const double *cents,
    int32_t windows,
    double drift)
{
    double lo = cents[0], hi = cents[0];
    int32_t w;
    for (w = 0; w < windows; w ++) {
        test_assert(fabs(cents[w]) <= drift + 0.2);
        if (w > 0) {
            test_assert(fabs(cents[w] - cents[w - 1]) < 2);
        }

        lo = cents[w] < lo ? cents[w] : lo;
        hi = cents[w] > hi ? cents[w] : hi;
    }

    printf("drift %.0f: %.2f..%.2f cents\n", drift, lo, hi);
    test_assert(hi - lo > 1);
}

/** With a drift of 20 cents the pitch of a long sine, in windows of 50 ms, moves, stays
 * within 20 cents and never jumps 2 cents from one window to the next, for several seeds. */
void SynthDrift_pitch_bounded(void) {
    // audio/synth
    box_audio_synth_patch_t p = drift_sine(20, DRIFT_SECONDS + 1);
    uint32_t seed;
    for (seed = 1; seed <= 3; seed ++) {
        box_audio_core_t *core = audio_new();
        p.seed = seed;
        drift_define(core, 1, &p);
        drift_render(core, 1, 1, DRIFT_SECONDS * AUDIO_FRAMES);
        drift_cents(drift_l, DRIFT_WINDOWS, 1000, drift_cents_l);
        drift_check(drift_cents_l, DRIFT_WINDOWS, 20);
        box_audio_core_free(core);
    }
}

/** The drift of the cutoff leaves the drift of the pitch as it is and a filtered sine as
 * smooth: through a lowpass, the pitch still moves within the drift. */
void SynthDrift_pitch_bounded_filter(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(20, DRIFT_SECONDS + 1);
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 3000;
    p.resonance = 0.3f;
    drift_define(core, 1, &p);
    drift_render(core, 1, 1, 2 * AUDIO_FRAMES);
    drift_cents(drift_l + DRIFT_WINDOW, 2 * AUDIO_FRAMES / DRIFT_WINDOW - 1, 1000,
        drift_cents_l);
    drift_check(drift_cents_l, 2 * AUDIO_FRAMES / DRIFT_WINDOW - 1, 20);
    box_audio_core_free(core);
}

/** Two unison voices without detune, one on each channel at full width, each drift on their
 * own walk within the drift: their pitches differ. */
void SynthDrift_unison_differ(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(20, DRIFT_SECONDS + 1);
    double most = 0;
    int32_t w;
    p.layer[0].voices = 2;
    p.layer[0].detune = 0;
    p.layer[0].width = 1;
    drift_define(core, 1, &p);
    drift_render(core, 1, 1, DRIFT_SECONDS * AUDIO_FRAMES);
    drift_cents(drift_l, DRIFT_WINDOWS, 1000, drift_cents_l);
    drift_cents(drift_r, DRIFT_WINDOWS, 1000, drift_cents_r);
    drift_check(drift_cents_l, DRIFT_WINDOWS, 20);
    drift_check(drift_cents_r, DRIFT_WINDOWS, 20);
    for (w = 0; w < DRIFT_WINDOWS; w ++) {
        double d = fabs(drift_cents_l[w] - drift_cents_r[w]);
        most = d > most ? d : most;
    }

    test_assert(most > 1);
    box_audio_core_free(core);
}

/** Render a sequence of plays with drift and free phases into a hash: a saw with a unison
 * layer and a sine through a lowpass, three plays.
 *
 * @return The hash
 */
static uint64_t drift_sequence(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(30, 0.3f);
    uint64_t result = 0;
    uint32_t voice;
    p.layers = 2;
    p.free_phase = 1;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 1500;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220,
        .level = 0.2f, .voices = 3, .detune = 10, .width = 0.5f, .sub = 0.3f };
    drift_define(core, 1, &p);
    for (voice = 1; voice <= 3; voice ++) {
        audio_play(core, voice, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES / 2);
        result = result * 31 + audio_hash(audio_l, AUDIO_FRAMES / 2);
        result = result * 31 + audio_hash(audio_r, AUDIO_FRAMES / 2);
    }

    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);

    return result;
}

/** Two renders of the same sequence of plays are bit identical. */
void SynthDrift_renders_identical(void) {
    // audio/synth
    uint64_t a = drift_sequence(), b = drift_sequence();
    test_assert(a == b);
}

/** Two successive plays of a sound with drift differ, where without drift they are the same. */
void SynthDrift_plays_differ(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(0, 0.4f);
    drift_define(core, 1, &p);
    p.drift = 20;
    drift_define(core, 2, &p);

    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    memcpy(drift_l, audio_l, AUDIO_FRAMES / 2 * sizeof(float));
    audio_play(core, 2, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(memcmp(drift_l, audio_l, AUDIO_FRAMES / 2 * sizeof(float)) == 0);

    audio_play(core, 3, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    memcpy(drift_l, audio_l, AUDIO_FRAMES / 2 * sizeof(float));
    audio_play(core, 4, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(memcmp(drift_l, audio_l, AUDIO_FRAMES / 2 * sizeof(float)) != 0);
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** Render the start of a play of a sound into drift_l.
 *
 * @param core The core
 * @param voice Id of the voice
 * @param sound Id of the sound
 */
static void drift_start(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound)
{
    audio_play(core, voice, sound, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    memcpy(drift_l + 4800 * (voice % 3), audio_l, 4800 * sizeof(float));
}

/** free_phase starts the oscillator of each play at another phase, so the first samples of
 * successive plays differ, a sine and a saw with a sub; without it every play starts at
 * phase 0 and the same. */
void SynthDrift_free_phase(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(0, 0.05f);
    uint32_t sound;
    for (sound = 1; sound <= 2; sound ++) {
        if (sound == 2) {
            p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
            p.layer[0].sub = 0.5f;
            p.layer[0].sub_octave = 2;
        }

        p.free_phase = 0;
        drift_define(core, 1, &p);
        drift_start(core, 1, 1);
        drift_start(core, 2, 1);
        test_assert(memcmp(drift_l + 4800, drift_l + 9600, 4800 * sizeof(float)) == 0);
        if (sound == 1) {
            test_assert(fabs(drift_l[4800]) < 1e-6);
        }

        p.free_phase = 1;
        drift_define(core, 1, &p);
        drift_start(core, 3, 1);
        drift_start(core, 4, 1);
        drift_start(core, 5, 1);
        test_assert(drift_l[1] != drift_l[4801]);
        test_assert(drift_l[4801] != drift_l[9601]);
        test_assert(drift_l[1] != drift_l[9601]);
        test_assert(memcmp(drift_l, drift_l + 4800, 4800 * sizeof(float)) != 0);
        test_assert(memcmp(drift_l + 4800, drift_l + 9600, 4800 * sizeof(float)) != 0);
    }

    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** Render two plays of a patch into a hash, on the voice slot of a core.
 *
 * @param p The patch
 * @return The hash of both channels of both plays
 */
static uint64_t drift_hash(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = audio_new();
    uint64_t result = 0;
    uint32_t voice;
    drift_define(core, 1, p);
    for (voice = 1; voice <= 2; voice ++) {
        audio_play(core, voice, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES / 2);
        result = result * 31 + audio_hash(audio_l, AUDIO_FRAMES / 2);
        result = result * 31 + audio_hash(audio_r, AUDIO_FRAMES / 2);
    }

    box_audio_core_free(core);

    return result;
}

/** At their defaults, and with a drift that clamps to 0, drift and free_phase leave a patch
 * bit identical: a mono patch with an LFO, a filter envelope and phase modulation, a wide
 * unison patch through a filter and a pluck.
 *
 * free_phase moves the mono patch only: unison voices, subs included, and strings start as
 * they did. */
void SynthDrift_defaults_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = drift_sine(0, 0.3f), q;
    int32_t pass;
    for (pass = 0; pass < 3; pass ++) {
        if (pass == 0) {
            p.layers = 2;
            p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE,
                .frequency = 1500, .modulates = 1, .index = 1.5f };
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){ .rate = 5, .pitch = 20, .cutoff = 0.5f };
            p.filter = BOX_AUDIO_SYNTH_LOWPASS;
            p.cutoff = 900;
            p.filter_amount = 2;
            p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.05f, .decay = 0.1f };
        } else if (pass == 1) {
            p = drift_sine(0, 0.3f);
            p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
            p.layer[0].voices = 4;
            p.layer[0].detune = 15;
            p.layer[0].width = 0.8f;
            p.layer[0].sub = 0.3f;
            p.filter = BOX_AUDIO_SYNTH_LOWPASS;
            p.cutoff = 1200;
        } else {
            p = drift_sine(0, 0.3f);
            p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_PLUCK,
                .frequency = 196, .level = 0.5f, .decay = 0.5f, .brightness = 0.5f,
                .hardness = 0.8f, .voices = 2, .detune = 6, .width = 0.5f, .rings_free = 1 };
        }

        q = p;
        q.drift = -5;
        test_assert(drift_hash(&p) == drift_hash(&q));
        q.drift = NAN;
        test_assert(drift_hash(&p) == drift_hash(&q));
        q.drift = 10;
        test_assert(drift_hash(&p) != drift_hash(&q));
        q = p;
        q.free_phase = 1;
        test_assert((drift_hash(&p) != drift_hash(&q)) == (pass == 0));
    }
}

/** The core clamps the drift to 50 cents. */
void SynthDrift_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = drift_sine(50, 0.3f), q = p;
    q.drift = 500;
    test_assert(drift_hash(&p) == drift_hash(&q));
}

/** Hz of a tone with harmonics from the lag in 100..400 frames at which it best matches
 * itself, refined by a parabola through its neighbours.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, over 400 frames past from
 * @return The Hz
 */
static double drift_period_frequency(
    const float *x,
    int32_t from,
    int32_t to)
{
    double score[401] = {0}, a, b, c, lag;
    int32_t best = 100, k, i;
    for (k = 100; k <= 400; k ++) {
        double sum = 0, energy = 0;
        for (i = from; i < to - k; i ++) {
            sum += (double)x[i] * (double)x[i + k];
            energy += (double)x[i + k] * (double)x[i + k] + (double)x[i] * (double)x[i];
        }

        score[k] = energy > 0 ? 2 * sum / energy : 0;
        best = score[k] > score[best] ? k : best;
    }

    lag = best;
    if (best > 100 && best < 400) {
        a = score[best - 1];
        b = score[best];
        c = score[best + 1];
        lag += a - 2 * b + c != 0 ? 0.5 * (a - c) / (a - 2 * b + c) : 0;
    }

    return AUDIO_RATE / lag;
}

/** A pluck string follows the drift as it follows vibrato: it renders otherwise than without,
 * its pitch stays near that of the string, and it ends. */
void SynthDrift_pluck_follows(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = drift_sine(0, 0);
    uint64_t steady;
    double f;
    int32_t i;
    p.envelope = (box_audio_synth_envelope_t){0};
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 200,
        .level = 0.5f, .decay = 0.8f, .brightness = 0, .hardness = 0.5f, .rings_free = 1 };
    drift_define(core, 1, &p);
    p.drift = 40;
    drift_define(core, 2, &p);
    drift_render(core, 1, 1, AUDIO_FRAMES);
    steady = audio_hash(drift_l, AUDIO_FRAMES);
    drift_render(core, 2, 2, AUDIO_FRAMES);
    test_assert(audio_hash(drift_l, AUDIO_FRAMES) != steady);
    f = drift_period_frequency(drift_l, 4800, 24000);
    test_assert(fabs(1200 * log2(f / 200)) < 40 + 10);
    for (i = 0; i < 4; i ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** The sounds of test/assets/audio/synth_drift.flecs read back as authored, compile into
 * their patches, play and end; a change of the drift compiles the sound again. */
void SynthDrift_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_drift.flecs");
    ecs_entity_t pad, pluck, steady;
    const AudioSynth *synth;
    int32_t start;
    test_assert(ecs_script_run(world, "synth_drift", code, NULL) == 0);
    ecs_os_free(code);
    pad = ecs_lookup(world, "DriftPad");
    pluck = ecs_lookup(world, "DriftPluck");
    steady = ecs_lookup(world, "Steady");
    test_assert(pad && pluck && steady);
    test_flt(ecs_get(world, pad, SynthSound)->drift, 12);
    test_bool(ecs_get(world, pad, SynthSound)->free_phase, true);
    test_flt(ecs_get(world, steady, SynthSound)->drift, 0);
    test_bool(ecs_get(world, steady, SynthSound)->free_phase, false);
    audio_run(world, 1);
    synth = ecs_get(world, pad, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.drift, 12);
    test_int(synth->patch.free_phase, 1);
    synth = ecs_get(world, pluck, AudioSynth);
    test_flt(synth->patch.drift, 8);
    test_int(synth->patch.free_phase, 0);
    synth = ecs_get(world, steady, AudioSynth);
    test_flt(synth->patch.drift, 0);
    test_int(synth->patch.free_phase, 0);

    start = audio_tap_count;
    test_assert(box_audio_play(world, pad, 1.0f));
    test_assert(box_audio_play(world, pluck, 1.0f));
    audio_run(world, 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    audio_run(world, 300);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    ecs_set(world, steady, SynthSound, { .drift = 25, .free_phase = true });
    audio_run(world, 1);
    synth = ecs_get(world, steady, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.drift, 25);
    test_int(synth->patch.free_phase, 1);
    ecs_fini(world);
}
