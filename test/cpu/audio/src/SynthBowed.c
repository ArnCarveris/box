/**
 * @file SynthBowed.c
 * @brief The bowed string of the synth, SynthBowed: its pitch once it settles, its harmonic
 * spectrum and the notches of the bow position, the bow speed and pressure, the end of a voice
 * after the release, legato, vibrato, the pool of strings, the routes to the bow, unison on
 * both render paths, its determinism, and the other waves, which stay bit-identical.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Level of a bowed layer, so the limiter of the master stays out of it */
#define SB_LEVEL 0.3f

/** Frames of a second at AUDIO_RATE */
#define SB_SECOND AUDIO_FRAMES

/** Largest step between two samples of a legato glide or a vibrato over the largest step of
 * the steady note before it */
#define SB_CLICK 3.0f

/** Create a core at AUDIO_RATE.
 *
 * @return The core
 */
static box_audio_core_t* sb_core(void)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);

    return core;
}

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void sb_define(
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

/** A patch of one bowed string, without bow noise, its envelope bowing for a hold and lifting
 * the bow over a short release.
 *
 * @param frequency Hz
 * @param pressure Pressure of the bow
 * @param speed Speed of the bow
 * @param position Position of the bow
 * @param hold Seconds the bow holds at full speed
 * @return The patch
 */
static box_audio_synth_patch_t sb_patch(
    float frequency,
    float pressure,
    float speed,
    float position,
    float hold)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.05f, .sustain = 1, .hold = hold, .release = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_BOWED, .frequency = frequency, .level = SB_LEVEL,
        .brightness = 0.5f, .bow_pressure = pressure, .bow_speed = speed,
        .bow_position = position };

    return p;
}

/** Render frames of a patch from its play into audio_l and audio_r.
 *
 * @param p The patch
 * @param frames Frames to render, up to AUDIO_FRAMES
 */
static void sb_render(
    const box_audio_synth_patch_t *p,
    int32_t frames)
{
    box_audio_core_t *core = sb_core();
    sb_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, frames);
    box_audio_core_free(core);
}

/** Check that every sample of a range is finite.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Whether they are
 */
static bool sb_finite(
    const float *x,
    int32_t from,
    int32_t to)
{
    bool result = true;
    int32_t i;
    for (i = from; i < to && result; i ++) {
        result = isfinite(x[i]);
    }

    return result;
}

/** Root mean square of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The RMS
 */
static double sb_rms(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * x[i];
    }

    return sqrt(sum / (to - from));
}

/** Energy of the component of a range at a frequency over a Hann window.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz
 * @return The energy
 */
static double sb_energy(
    const float *x,
    int32_t n,
    double frequency)
{
    double r = 0, m = 0, w, ph;
    int32_t i;
    for (i = 0; i < n; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / n);
        ph = 2 * AUDIO_PI * frequency * i / AUDIO_RATE;
        r += w * x[i] * cos(ph);
        m -= w * x[i] * sin(ph);
    }

    return (r * r + m * m) / ((double)n * n);
}

/** Component of a range at a frequency over a Hann window, the phase relative to frame 0.
 *
 * @param x Samples
 * @param from First frame of the window
 * @param window Frames of the window
 * @param frequency Hz
 * @param re Receives the real part
 * @param im Receives the imaginary part
 */
static void sb_component(
    const float *x,
    int32_t from,
    int32_t window,
    double frequency,
    double *re,
    double *im)
{
    double r = 0, m = 0, w, ph;
    int32_t i;
    for (i = 0; i < window; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / window);
        ph = 2 * AUDIO_PI * frequency * (double)(from + i) / AUDIO_RATE;
        r += w * x[from + i] * cos(ph);
        m -= w * x[from + i] * sin(ph);
    }

    *re = r;
    *im = m;
}

/** Frequency of the fundamental of a range near a guess, from the advance of its phase between
 * two Hann windows a gap apart, exact to a small share of a cent.
 *
 * @param x Samples
 * @param from First frame of the first window
 * @param gap Frames between the windows
 * @param window Frames of a window
 * @param guess Hz, within AUDIO_RATE / (2 gap) of it
 * @return Frequency in Hz
 */
