/**
 * @file SynthNoise.c
 * @brief Colors of the noise of synth layers, measured on offline renders of a core: the slope
 * of the spectrum of pink and brown noise at 48 and 96 kHz, their RMS level against white
 * noise, the seeding, white noise as it was, unison voices, the render paths of LFOs and
 * modulation, the waves that ignore it, the clamping of the field, and a script.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Largest size of the FFT of a spectrum, a window of 0.17 s at 96 kHz */
#define NOISE_FFT 16384

/** Seconds of noise a spectrum or a level averages over */
#define NOISE_SECONDS 20

/** Seconds skipped before a measure, past the attack and the settling of the filters */
#define NOISE_SKIP 0.5

/** Frames of the renders a test compares */
#define NOISE_FRAMES 24000

/** Room of the FFT */
static double noise_re[NOISE_FFT];
static double noise_im[NOISE_FFT];

/** Twiddles of the FFT, for the size noise_twiddles_n */
static double noise_wr[NOISE_FFT / 2];
static double noise_wi[NOISE_FFT / 2];
static int32_t noise_twiddles_n;

/** Power of each bin of the last spectrum, summed over its windows */
static double noise_power[NOISE_FFT / 2];

/** Samples of the renders a test compares */
static float noise_a_l[NOISE_FRAMES];
static float noise_a_r[NOISE_FRAMES];

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* noise_core(
    float rate)
{
    box_audio_core_desc_t desc = { .sample_rate = rate };
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
static void noise_define(
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

/** A patch of one noise layer of a color at a steady level for 30 seconds.
 *
 * @param color A box_audio_synth_color_t
 * @param seed The seed
 * @return The patch
 */
static box_audio_synth_patch_t noise_patch(
    uint32_t color,
    uint32_t seed)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = seed;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 30, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f, .color = color };

    return p;
}

/** Create a core at a rate that plays a patch, and render past NOISE_SKIP.
 *
 * @param p The patch
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* noise_start(
    const box_audio_synth_patch_t *p,
    float rate)
{
    box_audio_core_t *core = noise_core(rate);
    noise_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, (int32_t)(NOISE_SKIP * (double)rate));

    return core;
}

/** Render a patch at a rate in a new core into audio_l and audio_r, NOISE_FRAMES from the
 * start of the play.
 *
 * @param p The patch
 * @param rate Frames per second
 */
static void noise_render(
    const box_audio_synth_patch_t *p,
    float rate)
{
    box_audio_core_t *core = noise_core(rate);
    noise_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, NOISE_FRAMES);
    box_audio_core_free(core);
}

/** Render a patch at a rate in a new core into noise_a_l and noise_a_r, as noise_render.
 *
 * @param p The patch
 * @param rate Frames per second
 */
static void noise_render_a(
    const box_audio_synth_patch_t *p,
    float rate)
{
    noise_render(p, rate);
    memcpy(noise_a_l, audio_l, sizeof(noise_a_l));
    memcpy(noise_a_r, audio_r, sizeof(noise_a_r));
}

/** Whether audio_l and audio_r are bit identical to noise_a_l and noise_a_r.
 *
 * @return Whether they are
 */
static bool noise_same_a(void) {
    return memcmp(audio_l, noise_a_l, sizeof(noise_a_l)) == 0 &&
        memcmp(audio_r, noise_a_r, sizeof(noise_a_r)) == 0;
}

/** In place radix 2 FFT.
 *
 * @param re Real parts
 * @param im Imaginary parts
 * @param n Size, a power of 2, at most NOISE_FFT
 */
