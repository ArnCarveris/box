/**
 * @file Modulation.c
 * @brief The phaser and the chorus kinds of effect in an insert slot of a bus, in offline
 * renders: the notches of the phaser measured from white noise as they move at its rate and as
 * many as its stages make, its stability at full feedback, the reads of the chorus within its
 * delay and depth, its stereo spread, the comb of its feedback as a flanger and its stability,
 * rate changes without a click, the dry mix, the silence rule, bit identical renders, and the
 * Phaser and Chorus components in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, four seconds at 48 kHz */
#define MD_MAX (AUDIO_RATE * 4)

/** Frames of the windows of the spectra */
#define MD_FFT 8192

/** Frames of a short window, for a spectrum that follows the sweep */
#define MD_SHORT 4096

/** Level of the noise and the tones */
#define MD_LEVEL 0.25f

/** Volume of the bus of the effect, so the limiter of the master never acts */
#define MD_VOLUME 0.1f

/** A level above this counts as sound */
#define MD_THRESHOLD 1e-6f

/** Frames between two clicks of the click train of the chorus */
#define MD_CLICKS 960

/** Output of the last render, and of a second render that a case compares */
static float md_l[MD_MAX];
static float md_r[MD_MAX];
static float md_b_l[MD_MAX];
static float md_b_r[MD_MAX];

/** A sound of a case: noise, a tone or a click train */
static float md_sound[MD_MAX];

/** Room of the FFT */
static double md_re[MD_FFT];
static double md_im[MD_FFT];
static double md_re_b[MD_FFT];
static double md_im_b[MD_FFT];

/** A core with an effect on bus 1, which outputs to the master */
typedef struct md_t {
    box_audio_core_t *core;  /**< The core */
    float *memory;           /**< Memory of slot 0 */
} md_t;

/** The parameters of a phaser.
 *
 * @param stages Allpasses
 * @param rate Hz
 * @param depth 0..1
 * @param center Hz
 * @param feedback -0.9..0.9
 * @param mix Wet level
 * @param stereo Offset of the right LFO in half turns
 * @return The parameters
 */
static box_audio_phaser_params_t md_phaser(
    float stages,
    float rate,
    float depth,
    float center,
    float feedback,
    float mix,
    float stereo)
{
    return (box_audio_phaser_params_t){ stages, rate, depth, center, feedback, mix, stereo };
}

/** The parameters of a chorus without feedback.
 *
 * @param voices Taps
 * @param rate Hz
 * @param depth Milliseconds
 * @param delay Milliseconds
 * @param mix Wet level
 * @param spread 0..1
 * @return The parameters
 */
static box_audio_chorus_params_t md_chorus(
    float voices,
    float rate,
    float depth,
    float delay,
    float mix,
    float spread)
{
    return (box_audio_chorus_params_t){ voices, rate, depth, delay, mix, spread, 0 };
}

/** The parameters of a chorus with feedback, a flanger.
 *
 * @param voices Taps
 * @param rate Hz
 * @param depth Milliseconds
 * @param delay Milliseconds
 * @param mix Wet level
 * @param feedback -0.95..0.95
 * @return The parameters, at a spread of 0
 */
static box_audio_chorus_params_t md_flanger(
    float voices,
    float rate,
    float depth,
    float delay,
    float mix,
    float feedback)
{
    return (box_audio_chorus_params_t){ voices, rate, depth, delay, mix, 0, feedback };
}

/** Send an effect command into insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param kind Kind
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take the effect off
 * @param p Parameters
 * @param size Bytes of the parameters
 */
static void md_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    const void *p,
    size_t size)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, p, size);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, kind, bus, 0, BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Send a phaser command for slot 0 on bus 1.
 *
 * @param core The core
 * @param p Parameters
 */
static void md_phaser_command(
    box_audio_core_t *core,
    box_audio_phaser_params_t p)
{
    md_command(core, 0, BOX_AUDIO_EFFECT_PHASER, 1, &p, sizeof(p));
}

/** Send a chorus command for slot 0 on bus 1.
 *
 * @param core The core
 * @param p Parameters
 */
static void md_chorus_command(
    box_audio_core_t *core,
    box_audio_chorus_params_t p)
{
    md_command(core, 0, BOX_AUDIO_EFFECT_CHORUS, 1, &p, sizeof(p));
}

/** Fill md_sound with white noise of a seed, the same on every run.
 *
 * @param frames Frames
 * @param seed Seed
 */
static void md_noise(
    int32_t frames,
    uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        x = x * 1664525u + 1013904223u;
        md_sound[i] = ((float)(x >> 8) / 8388608.0f - 1.0f) * MD_LEVEL;
    }
}

/** Create a core with bus 1 into the master at MD_VOLUME, memory for slot 0 when the kind
 * needs it, and md_sound of some frames as sound 1, which plays on bus 1 when play.
 *
 * @param md Receives the core
 * @param kind Kind whose memory slot 0 gets, BOX_AUDIO_EFFECT_NONE for none
 * @param frames Frames of the sound
 * @param loop Whether it loops
 * @param play Whether it plays
 */
static void md_open(
    md_t *md,
    uint32_t kind,
    int32_t frames,
    bool loop,
    bool play)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(kind, AUDIO_RATE);
    md->core = box_audio_core_new(&desc);
    test_assert(md->core != NULL);
    md->memory = NULL;
    if (floats > 0) {
        md->memory = malloc((size_t)floats * sizeof(float));
        test_assert(md->memory != NULL);

        // Memory that is not clear: the chorus never reads what it did not write
        audio_fill(md->memory, floats, 0.75f);
        test_assert(box_audio_core_effect_memory(md->core, 0, md->memory, floats));
    }

    audio_order(md->core, routes, 2);
    audio_bus_set(md->core, 1, BOX_AUDIO_PARAM_VOLUME, MD_VOLUME);
    test_assert(box_audio_core_sound(md->core, 1, md_sound, frames, 1, AUDIO_RATE));
    if (play) {
        audio_play(md->core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);
    }
}

/** Free the core, then its memory.
 *
 * @param md The core
 */
static void md_close(
    md_t *md)
{
    box_audio_core_free(md->core);
    free(md->memory);
}

/** Render frames into two channels from a frame on, in renders of a size.
 *
 * @param core The core
 * @param l Left channel
 * @param r Right channel
 * @param at First frame
 * @param frames Number of frames
 * @param size Frames per render
 */