static double sb_pitch(
    const float *x,
    int32_t from,
    int32_t gap,
    int32_t window,
    double guess)
{
    double r0, i0, r1, i1, phase;
    sb_component(x, from, window, guess, &r0, &i0);
    sb_component(x, from + gap, window, guess, &r1, &i1);
    phase = atan2(i1 * r0 - r1 * i0, r1 * r0 + i1 * i0);

    return guess + phase * AUDIO_RATE / (2 * AUDIO_PI * gap);
}

/** Share of the energy of harmonics from one to another over that of the fundamental, over a
 * window of a render.
 *
 * @param x Samples of the window
 * @param n Frames of the window
 * @param frequency Hz of the fundamental
 * @param from First harmonic
 * @param to Last harmonic
 * @return The share
 */
static double sb_harmonics(
    const float *x,
    int32_t n,
    double frequency,
    int32_t from,
    int32_t to)
{
    double high = 0;
    int32_t k;
    for (k = from; k <= to; k ++) {
        high += sb_energy(x, n, frequency * k);
    }

    return high / sb_energy(x, n, frequency);
}

/** The bow of the first layer of the synth voice of a handle that plays.
 *
 * @param core The core
 * @param handle Handle of the voice
 * @param out Receives the bow
 * @return Whether a synth voice of the handle plays
 */
static bool sb_bow(
    box_audio_core_t *core,
    uint32_t handle,
    box_audio_synth_bow_t *out)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (v->state == BOX_AUDIO_VOICE_PLAYING && v->handle == handle &&
            v->source.kind == BOX_AUDIO_SOURCE_SYNTH)
        {
            *out = v->source.data.synth.part[0]->bow;
            result = true;
        }
    }

    return result;
}

/** A bowed note settles within 5 cents of 110, 440 and 1760 Hz: the fractional delays of both
 * sides and the phase delay of the lowpass at the bridge tune the loop. */
void SynthBowed_pitch(void) {
    // audio/synth
    const float frequencies[] = { 110, 440, 1760 };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_audio_synth_patch_t p = sb_patch(frequencies[i], 0.5f, 0.5f, 0.12f, 2);
        int32_t from = SB_SECOND / 2, gap = SB_SECOND / 20, window = SB_SECOND / 10;
        double f, cents;
        sb_render(&p, SB_SECOND);
        test_assert(sb_finite(audio_l, 0, SB_SECOND));
        test_assert(audio_peak(audio_l, from, SB_SECOND) > 0.01f);
        f = sb_pitch(audio_l, from, gap, window, frequencies[i]);
        cents = 1200 * log2(f / frequencies[i]);
        printf("bowed %.0f Hz: %.4f Hz, %.3f cents\n", (double)frequencies[i], f, cents);
        test_assert(fabs(cents) < 5);
    }
}

/** The spectrum of a bowed note is harmonic: nearly all of its energy lies at the harmonics of
 * the note, little between them. */
void SynthBowed_harmonic(void) {
    // audio/synth
    box_audio_synth_patch_t p = sb_patch(220, 0.5f, 0.5f, 0.12f, 2);
    const int32_t n = 9600;
    const float *x = audio_l + SB_SECOND / 2;
    double on = 0, off = 0;
    int32_t k;
    sb_render(&p, SB_SECOND);
    for (k = 1; k <= 12; k ++) {
        on += sb_energy(x, n, 220.0 * k);
        off += sb_energy(x, n, 220.0 * (k + 0.5));
    }

    printf("bowed harmonics: %.6g at the harmonics, %.6g between\n", on, off);
    test_assert(on > 100 * off);
}

/** A bow at a fifth of the string from the bridge notches its 5th and 10th harmonics: against
 * a bow at 0.12 they fall relative to the fundamental. */
void SynthBowed_position(void) {
    // audio/synth
    const float positions[] = { 0.12f, 0.2f };
    const int32_t n = 9600;
    double share[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_audio_synth_patch_t p = sb_patch(220, 0.5f, 0.5f, positions[i], 2);
        const float *x = audio_l + SB_SECOND / 2;
        sb_render(&p, SB_SECOND);
        share[i] = (sb_energy(x, n, 1100) + sb_energy(x, n, 2200)) / sb_energy(x, n, 220);
        printf("bowed position %.2f: harmonics 5 and 10 at %.6f of the fundamental\n",
            (double)positions[i], share[i]);
    }

    test_assert(share[1] < 0.5 * share[0]);
}