static void noise_fft(
    double *re,
    double *im,
    int32_t n)
{
    int32_t i, j, len, k, step;
    if (noise_twiddles_n != n) {
        for (k = 0; k < n / 2; k ++) {
            noise_wr[k] = cos(-2 * AUDIO_PI * k / n);
            noise_wi[k] = sin(-2 * AUDIO_PI * k / n);
        }

        noise_twiddles_n = n;
    }

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
        step = n / len;
        for (i = 0; i < n; i += len) {
            for (k = 0; k < len / 2; k ++) {
                double wr = noise_wr[k * step], wi = noise_wi[k * step];
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

/** Sum the Hann windowed power spectra of NOISE_SECONDS of a patch into noise_power, after
 * NOISE_SKIP, in windows of a bin every 5.86 Hz.
 *
 * @param p The patch
 * @param rate Frames per second, 48000 or 96000
 * @param right Whether to take the right channel instead of the left
 * @return Size of the FFT
 */
static int32_t noise_spectrum(
    const box_audio_synth_patch_t *p,
    float rate,
    bool right)
{
    int32_t n = rate > 50000 ? 16384 : 8192;
    int32_t windows = (int32_t)(NOISE_SECONDS * (double)rate) / n, w, i;
    box_audio_core_t *core = noise_start(p, rate);
    const float *x = right ? audio_r : audio_l;
    memset(noise_power, 0, sizeof(noise_power));
    for (w = 0; w < windows; w ++) {
        audio_render(core, 0, n);
        for (i = 0; i < n; i ++) {
            noise_re[i] = (double)x[i] * (0.5 - 0.5 * cos(2 * AUDIO_PI * i / n));
            noise_im[i] = 0;
        }

        noise_fft(noise_re, noise_im, n);
        for (i = 0; i < n / 2; i ++) {
            noise_power[i] += noise_re[i] * noise_re[i] + noise_im[i] * noise_im[i];
        }
    }

    box_audio_core_free(core);

    return n;
}

/** Mean power per bin of noise_power in the octave around a frequency, from half an octave
 * under it to half an octave over it.
 *
 * @param n Size of the FFT
 * @param rate Frames per second
 * @param center Hz
 * @return The mean power
 */
static double noise_band(
    int32_t n,
    float rate,
    double center)
{
    double lo = center / sqrt(2.0), hi = center * sqrt(2.0), sum = 0, f;
    int32_t i, count = 0;
    for (i = 1; i < n / 2; i ++) {
        f = (double)i * (double)rate / n;
        if (f >= lo && f < hi) {
            sum += noise_power[i];
            count ++;
        }
    }

    test_assert(count > 0);

    return sum / count;
}

/** Check that the spectrum of a patch falls by a slope per octave between octave bands, from
 * each of a list of frequencies to twice it.
 *
 * @param p The patch
 * @param rate Frames per second
 * @param right Whether to measure the right channel
 * @param from Frequencies in Hz, the lower band of each step
 * @param count Number of them
 * @param slope dB per octave it falls by
 * @param tolerance dB the fall of each octave may be off by
 */
static void noise_slope(
    const box_audio_synth_patch_t *p,
    float rate,
    bool right,
    const double *from,
    int32_t count,
    double slope,
    double tolerance)
{
    int32_t n = noise_spectrum(p, rate, right), i;
    for (i = 0; i < count; i ++) {
        double fall = 10 * log10(noise_band(n, rate, from[i]) /
            noise_band(n, rate, 2 * from[i]));
        printf("%.0f Hz: %.0f to %.0f Hz falls %.2f dB\n", (double)rate, from[i],
            2 * from[i], fall);
        test_assert(fabs(fall - slope) <= tolerance);
    }
}

/** Octaves the slope of pink noise is checked over, from 100 Hz to 10 kHz */
static const double noise_pink_octaves[] = { 100, 200, 400, 800, 1600, 3200, 5000 };

/** Octaves the slope of brown noise is checked over, from 100 Hz to 5 kHz */
static const double noise_brown_octaves[] = { 100, 200, 400, 800, 1600, 2500 };

void SynthNoise_pink_slope_48000(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_PINK, 7);
    noise_slope(&p, 48000, false, noise_pink_octaves, 7, 3, 1);
}

void SynthNoise_pink_slope_96000(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_PINK, 7);
    noise_slope(&p, 96000, false, noise_pink_octaves, 7, 3, 1);
}

void SynthNoise_brown_slope_48000(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_BROWN, 7);
    noise_slope(&p, 48000, false, noise_brown_octaves, 6, 6, 1.5);
}

void SynthNoise_brown_slope_96000(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_BROWN, 7);
    noise_slope(&p, 96000, false, noise_brown_octaves, 6, 6, 1.5);
}

/** White noise stays flat: the same check of its octaves finds no slope. */
void SynthNoise_white_flat(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_WHITE, 7);
    noise_slope(&p, 48000, false, noise_pink_octaves, 7, 0, 0.5);
}

/** RMS level and mean of NOISE_SECONDS of a patch after NOISE_SKIP.
 *
 * @param p The patch
 * @param rate Frames per second
 * @param mean Receives the mean
 * @return The RMS level
 */
static double noise_rms(
    const box_audio_synth_patch_t *p,
    float rate,
    double *mean)
{
    box_audio_core_t *core = noise_start(p, rate);
    int32_t chunks = NOISE_SECONDS * 2, chunk = (int32_t)rate / 2, c, i;
    double sum = 0, sum2 = 0;
    for (c = 0; c < chunks; c ++) {
        audio_render(core, 0, chunk);
        for (i = 0; i < chunk; i ++) {
            sum += (double)audio_l[i];
            sum2 += (double)audio_l[i] * (double)audio_l[i];
        }
    }

    box_audio_core_free(core);
    *mean = sum / ((double)chunks * chunk);

    return sqrt(sum2 / ((double)chunks * chunk));
}