static void md_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t i;
    test_assert(at + frames <= MD_MAX);
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, l + at + i, r + at + i, n);
    }
}

/** Stop a voice at once.
 *
 * @param core The core
 * @param voice Handle
 */
static void md_stop(
    box_audio_core_t *core,
    uint32_t voice)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, voice, 0));
    box_audio_core_apply(core, w.data, w.size);
}

/** Largest second difference of a range, which a click raises far above that of a tone.
 *
 * @param x Samples
 * @param from First frame, at least 2
 * @param to End frame
 * @return The largest magnitude
 */
static float md_max_curve(
    const float *x,
    int32_t from,
    int32_t to)
{
    float result = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        float c = fabsf(x[i] - 2 * x[i - 1] + x[i - 2]);
        result = c > result ? c : result;
    }

    return result;
}

/** In place radix 2 FFT.
 *
 * @param re Real parts
 * @param im Imaginary parts
 * @param n Size, a power of 2
 */
static void md_fft(
    double *re,
    double *im,
    int32_t n)
{
    int32_t i, j, len, k;
    for (i = 1, j = 0; i < n; i ++) {
        int32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }

        j ^= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }

    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * AUDIO_PI / (double)len;
        for (i = 0; i < n; i += len) {
            for (k = 0; k < len / 2; k ++) {
                double wr = cos(a * k), wi = sin(a * k);
                double *ur = &re[i + k], *ui = &im[i + k];
                double *vr = &re[i + k + len / 2], *vi = &im[i + k + len / 2];
                double xr = *vr * wr - *vi * wi, xi = *vr * wi + *vi * wr;
                *vr = *ur - xr;
                *vi = *ui - xi;
                *ur += xr;
                *ui += xi;
            }
        }
    }
}

/** Take the Hann windowed spectra of an input and an output over a window.
 *
 * @param x Input
 * @param y Output
 * @param from First frame
 * @param n Frames of the window, a power of 2
 */
static void md_spectra(
    const float *x,
    const float *y,
    int32_t from,
    int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i ++) {
        double w = 0.5 - 0.5 * cos(2 * AUDIO_PI * (double)i / (double)n);
        md_re[i] = (double)x[from + i] * w;
        md_im[i] = 0;
        md_re_b[i] = (double)y[from + i] * w;
        md_im_b[i] = 0;
    }

    md_fft(md_re, md_im, n);
    md_fft(md_re_b, md_im_b, n);
}

/** Find the deepest notch of the gain from an input to an output over a window, between two
 * frequencies, from the powers of both smoothed over 5 bins.
 *
 * @param x Input
 * @param y Output
 * @param from First frame
 * @param lo Lowest frequency in Hz
 * @param hi Highest frequency in Hz
 * @return The frequency of the notch in Hz
 */
static double md_notch(
    const float *x,
    const float *y,
    int32_t from,
    double lo,
    double hi)
{
    double hz = (double)AUDIO_RATE / MD_SHORT, best = 1e30, result = 0;
    int32_t k, j, k0 = (int32_t)(lo / hz), k1 = (int32_t)(hi / hz);
    md_spectra(x, y, from, MD_SHORT);
    for (k = k0; k <= k1; k ++) {
        double px = 0, py = 0;
        for (j = k - 2; j <= k + 2; j ++) {
            px += md_re[j] * md_re[j] + md_im[j] * md_im[j];
            py += md_re_b[j] * md_re_b[j] + md_im_b[j] * md_im_b[j];
        }

        if (px > 0 && py / px < best) {
            best = py / px;
            result = k * hz;
        }
    }

    return result;
}

/** The first notch of a phaser of 4 stages at a break frequency: where each allpass turns the
 * phase by a quarter of the half turn, tan(pi f / rate) = tan(pi fc / rate) tan(pi / 8).
 *
 * @param fc Break frequency in Hz
 * @return Frequency of the notch in Hz
 */
static double md_first_notch(
    double fc)
{
    double t = tan(AUDIO_PI * fc / AUDIO_RATE) * tan(AUDIO_PI / 8);

    return atan(t) * AUDIO_RATE / AUDIO_PI;
}