/** A bow at speed 0 is silent, and the level of a note rises with the speed of the bow. */
void SynthBowed_speed(void) {
    // audio/synth
    const float speeds[] = { 0, 0.25f, 0.5f, 1 };
    double rms[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        box_audio_synth_patch_t p = sb_patch(220, 0.5f, speeds[i], 0.12f, 2);
        p.layer[0].bow_noise = 0.5f;
        sb_render(&p, SB_SECOND);
        rms[i] = sb_rms(audio_l, SB_SECOND / 2, SB_SECOND);
        printf("bowed speed %.2f: rms %.6f\n", (double)speeds[i], rms[i]);
        if (i == 0) {
            test_assert(audio_peak(audio_l, 0, SB_SECOND) == 0);
        }
    }

    test_assert(rms[0] == 0);
    test_assert(rms[1] > 0.001);
    test_assert(rms[2] > 1.2 * rms[1]);
    test_assert(rms[3] > 1.2 * rms[2]);
}

/** A harder bow gives a brighter tone: the share of harmonics 4 to 12 against the fundamental
 * rises with the pressure. */
void SynthBowed_pressure(void) {
    // audio/synth
    const float pressures[] = { 0.2f, 0.9f };
    const int32_t n = 9600;
    double share[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_audio_synth_patch_t p = sb_patch(220, pressures[i], 0.5f, 0.12f, 2);
        sb_render(&p, SB_SECOND);
        share[i] = sb_harmonics(audio_l + SB_SECOND / 2, n, 220, 4, 12);
        printf("bowed pressure %.1f: harmonics 4..12 at %.6f of the fundamental\n",
            (double)pressures[i], share[i]);
    }

    test_assert(share[1] > share[0]);
}

/** Once the release lifts the bow the string rings out through its damping, a while past the
 * end of the envelope, and the voice ends within two seconds and gives its string back to the
 * pool. */
void SynthBowed_ends(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t p = sb_patch(330, 0.5f, 0.5f, 0.12f, 0.3f);
    int32_t end = (int32_t)(0.4f * AUDIO_RATE), last = -1, i, n;
    sb_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, SB_SECOND / 10);
    test_int(core->string_free_count, BOX_AUDIO_SYNTH_STRINGS - 1);
    audio_render(core, SB_SECOND / 10, SB_SECOND - SB_SECOND / 10);
    for (n = 0; n < 2; n ++) {
        if (n == 1) {
            audio_render(core, 0, SB_SECOND);
        }

        test_assert(sb_finite(audio_l, 0, SB_SECOND));
        for (i = SB_SECOND - 1; i >= 0; i --) {
            if (audio_l[i] != 0) {
                last = n * SB_SECOND + i;
                break;
            }
        }
    }

    printf("bowed: envelope ends at %d, last sound at %d\n", (int)end, (int)last);
    test_assert(last > end + SB_SECOND / 10);
    test_assert(last < 2 * SB_SECOND - SB_SECOND / 20);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(core->string_free_count, BOX_AUDIO_SYNTH_STRINGS);
    box_audio_core_free(core);
}

/** A legato note glides the bowed string to its pitch without a new attack and without a
 * click: the level holds, no step is much larger than those of the steady note, and it lands
 * on the new pitch. */