/** Pink and brown are within 1 dB of the RMS level of white noise at the same level, at 44.1,
 * 48 and 96 kHz, and brown does not drift from 0. */
void SynthNoise_rms_matches_white(void) {
    // audio/synth
    static const float rates[] = { 44100, 48000, 96000 };
    box_audio_synth_patch_t p;
    double white, pink, brown, mean;
    int32_t r;
    for (r = 0; r < 3; r ++) {
        p = noise_patch(BOX_AUDIO_SYNTH_WHITE, 11);
        white = noise_rms(&p, rates[r], &mean);
        test_assert(fabs(mean) < 0.05 * white);
        p = noise_patch(BOX_AUDIO_SYNTH_PINK, 11);
        pink = noise_rms(&p, rates[r], &mean);
        test_assert(fabs(mean) < 0.05 * white);
        p = noise_patch(BOX_AUDIO_SYNTH_BROWN, 11);
        brown = noise_rms(&p, rates[r], &mean);
        printf("%.0f Hz: white %.4f, pink %+.2f dB, brown %+.2f dB, brown mean %.4f\n",
            (double)rates[r], white, 20 * log10(pink / white), 20 * log10(brown / white), mean);
        test_assert(white > 0.05 && white < 0.07);
        test_assert(fabs(20 * log10(pink / white)) < 1);
        test_assert(fabs(20 * log10(brown / white)) < 1);
        test_assert(fabs(mean) < 0.1 * white);
    }
}

/** Two renders with the same seed are identical, another seed differs, for each color. */
void SynthNoise_seed_identical(void) {
    // audio/synth
    uint32_t color;
    for (color = BOX_AUDIO_SYNTH_PINK; color <= BOX_AUDIO_SYNTH_BROWN; color ++) {
        box_audio_synth_patch_t p = noise_patch(color, 1234);
        noise_render_a(&p, AUDIO_RATE);
        test_assert(audio_peak(noise_a_l, 0, NOISE_FRAMES) > 0.02f);
        noise_render(&p, AUDIO_RATE);
        test_assert(noise_same_a());

        p.seed = 1235;
        noise_render(&p, AUDIO_RATE);
        test_assert(memcmp(audio_l, noise_a_l, sizeof(noise_a_l)) != 0);
    }
}

/** The next play of the same colored patch takes the next count of plays, as white noise does,
 * and its filter starts over. */
void SynthNoise_seed_plays(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_PINK, 1);
    box_audio_core_t *core;
    noise_render_a(&p, AUDIO_RATE);

    core = noise_core(AUDIO_RATE);
    noise_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(memcmp(audio_l, noise_a_l, 4800 * sizeof(float)) == 0);
    audio_play(core, 2, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    box_audio_core_free(core);
    test_assert(memcmp(audio_l, noise_a_l, 4800 * sizeof(float)) != 0);
}

/** The seeding of a play as the core does it, from the seed of the patch and the count of its
 * plays.
 *
 * @param seed The seed
 * @param plays The count of plays, 1 for the first
 * @return The state of the noise, never 0
 */
static uint32_t noise_hash(
    uint32_t seed,
    uint32_t plays)
{
    uint32_t x = seed ^ (plays * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;

    return x ? x : 0x6D2B79F5u;
}

/** Next value of white noise, xorshift32, as the core draws it.
 *
 * @param state The state
 * @return A value in -1..1
 */
static float noise_next(
    uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/** White noise is the xorshift32 sequence seeded from the seed and the count of plays, as
 * before the colors: after the two draws of the variances of the play, a draw per frame, at the
 * level of the layer. An explicit white color is bit identical to the default. */
void SynthNoise_white_unchanged(void) {
    // audio/synth
    static float ref[NOISE_FRAMES];
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_WHITE, 99), q = {0};
    uint32_t rng = noise_hash(99, 1);
    int32_t i, skip = 2048;
    double scale, error = 0;
    noise_next(&rng);
    noise_next(&rng);
    for (i = 0; i < NOISE_FRAMES; i ++) {
        ref[i] = noise_next(&rng);
    }

    p.envelope.attack = 0;
    noise_render_a(&p, AUDIO_RATE);
    for (i = skip; fabsf(ref[i]) < 0.5f; i ++) { }
    scale = (double)noise_a_l[i] / (double)ref[i];
    printf("scale of the noise %.6f\n", scale);
    test_assert(fabs(scale - 0.1) < 0.01);
    for (i = skip; i < NOISE_FRAMES; i ++) {
        double e = fabs((double)noise_a_l[i] - scale * (double)ref[i]);
        error = e > error ? e : error;
    }

    printf("largest error %g\n", error);
    test_assert(error < 1e-6);

    q.layers = 1;
    q.seed = 99;
    q.envelope = p.envelope;
    q.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f };
    noise_render(&q, AUDIO_RATE);
    test_assert(noise_same_a());
}