void Modulation_phaser_notch_moves(void) {
    // audio/phaser
    md_t md;
    float rate = 0.5f, depth = 0.5f, center = 800;
    int32_t frames = MD_MAX, from, windows = 0, c;
    double lowest[2] = { 1e30, 1e30 }, highest[2] = { 0, 0 };
    md_noise(MD_MAX, 1);

    // The input: the same core at a mix of 0, which is dry
    md_open(&md, BOX_AUDIO_EFFECT_NONE, MD_MAX, true, true);
    md_phaser_command(md.core, md_phaser(4, rate, depth, center, 0, 0, 0.5f));
    md_render(md.core, md_b_l, md_b_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    md_open(&md, BOX_AUDIO_EFFECT_NONE, MD_MAX, true, true);
    md_phaser_command(md.core, md_phaser(4, rate, depth, center, 0, 0.5f, 0.5f));
    md_render(md.core, md_l, md_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    for (from = AUDIO_RATE / 10; from + MD_SHORT <= frames; from += MD_SHORT / 2) {
        double t = (double)(from + MD_SHORT / 2) / AUDIO_RATE;
        for (c = 0; c < 2; c ++) {
            double turns = t * rate + (c ? 0.25 : 0.0);
            double fc = center * pow(2, 2 * depth * sin(2 * AUDIO_PI * turns));
            double expect = md_first_notch(fc);

            // Up to the break frequency, which lies between the first notch and the second
            double got = md_notch(c ? md_b_r : md_b_l, c ? md_r : md_l, from, 80, fc);
            double octaves = log2(got / expect);
            if (windows % 8 == 0) {
                printf("%.3f s %s: notch at %.0f Hz, %.0f Hz expected\n", t,
                    c ? "right" : "left", got, expect);
            }

            test_assert(fabs(octaves) < 0.2);
            lowest[c] = got < lowest[c] ? got : lowest[c];
            highest[c] = got > highest[c] ? got : highest[c];
        }

        windows ++;
    }

    // Two octaves of sweep: from about 165 to 663 Hz
    for (c = 0; c < 2; c ++) {
        printf("%s: notch from %.0f to %.0f Hz\n", c ? "right" : "left", lowest[c],
            highest[c]);
        test_assert(highest[c] / lowest[c] > 3.0);
    }
}

/** Measure the gain of a static phaser from white noise with the cross spectrum of its input
 * and its output, and count its notches between 20 Hz and 20 kHz: dips under 0.2 after the
 * gain was over 0.6.
 *
 * @param stages Allpasses
 * @return Notches
 */
static int32_t md_notches(
    int32_t stages)
{
    static double sxx[MD_FFT / 2], sxy_re[MD_FFT / 2], sxy_im[MD_FFT / 2];
    md_t md;
    int32_t frames = AUDIO_RATE * 2, from, k, result = 0;
    double hz = (double)AUDIO_RATE / MD_FFT;
    bool high = true;
    md_noise(MD_MAX, 2);
    md_open(&md, BOX_AUDIO_EFFECT_NONE, MD_MAX, true, true);
    md_phaser_command(md.core, md_phaser((float)stages, 0, 0, 800, 0, 0, 0));
    md_render(md.core, md_b_l, md_b_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    md_open(&md, BOX_AUDIO_EFFECT_NONE, MD_MAX, true, true);
    md_phaser_command(md.core, md_phaser((float)stages, 0, 0, 800, 0, 0.5f, 0));
    md_render(md.core, md_l, md_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    memset(sxx, 0, sizeof(sxx));
    memset(sxy_re, 0, sizeof(sxy_re));
    memset(sxy_im, 0, sizeof(sxy_im));
    for (from = AUDIO_RATE / 10; from + MD_FFT <= frames; from += MD_FFT / 2) {
        md_spectra(md_b_l, md_l, from, MD_FFT);
        for (k = 0; k < MD_FFT / 2; k ++) {
            sxx[k] += md_re[k] * md_re[k] + md_im[k] * md_im[k];
            sxy_re[k] += md_re_b[k] * md_re[k] + md_im_b[k] * md_im[k];
            sxy_im[k] += md_im_b[k] * md_re[k] - md_re_b[k] * md_im[k];
        }
    }

    for (k = (int32_t)(20 / hz); k < (int32_t)(20000 / hz); k ++) {
        double g = sqrt(sxy_re[k] * sxy_re[k] + sxy_im[k] * sxy_im[k]) / sxx[k];
        if (high && g < 0.2) {
            result ++;
            high = false;
            printf("%d stages: notch %d at %.0f Hz\n", stages, result, k * hz);
        } else if (!high && g > 0.6) {
            high = true;
        }
    }

    return result;
}

void Modulation_phaser_stages_notches(void) {
    // audio/phaser
    int32_t stages;
    for (stages = BOX_AUDIO_PHASER_STAGES_MIN; stages <= BOX_AUDIO_PHASER_STAGES_MAX;
        stages += 2)
    {
        test_int(md_notches(stages), stages / 2);
    }

    // An odd count rounds to an even one
    test_int(md_notches(5), 3);
}

void Modulation_phaser_feedback_stable(void) {
    // audio/phaser
    float feedbacks[] = { 0.9f, -0.9f, 5.0f, -5.0f, NAN };
    md_t md;
    int32_t f, i, second;
    for (f = 0; f < 5; f ++) {
        float peak = 0, first = 0, last = 0;
        md_noise(AUDIO_RATE, 3 + (uint32_t)f);
        for (i = 0; i < AUDIO_RATE; i += 400) {
            md_sound[i] = MD_LEVEL;
        }

        md_open(&md, BOX_AUDIO_EFFECT_NONE, AUDIO_RATE, true, true);
        md_phaser_command(md.core, md_phaser(12, 5, 1, 800, feedbacks[f], 1, 1));
        for (second = 0; second < 4; second ++) {
            md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
            for (i = 0; i < AUDIO_RATE; i ++) {
                test_assert(isfinite(md_l[i]) && isfinite(md_r[i]));
                peak = fabsf(md_l[i]) > peak ? fabsf(md_l[i]) : peak;
                peak = fabsf(md_r[i]) > peak ? fabsf(md_r[i]) : peak;
            }

            first = second == 0 ? audio_peak(md_l, AUDIO_RATE / 2, AUDIO_RATE) : first;
            last = audio_peak(md_l, AUDIO_RATE / 2, AUDIO_RATE);

            // A sweep of the center while the feedback rings
            md_phaser_command(md.core, md_phaser(12, 5, 1, 200 + 2000 * (float)second,
                feedbacks[f], 1, 1));
        }

        // The resonance of a feedback of 0.9 raises the peaks at most tenfold, and the output
        // does not grow: well under the limiter of the master
        printf("feedback %.1f: peak %.4f, %.4f in the first second, %.4f in the last\n",
            (double)feedbacks[f], (double)peak, (double)first, (double)last);
        test_assert(peak > 0);
        test_assert(peak < 0.5f);
        test_assert(last < first * 3);

        // Then the input stops and the tail decays to silence
        md_stop(md.core, 1);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        test_assert(!box_audio_core_effect_running(md.core, 0));
        test_flt(audio_peak(md_l, AUDIO_RATE / 2, AUDIO_RATE), 0);
        md_close(&md);
    }
}

/** Fill md_sound with a click train: a sample of 1 every spacing frames from spacing / 2.
 *
 * @param frames Frames
 * @param spacing Frames between two clicks
 */
static void md_click_train(
    int32_t frames,
    int32_t spacing)
{
    int32_t i;
    memset(md_sound, 0, sizeof(float) * (size_t)frames);
    for (i = spacing / 2; i < frames; i += spacing) {
        md_sound[i] = 1;
    }
}

/** Find the delay of the echo of each click of the click train through a chorus at a mix of
 * 1, as the centroid of the echo, which the linear interpolation puts at the fraction of its
 * read.
 *
 * @param p Parameters of the chorus
 * @param spacing Frames between two clicks, longer than the longest echo
 * @param delays Receives the delay of each click in milliseconds
 * @param lo Receives the earliest frame of any sound after a click, in frames
 * @param hi Receives the latest
 * @return Number of clicks
 */
static int32_t md_echoes(
    box_audio_chorus_params_t p,
    int32_t spacing,
    double *delays,
    int32_t *lo,
    int32_t *hi)
{
    md_t md;
    int32_t frames = AUDIO_RATE * 2, click, i, count = 0;
    md_click_train(frames, spacing);
    md_open(&md, BOX_AUDIO_EFFECT_CHORUS, frames, false, true);
    md_chorus_command(md.core, p);
    md_render(md.core, md_l, md_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    *lo = spacing;
    *hi = 0;
    for (click = spacing / 2; click + spacing <= frames; click += spacing) {
        double sum = 0, moment = 0;
        for (i = 1; i < spacing; i ++) {
            double a = fabs((double)md_l[click + i]) + fabs((double)md_r[click + i]);
            sum += a;
            moment += a * i;
            if (a > MD_THRESHOLD) {
                *lo = i < *lo ? i : *lo;
                *hi = i > *hi ? i : *hi;
            }
        }

        test_assert(sum > 0);
        delays[count ++] = moment / sum * 1000.0 / AUDIO_RATE;
    }

    return count;
}

void Modulation_chorus_delay_range(void) {
    // audio/chorus
    double delays[AUDIO_RATE * 2 / MD_CLICKS], lowest = 1e30, highest = 0;
    float depth = 3, delay = 12, rate = 1;
    int32_t count, i, lo, hi, voices;
    count = md_echoes(md_chorus(1, rate, depth, delay, 1, 0), MD_CLICKS, delays, &lo, &hi);
    test_assert(count > 40);
    for (i = 0; i < count; i ++) {
        // The LFO starts at phase 0 at the first frame and reads an echo when it arrives
        double click = MD_CLICKS / 2 + i * MD_CLICKS;
        double t = (click + delays[i] * AUDIO_RATE / 1000.0) / AUDIO_RATE;
        double expect = delay + depth * sin(2 * AUDIO_PI * rate * t);
        if (i % 10 == 0) {
            printf("click %d: echo after %.3f ms, %.3f ms expected\n", i, delays[i], expect);
        }

        test_assert(fabs(delays[i] - expect) < 0.1);
        test_assert(delays[i] >= delay - depth - 0.03);
        test_assert(delays[i] <= delay + depth + 0.03);
        lowest = delays[i] < lowest ? delays[i] : lowest;
        highest = delays[i] > highest ? delays[i] : highest;
    }

    printf("one voice: echoes from %.3f to %.3f ms\n", lowest, highest);
    test_assert(lowest < delay - depth * 0.95);
    test_assert(highest > delay + depth * 0.95);

    // Every voice reads within the delay and the depth
    for (voices = 2; voices <= BOX_AUDIO_CHORUS_VOICES_MAX; voices ++) {
        md_echoes(md_chorus((float)voices, rate, depth, delay, 1, 1), MD_CLICKS,
            delays, &lo, &hi);
        printf("%d voices: sound from %d to %d frames after a click\n", voices, lo, hi);
        test_assert(lo >= (int32_t)((delay - depth) * AUDIO_RATE / 1000) - 1);
        test_assert(hi <= (int32_t)((delay + depth) * AUDIO_RATE / 1000) + 2);
    }

    // A read never goes further back than 50 ms, nor nearer than a frame
    md_echoes(md_chorus(1, 0, 25, 100, 1, 0), MD_CLICKS * 4, delays, &lo, &hi);
    test_int(lo, (int32_t)(BOX_AUDIO_CHORUS_MAX * AUDIO_RATE / 1000));
    test_int(hi, lo);
    md_echoes(md_chorus(NAN, NAN, NAN, NAN, 1, NAN), MD_CLICKS, delays, &lo, &hi);
    test_int(lo, 1);
    test_int(hi, 1);
}

/** Gain of a static chorus from white noise per bin of MD_FFT, from the cross spectrum of its
 * input and its output */
static double md_comb_gain[MD_FFT / 2];

/** Measure the gain of a static chorus into md_comb_gain: its input is the same core at a mix
 * of 0, which is dry.
 *
 * @param p Parameters of the chorus, at a rate or a depth of 0
 */
static void md_comb(
    box_audio_chorus_params_t p)
{
    static double sxx[MD_FFT / 2], sxy_re[MD_FFT / 2], sxy_im[MD_FFT / 2];
    box_audio_chorus_params_t dry = p;
    md_t md;
    int32_t frames = AUDIO_RATE * 3, from, k;
    md_noise(MD_MAX, 11);
    dry.mix = 0;
    md_open(&md, BOX_AUDIO_EFFECT_CHORUS, MD_MAX, true, true);
    md_chorus_command(md.core, dry);
    md_render(md.core, md_b_l, md_b_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    md_open(&md, BOX_AUDIO_EFFECT_CHORUS, MD_MAX, true, true);
    md_chorus_command(md.core, p);
    md_render(md.core, md_l, md_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    memset(sxx, 0, sizeof(sxx));
    memset(sxy_re, 0, sizeof(sxy_re));
    memset(sxy_im, 0, sizeof(sxy_im));
    for (from = AUDIO_RATE / 2; from + MD_FFT <= frames; from += MD_FFT / 2) {
        md_spectra(md_b_l, md_l, from, MD_FFT);
        for (k = 0; k < MD_FFT / 2; k ++) {
            sxx[k] += md_re[k] * md_re[k] + md_im[k] * md_im[k];
            sxy_re[k] += md_re_b[k] * md_re[k] + md_im_b[k] * md_im[k];
            sxy_im[k] += md_im_b[k] * md_re[k] - md_re_b[k] * md_im[k];
        }
    }

    for (k = 0; k < MD_FFT / 2; k ++) {
        md_comb_gain[k] = sxx[k] > 0 ?
            sqrt(sxy_re[k] * sxy_re[k] + sxy_im[k] * sxy_im[k]) / sxx[k] : 0;
    }
}

/** The largest or the smallest gain of md_comb_gain within 30 Hz of a frequency.
 *
 * @param hz The frequency
 * @param largest Whether to take the largest
 * @return The gain
 */
static double md_comb_at(
    double hz,
    bool largest)
{
    double bin = (double)AUDIO_RATE / MD_FFT, result = largest ? 0 : 1e30;
    int32_t k, k0 = (int32_t)((hz - 30) / bin), k1 = (int32_t)((hz + 30) / bin) + 1;
    for (k = k0; k <= k1; k ++) {
        double g = md_comb_gain[k];
        result = largest ? (g > result ? g : result) : (g < result ? g : result);
    }

    return result;
}

void Modulation_chorus_feedback_comb(void) {
    // audio/chorus
    double none[10], peaks[10], dips[10];
    int32_t k;

    // One tap 1 ms back at a mix of 0.5: without feedback a comb of peaks of 1 at multiples
    // of 1 kHz, with a feedback of 0.8 peaks of 0.5 + 0.5 / 0.2 = 3 and dips of 0.22
    md_comb(md_flanger(1, 0, 0, 1, 0.5f, 0));
    for (k = 1; k <= 10; k ++) {
        none[k - 1] = md_comb_at(k * 1000.0, true);
    }

    md_comb(md_flanger(1, 0, 0, 1, 0.5f, 0.8f));
    for (k = 1; k <= 10; k ++) {
        peaks[k - 1] = md_comb_at(k * 1000.0, true);
        dips[k - 1] = md_comb_at(k * 1000.0 - 500.0, false);
        printf("%d kHz: peak %.3f, %.3f without feedback, dip at %.1f kHz %.3f\n", k,
            peaks[k - 1], none[k - 1], k - 0.5, dips[k - 1]);
        test_assert(peaks[k - 1] > 2.0);
        test_assert(peaks[k - 1] > none[k - 1] * 2.0);
        test_assert(dips[k - 1] < 0.4);
    }

    // A negative feedback moves the peaks to the odd multiples of 500 Hz, of 0.5 + 0.5 / 0.2,
    // and leaves 0.5 + 0.5 / 1.8 at the multiples of 1 kHz
    md_comb(md_flanger(1, 0, 0, 1, 0.5f, -0.8f));
    for (k = 1; k <= 10; k ++) {
        double odd = md_comb_at(k * 1000.0 - 500.0, true);
        double even = md_comb_at(k * 1000.0, true);
        printf("feedback -0.8: peak at %.1f kHz %.3f, %.3f at %d kHz\n", k - 0.5, odd, even,
            k);
        test_assert(odd > 1.5);
        test_assert(odd > even * 1.8);
        test_assert(even < 1.0);
    }

    // Half a frame past 48 frames the peaks sit at multiples of 48000 / 48.5 Hz; the cubic
    // read of the feedback keeps a loop gain of 0.8 * 0.94 near 10 kHz, where a linear one
    // would keep 0.8 * 0.79 and a peak of 1.6
    md_comb(md_flanger(1, 0, 0, 48.5f / 48.0f, 0.5f, 0.8f));
    for (k = 1; k <= 10; k ++) {
        double peak = md_comb_at(k * 48000.0 / 48.5, true);
        printf("delay 48.5 frames: peak at %.0f Hz %.3f\n", k * 48000.0 / 48.5, peak);
        test_assert(peak > (k < 8 ? 2.2 : 1.9));
    }
}

void Modulation_chorus_feedback_stable(void) {
    // audio/chorus
    box_audio_chorus_params_t cases[] = {
        md_flanger(1, 0, 0, 1, 1, 0.95f),
        md_flanger(1, 0, 0, 0.1f, 0.5f, -0.95f),
        md_flanger(1, 0.3f, 0.1f, 0.1f, 1, 0.95f),
        md_flanger(1, 0.2f, 1.8f, 2, 0.5f, 0.95f),
        md_flanger(4, 20, 25, 25, 1, -0.95f),
        md_flanger(3, 5, 10, 50, 1, 0.95f),
        md_flanger(2, 0.5f, 5, 5, 1, 5),
        md_flanger(4, 1, 0.5f, 0, 1, -5),
        md_flanger(1, 0.1f, 0, 0, 1, NAN)
    };
    int32_t count = (int32_t)(sizeof(cases) / sizeof(cases[0]));
    int32_t c, i, second;
    for (c = 0; c < count; c ++) {
        md_t md;
        int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE);
        float line = 0;

        // Loud noise with clicks, at full scale
        md_noise(AUDIO_RATE, 20 + (uint32_t)c);
        for (i = 0; i < AUDIO_RATE; i ++) {
            md_sound[i] = i % 480 == 0 ? 1.0f : md_sound[i] * (1.0f / MD_LEVEL);
        }

        md_open(&md, BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE, true, true);
        md_chorus_command(md.core, cases[c]);
        for (second = 0; second < 10; second ++) {
            float peak;
            md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
            for (i = 0; i < AUDIO_RATE; i ++) {
                test_assert(isfinite(md_l[i]) && isfinite(md_r[i]));
            }

            // The loop gain is at most 0.95 at every frequency, so the lines stay far under 20
            // times the peak of the input
            peak = 0;
            for (i = 0; i < floats; i ++) {
                float a = fabsf(md.memory[i]);
                test_assert(isfinite(md.memory[i]));
                peak = a > peak ? a : peak;
            }

            if (second % 3 == 0 || second == 9) {
                printf("case %d: lines peak %.3f in second %d\n", c, (double)peak, second);
            }

            line = peak > line ? peak : line;
        }

        test_assert(line > 0);
        test_assert(line <= 20.5f);
        md_close(&md);
    }
}

void Modulation_chorus_feedback_tail(void) {
    // audio/chorus
    float feedbacks[] = { 0.95f, -0.95f, 0.5f };
    md_t md;
    int32_t f, frames = AUDIO_RATE / 4;
    for (f = 0; f < 3; f ++) {
        md_noise(frames, 30 + (uint32_t)f);
        md_open(&md, BOX_AUDIO_EFFECT_CHORUS, frames, false, false);
        md_chorus_command(md.core, md_flanger(2, 0.5f, 1, 4, 0.5f, feedbacks[f]));
        audio_play(md.core, 1, 1, 1, 1, 0, 1, 0);

        // The noise ends after 250 ms; a feedback of 0.95 still rings 100 ms later, past the
        // 50 ms after which a chorus without feedback goes idle
        md_render(md.core, md_l, md_r, 0, frames + AUDIO_RATE / 10, BOX_AUDIO_CORE_QUANTUM);
        if (fabsf(feedbacks[f]) > 0.9f) {
            test_assert(box_audio_core_effect_running(md.core, 0));
            test_assert(audio_peak(md_l, frames + AUDIO_RATE / 20,
                frames + AUDIO_RATE / 10) > 1e-5f);
        }

        // At 0.95 a pass of at most 5 ms decays by 0.45 dB: under the silence within 2
        // seconds
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE * 4, BOX_AUDIO_CORE_QUANTUM);
        printf("feedback %.2f: tail %.2e at 1 s, %.2e at 3 s\n", (double)feedbacks[f],
            (double)audio_peak(md_l, AUDIO_RATE - 480, AUDIO_RATE),
            (double)audio_peak(md_l, AUDIO_RATE * 3 - 480, AUDIO_RATE * 3));
        test_assert(!box_audio_core_effect_running(md.core, 0));
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        test_flt(audio_peak(md_l, 0, AUDIO_RATE), 0);
        test_flt(audio_peak(md_r, 0, AUDIO_RATE), 0);

        // It starts again on the next sound, with nothing left of the last
        audio_play(md.core, 2, 1, 1, 1, 0, 1, 0);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
        test_assert(box_audio_core_effect_running(md.core, 0));
        test_assert(audio_peak(md_l, 0, AUDIO_RATE / 8) > 0.001f);
        md_close(&md);
    }
}

/** Render noise through a chorus whose parameters, rate and voices change, in renders of
 * BOX_AUDIO_CORE_QUANTUM, and hash the output; the voice ends, so the chorus idles and starts
 * again.
 *
 * @param floats Floats of the params of each command: 6 sends no feedback, as before the
 * feedback was added, 7 sends it
 * @param feedback The feedback of every command
 * @return The hash of both channels
 */
static uint64_t md_feedback_sized(
    size_t floats,
    float feedback)
{
    md_t md;
    int32_t k, frames = AUDIO_RATE / 2;
    md_noise(frames, 40);
    md_open(&md, BOX_AUDIO_EFFECT_CHORUS, frames, false, true);
    for (k = 0; k < 8; k ++) {
        box_audio_chorus_params_t p = md_flanger((float)(1 + k % 4), 0.5f + (float)k,
            0.5f + (float)k, 1 + 3 * (float)k, 0.6f, feedback);
        p.spread = 0.15f * (float)k;
        md_command(md.core, 0, BOX_AUDIO_EFFECT_CHORUS, 1, &p, floats * sizeof(float));
        md_render(md.core, md_l, md_r, k * (AUDIO_RATE / 4), AUDIO_RATE / 4,
            BOX_AUDIO_CORE_QUANTUM);
    }

    audio_play(md.core, 2, 1, 1, 1, 0.2f, 1, 0);
    md_render(md.core, md_l, md_r, 2 * AUDIO_RATE, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);

    return audio_hash(md_l, AUDIO_RATE * 3) ^ (audio_hash(md_r, AUDIO_RATE * 3) * 31);
}

void Modulation_chorus_feedback_zero_identical(void) {
    // audio/chorus
    uint64_t before = md_feedback_sized(6, 0);
    uint64_t zero = md_feedback_sized(7, 0);
    uint64_t negative_zero = md_feedback_sized(7, -0.0f);
    uint64_t some = md_feedback_sized(7, 0.3f);
    test_assert(before == zero);
    test_assert(before == negative_zero);
    test_assert(before != some);
}

/** Correlation of the channels of a range.
 *
 * @param l Left channel
 * @param r Right channel
 * @param from First frame
 * @param to End frame
 * @return The correlation, -1..1
 */
static double md_correlation(
    const float *l,
    const float *r,
    int32_t from,
    int32_t to)
{
    double lr = 0, ll = 0, rr = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        lr += (double)l[i] * r[i];
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
    }

    return ll > 0 && rr > 0 ? lr / sqrt(ll * rr) : 1;
}

/** Render mono noise through a chorus of 3 voices at a mix of 1 and a spread.
 *
 * @param spread The spread
 * @return The correlation of the channels after the first 100 ms
 */
static double md_spread(
    float spread)
{
    md_t md;
    double result;
    int32_t i, frames = AUDIO_RATE * 2;
    md_noise(AUDIO_RATE, 5);
    md_open(&md, BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE, true, true);
    md_chorus_command(md.core, md_chorus(3, 0.8f, 3, 12, 1, spread));
    md_render(md.core, md_l, md_r, 0, frames, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
    test_assert(audio_peak(md_l, AUDIO_RATE / 10, frames) > 0.001f);
    result = md_correlation(md_l, md_r, AUDIO_RATE / 10, frames);
    if (spread == 0) {
        for (i = 0; i < frames; i ++) {
            test_assert(md_l[i] == md_r[i]);
        }
    }

    return result;
}

void Modulation_chorus_spread(void) {
    // audio/chorus
    double none = md_spread(0), half = md_spread(0.5f), full = md_spread(1);
    printf("correlation of the channels: %.3f at a spread of 0, %.3f at 0.5, %.3f at 1\n",
        none, half, full);
    test_flt(none, 1);
    test_assert(half < 0.9);
    test_assert(full < half - 0.2);
    test_assert(full < 0.4);
}

/** Render a sine through an effect, change its rate four times and compare the largest
 * second difference of the output before and after, which a jump of the phase of its LFO
 * would raise.
 *
 * @param kind BOX_AUDIO_EFFECT_PHASER or BOX_AUDIO_EFFECT_CHORUS
 * @param before Receives the largest second difference of the second before the changes
 * @param after Receives the largest of the second of the changes
 */
static void md_rate_change(
    uint32_t kind,
    float *before,
    float *after)
{
    md_t md;
    int32_t at = AUDIO_RATE, k;
    audio_sine(md_sound, AUDIO_RATE, 440, MD_LEVEL);
    md_open(&md, kind, AUDIO_RATE, true, true);
    if (kind == BOX_AUDIO_EFFECT_PHASER) {
        md_phaser_command(md.core, md_phaser(6, 0.5f, 1, 800, 0, 0.5f, 0.5f));
    } else {
        md_chorus_command(md.core, md_chorus(3, 0.8f, 3, 12, 0.5f, 1));
    }

    md_render(md.core, md_l, md_r, 0, at, BOX_AUDIO_CORE_QUANTUM);
    *before = md_max_curve(md_l, AUDIO_RATE / 10, at);
    for (k = 0; k < 4; k ++) {
        // A change of the rate at every quarter of a second, at frames that are no block edge
        md_render(md.core, md_l, md_r, at + k * (AUDIO_RATE / 4), AUDIO_RATE / 4 - 37,
            BOX_AUDIO_CORE_QUANTUM);
        if (kind == BOX_AUDIO_EFFECT_PHASER) {
            md_phaser_command(md.core, md_phaser(6, k % 2 ? 0.5f : 7, 1, 800, 0, 0.5f,
                0.5f));
        } else {
            md_chorus_command(md.core, md_chorus(3, k % 2 ? 0.8f : 6, 3, 12, 0.5f, 1));
        }

        md_render(md.core, md_l, md_r, at + (k + 1) * (AUDIO_RATE / 4) - 37, 37,
            BOX_AUDIO_CORE_QUANTUM);
    }

    *after = md_max_curve(md_l, at, at + AUDIO_RATE);
    if (md_max_curve(md_r, at, at + AUDIO_RATE) > *after) {
        *after = md_max_curve(md_r, at, at + AUDIO_RATE);
    }

    md_close(&md);
}

void Modulation_rate_change_no_click(void) {
    // audio/phaser
    double w = 2 * AUDIO_PI * 440 / AUDIO_RATE;
    float curve = (float)(w * w) * MD_LEVEL * MD_VOLUME, before, after;
    md_rate_change(BOX_AUDIO_EFFECT_PHASER, &before, &after);
    printf("phaser: largest second difference %.7f before, %.7f with changes of the rate, "
        "%.7f for the sine\n", (double)before, (double)after, (double)curve);
    test_assert(after < before * 1.5f);
    test_assert(after < curve * 1.5f);

    // The voices of the chorus sum up to 1.7 times the tone at a spread of 1, and a rate of
    // 6 Hz raises the pitch of the taps by up to 11%
    md_rate_change(BOX_AUDIO_EFFECT_CHORUS, &before, &after);
    printf("chorus: largest second difference %.7f before, %.7f with changes of the rate, "
        "%.7f for the sine\n", (double)before, (double)after, (double)curve);
    test_assert(after < before * 1.5f);
    test_assert(after < curve * 2.5f);
}

/** Render a looping sine on bus 1, through an effect or none.
 *
 * @param kind Kind of the effect, BOX_AUDIO_EFFECT_NONE for none
 * @param p Parameters
 * @param size Bytes of the parameters
 * @param l Receives two seconds of the left channel
 * @param r Receives the right channel
 */
static void md_tone(
    uint32_t kind,
    const void *p,
    size_t size,
    float *l,
    float *r)
{
    md_t md;
    audio_sine(md_sound, AUDIO_RATE, 330, MD_LEVEL);
    md_open(&md, kind, AUDIO_RATE, false, false);
    audio_play(md.core, 1, 1, 1, 1, 0.3f, 1, BOX_AUDIO_PLAY_LOOP);
    if (kind != BOX_AUDIO_EFFECT_NONE) {
        md_command(md.core, 0, kind, 1, p, size);
    }

    md_render(md.core, l, r, 0, AUDIO_RATE * 2, BOX_AUDIO_CORE_QUANTUM);
    md_close(&md);
}

void Modulation_mix_dry(void) {
    // audio/phaser
    box_audio_phaser_params_t ph = md_phaser(12, 3, 1, 500, 0.9f, 0, 1);
    box_audio_chorus_params_t ch = md_chorus(4, 5, 10, 30, 0, 1);
    int32_t i;
    md_tone(BOX_AUDIO_EFFECT_NONE, NULL, 0, md_l, md_r);
    test_assert(audio_peak(md_l, 0, AUDIO_RATE * 2) > 0.01f);
    md_tone(BOX_AUDIO_EFFECT_PHASER, &ph, sizeof(ph), md_b_l, md_b_r);
    for (i = 0; i < AUDIO_RATE * 2; i ++) {
        test_assert(md_l[i] == md_b_l[i]);
        test_assert(md_r[i] == md_b_r[i]);
    }

    md_tone(BOX_AUDIO_EFFECT_CHORUS, &ch, sizeof(ch), md_b_l, md_b_r);
    for (i = 0; i < AUDIO_RATE * 2; i ++) {
        test_assert(md_l[i] == md_b_l[i]);
        test_assert(md_r[i] == md_b_r[i]);
    }
}

void Modulation_silent_after_tail(void) {
    // audio/chorus
    md_t md;
    int32_t kind;
    for (kind = 0; kind < 2; kind ++) {
        uint32_t k = kind ? BOX_AUDIO_EFFECT_CHORUS : BOX_AUDIO_EFFECT_PHASER;
        md_noise(AUDIO_RATE / 4, 6);
        md_open(&md, k, AUDIO_RATE / 4, false, false);
        if (kind) {
            md_chorus_command(md.core, md_chorus(4, 1, 5, 40, 0.5f, 1));
        } else {
            md_phaser_command(md.core, md_phaser(8, 1, 1, 400, 0.5f, 0.5f, 0.5f));
        }

        test_assert(!box_audio_core_effect_running(md.core, 0));
        audio_play(md.core, 1, 1, 1, 1, 0, 1, 0);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
        test_assert(box_audio_core_effect_running(md.core, 0));
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        test_assert(!box_audio_core_effect_running(md.core, 0));
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
        test_flt(audio_peak(md_l, 0, AUDIO_RATE), 0);
        test_flt(audio_peak(md_r, 0, AUDIO_RATE), 0);

        // It starts again on the next sound
        audio_play(md.core, 2, 1, 1, 1, 0, 1, 0);
        md_render(md.core, md_l, md_r, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
        test_assert(box_audio_core_effect_running(md.core, 0));
        test_assert(audio_peak(md_l, 0, AUDIO_RATE / 8) > 0.001f);
        md_close(&md);
    }
}

/** Render noise and a tone through a phaser on bus 1 and a chorus on bus 2, whose parameters,
 * rates, stages and voices change, in renders of a size, and hash the output.
 *
 * Which block the tail of an effect decays in, and so the frame from which it outputs
 * silence rather than a tail under -140 dB, depends on the render sizes: renders of other
 * sizes keep the noise looping.
 *
 * @param size Frames per render
 * @param idle Whether the noise ends, so that the phaser idles, and starts again
 * @return The hash of both channels
 */
static uint64_t md_sized(
    int32_t size,
    bool idle)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE), k;
    float *memory = malloc((size_t)floats * sizeof(float));
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL && memory != NULL);
    audio_fill(memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(core, 1, memory, floats));
    audio_order(core, routes, 3);
    md_noise(AUDIO_RATE / 2, 7);
    audio_sine(md_sound + AUDIO_RATE / 2, AUDIO_RATE / 2, 220, MD_LEVEL);
    test_assert(box_audio_core_sound(core, 1, md_sound, AUDIO_RATE / 2, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, md_sound + AUDIO_RATE / 2, AUDIO_RATE / 2, 1,
        AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.5f, -0.4f, 1, idle ? 0 : BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 0.5f, 0.6f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 3, 1, 2, 0.5f, 0, 1.5f, 0);
    for (k = 0; k < 8; k ++) {
        box_audio_phaser_params_t ph = md_phaser((float)(2 + 2 * (k % 3)),
            0.3f + 1.7f * (float)k, 0.2f + 0.1f * (float)k, 300 + 200 * (float)k,
            k % 2 ? 0.7f : -0.4f, 0.5f, 0.25f * (float)(k % 5));
        box_audio_chorus_params_t ch = md_chorus((float)(1 + k % 4), 0.5f + (float)k,
            1 + (float)k, 8 + 3 * (float)k, 0.6f, 0.15f * (float)k);
        md_command(core, 0, BOX_AUDIO_EFFECT_PHASER, 1, &ph, sizeof(ph));
        md_command(core, 1, BOX_AUDIO_EFFECT_CHORUS, 2, &ch, sizeof(ch));
        md_render(core, md_l, md_r, k * (AUDIO_RATE / 4), AUDIO_RATE / 4, size);
    }

    // The voice on bus 1 ended: the phaser idles while the LFO runs on, then a new play
    if (idle) {
        audio_play(core, 4, 1, 1, 0.5f, 0, 1, 0);
    }

    md_render(core, md_l, md_r, 2 * AUDIO_RATE, AUDIO_RATE / 2, size);
    box_audio_core_free(core);
    free(memory);

    return audio_hash(md_l, AUDIO_RATE * 5 / 2) ^
        (audio_hash(md_r, AUDIO_RATE * 5 / 2) * 31);
}

void Modulation_render_identical(void) {
    // audio/chorus
    uint64_t a = md_sized(BOX_AUDIO_CORE_QUANTUM, true);
    uint64_t b = md_sized(BOX_AUDIO_CORE_QUANTUM, true);
    uint64_t c = md_sized(BOX_AUDIO_CORE_QUANTUM, false), d = md_sized(100, false);
    uint64_t e = md_sized(1000, false);
    test_assert(a == b);
    test_assert(a != c);
    test_assert(c == d);
    test_assert(c == e);
}

void Modulation_components(void) {
    // audio/phaser
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world), f = ecs_new(world);
    const Phaser *ph;
    const Chorus *ch;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    ecs_add(world, e, Phaser);
    ph = ecs_get(world, e, Phaser);
    test_int(ph->stages, 4);
    test_flt(ph->rate, 0.5f);
    test_flt(ph->depth, 0.7f);
    test_flt(ph->center, 800);
    test_flt(ph->feedback, 0);
    test_flt(ph->mix, 0.5f);
    test_flt(ph->stereo, 0.5f);
    test_assert(ecs_has(world, e, AudioEffectState));
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_PHASER);
    test_flt(params[0], 4);
    test_flt(params[1], 0.5f);
    test_flt(params[2], 0.7f);
    test_flt(params[3], 800);
    test_flt(params[4], 0);
    test_flt(params[5], 0.5f);
    test_flt(params[6], 0.5f);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_PHASER, AUDIO_RATE), 0);

    ecs_add(world, f, Chorus);
    ch = ecs_get(world, f, Chorus);
    test_int(ch->voices, 2);
    test_flt(ch->rate, 0.8f);
    test_flt(ch->depth, 3);
    test_flt(ch->delay, 12);
    test_flt(ch->mix, 0.5f);
    test_flt(ch->spread, 0.5f);
    test_flt(ch->feedback, 0);
    test_assert(ecs_has(world, f, AudioEffectState));
    test_int(box_audio_module_effect_kind(world, f, params), BOX_AUDIO_EFFECT_CHORUS);
    test_flt(params[0], 2);
    test_flt(params[1], 0.8f);
    test_flt(params[2], 3);
    test_flt(params[3], 12);
    test_flt(params[4], 0.5f);
    test_flt(params[5], 0.5f);
    test_flt(params[6], 0);
    ecs_set(world, f, Chorus, { .voices = 1, .rate = 0.2f, .depth = 1.8f, .delay = 2,
        .mix = 0.5f, .feedback = -0.7f });
    test_int(box_audio_module_effect_kind(world, f, params), BOX_AUDIO_EFFECT_CHORUS);
    test_flt(params[3], 2);
    test_flt(params[6], -0.7f);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE),
        ((int32_t)(BOX_AUDIO_CHORUS_MAX * 0.001f * AUDIO_RATE) + 2) * 2);
    ecs_fini(world);
}