void SynthBowed_legato(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t a = sb_patch(220, 0.5f, 0.5f, 0.12f, 2), b;
    uint8_t bytes[64];
    box_audio_writer_t w;
    float before, after;
    double low = 1e9, f, cents;
    int32_t i;
    a.glide = 0.08f;
    b = a;
    b.layer[0].frequency = 330;
    sb_define(core, 1, &a);
    sb_define(core, 2, &b);
    audio_play(core, 7, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, SB_SECOND / 2);
    before = audio_max_step(audio_l, SB_SECOND / 4, SB_SECOND / 2);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato(&w, 8, 7, 2, 0, 1, 0, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, SB_SECOND / 2);
    test_assert(sb_finite(audio_l, 0, SB_SECOND / 2));
    after = audio_max_step(audio_l, 1, SB_SECOND / 2);
    for (i = 0; i < SB_SECOND / 4; i += 480) {
        double r = sb_rms(audio_l, i, i + 480);
        low = r < low ? r : low;
    }

    f = sb_pitch(audio_l, SB_SECOND / 4, SB_SECOND / 40, SB_SECOND / 10, 330);
    cents = 1200 * log2(f / 330);
    printf("bowed legato: steps %.5f before, %.5f in the glide, lowest rms %.5f, %.3f cents\n",
        (double)before, (double)after, low, cents);
    test_assert(after < SB_CLICK * before);
    test_assert(low > 0.3 * sb_rms(audio_l, SB_SECOND / 4, SB_SECOND / 2));
    test_assert(fabs(cents) < 5);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

/** A pitch LFO moves the bowed string without a restart and without a click, around its
 * pitch. */
void SynthBowed_vibrato(void) {
    // audio/synth
    box_audio_synth_patch_t p = sb_patch(440, 0.5f, 0.5f, 0.12f, 2);
    float plain, moved;
    double f;
    sb_render(&p, SB_SECOND);
    plain = audio_max_step(audio_l, SB_SECOND / 2, SB_SECOND);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 40 };
    sb_render(&p, SB_SECOND);
    test_assert(sb_finite(audio_l, 0, SB_SECOND));
    moved = audio_max_step(audio_l, SB_SECOND / 2, SB_SECOND);
    f = sb_pitch(audio_l, SB_SECOND / 2, SB_SECOND / 5, SB_SECOND / 5, 440);
    printf("bowed vibrato: steps %.5f steady, %.5f with vibrato, %.3f Hz\n", (double)plain,
        (double)moved, f);
    test_assert(moved < SB_CLICK * plain);
    test_assert(sb_rms(audio_l, SB_SECOND / 2, SB_SECOND) >
        0.5 * sb_rms(audio_l, SB_SECOND / 4, SB_SECOND / 2));
    test_assert(fabs(1200 * log2(f / 440)) < 25);
}

/** Bowed strings share the BOX_AUDIO_SYNTH_STRINGS of the core with plucks: a play that finds
 * none free is ignored, as a pluck play is, and the strings come back when the voices end. */
void SynthBowed_pool(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t p = sb_patch(440, 0.5f, 0.5f, 0.12f, 0.05f);
    box_audio_synth_patch_t q = sb_patch(440, 0.5f, 0.5f, 0.12f, 0.05f);
    int32_t i;
    q.layer[0].voices = 4;
    q.layer[0].detune = 10;
    sb_define(core, 1, &p);
    sb_define(core, 2, &q);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS - 4; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    audio_play(core, 100, 2, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(core->string_free_count, 0);
    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    audio_play(core, 1001, 2, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 2);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS - 3);
    audio_render(core, 0, AUDIO_FRAMES);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(core->string_free_count, BOX_AUDIO_SYNTH_STRINGS);
    audio_events(core, 0, NULL);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
        audio_play(core, 2000 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS);
    box_audio_core_free(core);
}

/** Routes move the pressure, the speed and the position of the bow on top of those of the
 * layer, clamped, and a route that lowers the speed lowers the level. */