/** Unison voices of a colored noise layer each color their own noise: wide, the channels
 * differ, each keeps the slope, and two renders are identical. */
void SynthNoise_unison(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_PINK, 5);
    p.layer[0].voices = 3;
    p.layer[0].detune = 20;
    p.layer[0].width = 1;
    noise_render_a(&p, AUDIO_RATE);
    test_assert(memcmp(noise_a_l, noise_a_r, sizeof(noise_a_l)) != 0);
    test_assert(audio_peak(noise_a_l, 0, NOISE_FRAMES) > 0.02f);
    test_assert(audio_peak(noise_a_r, 0, NOISE_FRAMES) > 0.02f);
    noise_render(&p, AUDIO_RATE);

    test_assert(noise_same_a());
    noise_slope(&p, AUDIO_RATE, false, noise_pink_octaves, 7, 3, 1);
    noise_slope(&p, AUDIO_RATE, true, noise_pink_octaves, 7, 3, 1);

    p.layer[0].color = BOX_AUDIO_SYNTH_BROWN;
    p.layer[0].width = 0;
    noise_slope(&p, AUDIO_RATE, false, noise_brown_octaves, 6, 6, 1.5);
}

/** A colored layer renders the same through the path of LFOs, with an LFO on the pitch that
 * noise ignores, and through the path of modulation, with a modulator of index 0. */
void SynthNoise_paths(void) {
    // audio/synth
    uint32_t color;
    for (color = BOX_AUDIO_SYNTH_PINK; color <= BOX_AUDIO_SYNTH_BROWN; color ++) {
        box_audio_synth_patch_t p = noise_patch(color, 3), q;
        noise_render_a(&p, AUDIO_RATE);

        q = p;
        q.lfos = 1;
        q.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
            .pitch = 100 };
        noise_render(&q, AUDIO_RATE);

        test_assert(noise_same_a());

        q = p;
        q.layers = 2;
        q.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 300,
            .level = 1, .modulates = 1, .index = 0 };
        noise_render(&q, AUDIO_RATE);

        test_assert(noise_same_a());
    }
}

/** Other waves ignore the color, and a color out of range counts as white. */
void SynthNoise_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = noise_patch(BOX_AUDIO_SYNTH_WHITE, 7);
    p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
    p.layer[0].frequency = 220;
    noise_render_a(&p, AUDIO_RATE);
    p.layer[0].color = BOX_AUDIO_SYNTH_PINK;
    noise_render(&p, AUDIO_RATE);

    test_assert(noise_same_a());

    p = noise_patch(BOX_AUDIO_SYNTH_WHITE, 7);
    noise_render_a(&p, AUDIO_RATE);
    p.layer[0].color = 77;
    noise_render(&p, AUDIO_RATE);

    test_assert(noise_same_a());
}

/** test/assets/audio/synth_noise.flecs compiles the color of each layer, white by default, and
 * the sounds play. */
void SynthNoise_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_noise.flecs");
    ecs_entity_t wind, rumble, hiss;
    const box_audio_synth_patch_t *p;
    int32_t start;
    test_assert(ecs_script_run(world, "noise", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);

    wind = ecs_lookup(world, "Wind");
    rumble = ecs_lookup(world, "Rumble");
    hiss = ecs_lookup(world, "Hiss");
    test_assert(wind != 0 && rumble != 0 && hiss != 0);
    test_int(ecs_get(world, hiss, SynthOscillator)->color, SynthNoiseWhite);
    test_int(ecs_get(world, wind, SynthOscillator)->color, SynthNoisePink);
    test_int(ecs_get(world, rumble, SynthOscillator)->color, SynthNoiseBrown);
    p = &ecs_get(world, wind, AudioSynth)->patch;
    test_int(p->layers, 1);
    test_int(p->layer[0].color, BOX_AUDIO_SYNTH_PINK);
    p = &ecs_get(world, rumble, AudioSynth)->patch;
    test_int(p->layer[0].color, BOX_AUDIO_SYNTH_BROWN);
    test_int(p->layer[0].voices, 3);
    p = &ecs_get(world, hiss, AudioSynth)->patch;
    test_int(p->layer[0].color, BOX_AUDIO_SYNTH_WHITE);

    audio_run(world, 1);
    start = audio_tap_count;
    test_assert(box_audio_play(world, wind, 1.0f));
    test_assert(box_audio_play(world, rumble, 1.0f));
    audio_run(world, 4);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.01f);
    test_assert(audio_peak(audio_tap_r, start, audio_tap_count) > 0.01f);
    ecs_fini(world);
}