void Modulation_script(void) {
    // audio/chorus
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/modulation.flecs");
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "modulation_scene" }),
        .code = code
    });
    ecs_entity_t keys, pad, tone, source;
    const Phaser *ph;
    const Chorus *ch;
    ecs_os_free(code);
    test_assert(script != 0);
    audio_run(world, 2);
    keys = ecs_lookup(world, "keys");
    pad = ecs_lookup(world, "pad");
    test_assert(keys != 0 && pad != 0);
    ph = ecs_get(world, keys, Phaser);
    ch = ecs_get(world, pad, Chorus);
    test_assert(ph != NULL && ch != NULL);
    test_int(ph->stages, 6);
    test_flt(ph->rate, 0.3f);
    test_flt(ph->feedback, 0.5f);
    test_int(ch->voices, 3);
    test_flt(ch->spread, 1);
    test_flt(ch->mix, 0.4f);
    test_assert(box_audio_module_effect_slot(world, keys) >= 0);
    test_assert(box_audio_module_effect_slot(world, pad) >= 0);

    // Both run on a tone and keep it finite
    tone = audio_tone(world, "tone", 440, 0.25f, AUDIO_RATE);
    source = audio_source(world, tone, 1, true, keys);
    audio_source(world, tone, 1, true, pad);
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME * 10,
        audio_tap_count) > 0.01f);
    test_assert(audio_peak(audio_tap_r, audio_tap_count - AUDIO_FRAME * 10,
        audio_tap_count) > 0.01f);
    ecs_delete(world, source);
    ecs_fini(world);
}