void SynthBowed_routes(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t p = sb_patch(220, 0.4f, 0.6f, 0.12f, 2), q;
    box_audio_synth_bow_t bow;
    double plain, slow;
    q = p;
    p.routes = 3;
    p.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_VELOCITY,
        .target = BOX_AUDIO_SYNTH_TO_BOW_PRESSURE, .amount = 0.3f };
    p.route[1] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_VELOCITY,
        .target = BOX_AUDIO_SYNTH_TO_BOW_SPEED, .amount = -0.4f };
    p.route[2] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_VELOCITY,
        .target = BOX_AUDIO_SYNTH_TO_BOW_POSITION, .layer = 1, .amount = 0.08f };
    sb_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, SB_SECOND / 2);
    test_assert(sb_bow(core, 1, &bow));
    printf("bowed routes: pressure %.4f, speed %.4f, position %.4f\n",
        (double)bow.pressure_play, (double)bow.speed_play, (double)bow.position_play);
    test_flt(bow.pressure_play, 0.7f);
    test_flt(bow.speed_play, 0.2f);
    test_flt(bow.position_play, 0.2f);
    slow = sb_rms(audio_l, SB_SECOND / 4, SB_SECOND / 2);
    box_audio_core_free(core);

    core = sb_core();
    p.route[0].amount = 1;
    p.route[1].amount = -1;
    p.route[2].amount = 0.5f;
    sb_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, SB_SECOND / 10);
    test_assert(sb_bow(core, 1, &bow));
    test_flt(bow.pressure_play, 1);
    test_flt(bow.speed_play, 0);
    test_flt(bow.position_play, BOX_AUDIO_SYNTH_BOW_POSITION_MAX);
    box_audio_core_free(core);

    sb_render(&q, SB_SECOND / 2);
    plain = sb_rms(audio_l, SB_SECOND / 4, SB_SECOND / 2);
    printf("bowed routes: rms %.5f at speed 0.6, %.5f at 0.2\n", plain, slow);
    test_assert(slow < 0.8 * plain);
}

/** An LFO routed to the pressure of the bow moves it around the pressure of the layer while
 * the note plays. */
void SynthBowed_routes_lfo(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t p = sb_patch(220, 0.5f, 0.5f, 0.12f, 2);
    box_audio_synth_bow_t bow;
    float low = 1, high = 0;
    int32_t i;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 4 };
    p.routes = 1;
    p.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
        .target = BOX_AUDIO_SYNTH_TO_BOW_PRESSURE, .amount = 0.3f };
    sb_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    for (i = 0; i < 25; i ++) {
        audio_render(core, 0, SB_SECOND / 50);
        test_assert(sb_bow(core, 1, &bow));
        low = bow.pressure_play < low ? bow.pressure_play : low;
        high = bow.pressure_play > high ? bow.pressure_play : high;
    }

    printf("bowed lfo: pressure %.4f to %.4f\n", (double)low, (double)high);
    test_assert(low < 0.3f && high > 0.7f);
    test_assert(low >= 0.19f && high <= 0.81f);
    box_audio_core_free(core);
}

/** The velocity of a note scales the pressure and the speed of the bow by the share of the
 * layer: a soft note is quieter. */
void SynthBowed_velocity(void) {
    // audio/synth
    box_audio_core_t *core = sb_core();
    box_audio_synth_patch_t p = sb_patch(220, 0.5f, 0.8f, 0.12f, 2);
    box_audio_synth_bow_t bow;
    uint8_t bytes[64];
    box_audio_writer_t w;
    p.layer[0].bow_velocity = 1;
    sb_define(core, 1, &p);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, 1, 1, 0, 1, 0, 1, 0.25f, 0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, SB_SECOND / 10);
    test_assert(sb_bow(core, 1, &bow));
    test_flt(bow.pressure_play, 0.125f);
    test_flt(bow.speed_play, 0.2f);
    box_audio_core_free(core);
}

/** One string per unison voice: at width 0 both channels are the same, at width 1 the strings
 * spread them apart, on the stereo path; the voices take a string each. */
void SynthBowed_unison(void) {
    // audio/synth
    box_audio_core_t *core;
    box_audio_synth_patch_t p = sb_patch(220, 0.5f, 0.5f, 0.12f, 2);
    float diff = 0;
    int32_t i;
    p.layer[0].voices = 3;
    p.layer[0].detune = 12;
    sb_render(&p, SB_SECOND / 2);
    test_assert(sb_finite(audio_l, 0, SB_SECOND / 2));
    test_assert(audio_peak(audio_l, 0, SB_SECOND / 2) > 0.01f);
    test_assert(memcmp(audio_l, audio_r, SB_SECOND / 2 * sizeof(float)) == 0);

    core = sb_core();
    p.layer[0].width = 1;
    sb_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    test_int(core->string_free_count, BOX_AUDIO_SYNTH_STRINGS - 3);
    audio_render(core, 0, SB_SECOND / 2);
    test_assert(sb_finite(audio_l, 0, SB_SECOND / 2));
    test_assert(sb_finite(audio_r, 0, SB_SECOND / 2));
    for (i = SB_SECOND / 4; i < SB_SECOND / 2; i ++) {
        float d = fabsf(audio_l[i] - audio_r[i]);
        diff = d > diff ? d : diff;
    }

    printf("bowed unison: largest difference of the channels %.5f\n", (double)diff);
    test_assert(diff > 0.005f);
    box_audio_core_free(core);
}

/** A bowed note with noise of the hair, a vibrato, routes, a glide and unison renders bit
 * identical twice, on the mono and on the stereo path. */
void SynthBowed_deterministic(void) {
    // audio/synth
    int32_t wide, n;
    for (wide = 0; wide < 2; wide ++) {
        uint64_t hash[2];
        for (n = 0; n < 2; n ++) {
            box_audio_core_t *core = sb_core();
            box_audio_synth_patch_t p = sb_patch(196, 0.6f, 0.7f, 0.1f, 0.5f);
            p.layer[0].bow_noise = 0.4f;
            p.glide = 0.05f;
            p.drift = 5;
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5.5f,
                .pitch = 20, .amplitude = 0.1f };
            p.routes = 1;
            p.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
                .target = BOX_AUDIO_SYNTH_TO_BOW_SPEED, .amount = 0.2f };
            if (wide) {
                p.layer[0].voices = 3;
                p.layer[0].detune = 15;
                p.layer[0].width = 0.8f;
            }

            sb_define(core, 1, &p);
            audio_play(core, 1, 1, 0, 1, 0, 1, 0);
            audio_render(core, 0, SB_SECOND / 2);
            audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1.25f);
            audio_render(core, SB_SECOND / 2, SB_SECOND / 2);
            test_assert(audio_peak(audio_l, 0, SB_SECOND) > 0.01f);
            hash[n] = audio_hash(audio_l, SB_SECOND) ^ (audio_hash(audio_r, SB_SECOND) * 31);
            box_audio_core_free(core);
        }

        test_assert(hash[0] == hash[1]);
    }
}

/** The fields of the bow and routes to it do nothing on the other waves: each renders bit
 * identical to without them, on the mono and the stereo path. */
void SynthBowed_others_identical(void) {
    // audio/synth
    uint32_t wave;
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        for (wave = BOX_AUDIO_SYNTH_SINE; wave <= BOX_AUDIO_SYNTH_WAVETABLE; wave ++) {
            box_audio_synth_patch_t p = {0};
            uint64_t plain, bowed;
            p.layers = 1;
            p.seed = 3;
            p.envelope = (box_audio_synth_envelope_t){
                .attack = 0.01f, .sustain = 0.8f, .hold = 0.4f, .release = 0.1f };
            p.layer[0] = (box_audio_synth_layer_t){ .wave = wave, .frequency = 220,
                .level = 0.3f, .decay = 1, .brightness = 0.5f, .hardness = 0.5f };
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
                .pitch = 15 };
            p.routes = 1;
            p.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
                .target = BOX_AUDIO_SYNTH_TO_PITCH, .amount = 0.1f };
            if (wide) {
                p.layer[0].voices = 3;
                p.layer[0].detune = 12;
                p.layer[0].width = 0.7f;
            }

            sb_render(&p, SB_SECOND);
            plain = audio_hash(audio_l, SB_SECOND) ^ (audio_hash(audio_r, SB_SECOND) * 31);
            p.layer[0].bow_pressure = 0.7f;
            p.layer[0].bow_speed = 0.4f;
            p.layer[0].bow_position = 0.3f;
            p.layer[0].bow_noise = 0.5f;
            p.layer[0].bow_velocity = 1;
            p.routes = 4;
            p.route[1] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
                .target = BOX_AUDIO_SYNTH_TO_BOW_PRESSURE, .amount = 0.5f };
            p.route[2] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
                .target = BOX_AUDIO_SYNTH_TO_BOW_SPEED, .amount = 0.5f };
            p.route[3] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
                .target = BOX_AUDIO_SYNTH_TO_BOW_POSITION, .amount = 0.2f };
            sb_render(&p, SB_SECOND);
            bowed = audio_hash(audio_l, SB_SECOND) ^ (audio_hash(audio_r, SB_SECOND) * 31);
            test_assert(plain == bowed);
        }
    }
}
