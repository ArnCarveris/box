/**
 * @file audio/analyze.c
 * @brief Analysis of recordings: a real FFT, the K-weighting and gating of BS.1770 and EBU
 * R128, a 4x oversampled true peak, band energies, stereo measures, onsets, attack, tail,
 * silence, clicks, discontinuities and pitch, and a reader of WAV files.
 *
 * Plain C without Flecs or platform code. Buffers are allocated once per call, never in a
 * loop that runs per sample. The same input gives the same result.
 */

#include "analyze.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Hops of about 10 ms in a gating block of 400 ms */
#define BOX_AUDIO_BLOCK_HOPS (40)

/** Hops in the step of the gating blocks, 100 ms */
#define BOX_AUDIO_STEP_HOPS (10)

/** Hops in a short-term window of 3 s */
#define BOX_AUDIO_SHORT_HOPS (300)

/** Frames of the pitch analysis at most */
#define BOX_AUDIO_PITCH_FRAMES (256)

/** Peaks of the normalized square difference function considered per frame */
#define BOX_AUDIO_PITCH_PEAKS (64)

/** Half the window of the local slope of the discontinuities, in samples */
#define BOX_AUDIO_JUMP_WINDOW (32)

/** Power of the floor level, 10^(BOX_AUDIO_ANALYZE_FLOOR / 10) */
#define BOX_AUDIO_FLOOR_POWER (1e-15)

/** Edges of the bands in Hz */
static const float box_audio_band_edges[BOX_AUDIO_ANALYZE_BANDS + 1] = {
    20, 60, 250, 500, 2000, 6000, 12000, 20000
};

/** Names of the bands */
static const char *box_audio_band_names[BOX_AUDIO_ANALYZE_BANDS] = {
    "sub", "low", "low-mid", "mid", "presence", "brilliance", "air"
};

/** The scratch of a spectrum: an FFT, its window and the summed power per bin */
typedef struct box_audio_spectrum_t {
    box_audio_fft_t fft;    /**< The FFT */
    float *window;          /**< Hann window of the size of the FFT */
    float *frame;           /**< Windowed samples */
    float *re;              /**< Real parts of the bins */
    float *im;              /**< Imaginary parts of the bins */
    double *power;          /**< Power per bin summed over the frames and channels */
} box_audio_spectrum_t;

/** The measures of a spectrum */
typedef struct box_audio_spectrum_result_t {
    double bands[BOX_AUDIO_ANALYZE_BANDS]; /**< Power per band */
    double total;           /**< Power of every bin */
    float centroid;         /**< Spectral centroid in Hz */
    float rolloff;          /**< 85% rolloff in Hz */
} box_audio_spectrum_result_t;

/** The state of an analysis */
typedef struct box_audio_analyze_ctx_t {
    const float *data;      /**< Interleaved frames */
    int32_t frames;         /**< Number of frames */
    int32_t channels;       /**< 1 or 2 */
    float rate;             /**< Frames per second */
    int32_t hop;            /**< Frames of a hop of about 10 ms */
    int32_t hops;           /**< Hops, the last one may be partial */
    double *hop_kw;         /**< Sum of the K-weighted squares of every channel per hop */
    float *hop_level;       /**< Mean square of a hop in dB */
    double *blocks;         /**< Power of the gating blocks */
    float *values;          /**< Loudness of the short-term blocks, sorted */
    int32_t first_onset;    /**< Frame of the first onset, -1 for none */
    int32_t second_onset;   /**< Frame of the second onset, -1 for none */
    int32_t last_hop;       /**< Hop of the last onset, -1 for none */
    box_audio_analysis_t *out; /**< The result */
} box_audio_analyze_ctx_t;

/** The scratch of the pitch analysis */
typedef struct box_audio_pitch_t {
    box_audio_fft_t fft;    /**< FFT of twice the frame */
    int32_t size;           /**< Samples of a frame */
    int32_t min_lag;        /**< Shortest period in samples */
    int32_t max_lag;        /**< Longest period in samples */
    float *samples;         /**< The samples of a frame */
    float *frame;           /**< Input of the FFT, twice the size of a frame */
    float *re;              /**< Real parts of the bins */
    float *im;              /**< Imaginary parts of the bins */
    float *nsdf;            /**< Normalized square difference per lag */
} box_audio_pitch_t;

/** Convert a power to dB, at least the floor.
 *
 * @param power The power
 * @return The level in dB
 */
static float box_audio_db_power(
    double power)
{
    float result = BOX_AUDIO_ANALYZE_FLOOR;
    if (power > BOX_AUDIO_FLOOR_POWER) {
        result = (float)(10.0 * log10(power));
    }

    return result;
}

/** Convert an amplitude to dB, at least the floor.
 *
 * @param amplitude The amplitude
 * @return The level in dB
 */
static float box_audio_db(
    double amplitude)
{
    return box_audio_db_power(amplitude * amplitude);
}

float box_audio_analyze_band_low(
    int32_t band)
{
    return band >= 0 && band < BOX_AUDIO_ANALYZE_BANDS ? box_audio_band_edges[band] : 0;
}

float box_audio_analyze_band_high(
    int32_t band)
{
    return band >= 0 && band < BOX_AUDIO_ANALYZE_BANDS ? box_audio_band_edges[band + 1] : 0;
}

const char* box_audio_analyze_band_name(
    int32_t band)
{
    return band >= 0 && band < BOX_AUDIO_ANALYZE_BANDS ? box_audio_band_names[band] : "?";
}

int box_audio_fft_init(
    box_audio_fft_t *fft,
    int32_t size)
{
    int32_t half = size / 2, i, bits = 0, tables;
    int result = -1;
    memset(fft, 0, sizeof(*fft));
    if (size < 4 || (size & (size - 1))) {
        goto done;
    }

    tables = half / 2 + half + 1;
    fft->size = size;
    fft->cos = malloc(sizeof(float) * (size_t)tables);
    fft->sin = malloc(sizeof(float) * (size_t)tables);
    fft->rev = malloc(sizeof(int32_t) * (size_t)half);
    fft->re = malloc(sizeof(float) * (size_t)half);
    fft->im = malloc(sizeof(float) * (size_t)half);
    if (!fft->cos || !fft->sin || !fft->rev || !fft->re || !fft->im) {
        box_audio_fft_fini(fft);
        goto done;
    }

    while ((1 << bits) < half) {
        bits ++;
    }

    for (i = 0; i < half; i ++) {
        int32_t r = 0, b;
        for (b = 0; b < bits; b ++) {
            r |= ((i >> b) & 1) << (bits - 1 - b);
        }

        fft->rev[i] = r;
    }

    for (i = 0; i < half / 2; i ++) {
        fft->cos[i] = (float)cos(2.0 * M_PI * i / half);
        fft->sin[i] = (float)-sin(2.0 * M_PI * i / half);
    }

    for (i = 0; i <= half; i ++) {
        fft->cos[half / 2 + i] = (float)cos(2.0 * M_PI * i / size);
        fft->sin[half / 2 + i] = (float)-sin(2.0 * M_PI * i / size);
    }

    result = 0;
done:
    return result;
}

void box_audio_fft_fini(
    box_audio_fft_t *fft)
{
    free(fft->cos);
    free(fft->sin);
    free(fft->rev);
    free(fft->re);
    free(fft->im);
    memset(fft, 0, sizeof(*fft));
}

/** Run the half size complex FFT in place on fft->re and fft->im, in bit reversed order.
 *
 * @param fft The FFT
 */
static void box_audio_fft_complex(
    box_audio_fft_t *fft)
{
    int32_t n = fft->size / 2, len, i, j;
    float *re = fft->re, *im = fft->im;
    for (i = 0; i < n; i ++) {
        j = fft->rev[i];
        if (j > i) {
            float tr = re[i], ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }

    for (len = 2; len <= n; len <<= 1) {
        int32_t half = len / 2, step = n / len;
        for (i = 0; i < n; i += len) {
            for (j = 0; j < half; j ++) {
                float wr = fft->cos[j * step], wi = fft->sin[j * step];
                int32_t a = i + j, b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void box_audio_fft_real(
    box_audio_fft_t *fft,
    const float *in,
    float *re,
    float *im)
{
    int32_t n = fft->size / 2, k;
    const float *wc = fft->cos + n / 2, *ws = fft->sin + n / 2;
    for (k = 0; k < n; k ++) {
        fft->re[k] = in[2 * k];
        fft->im[k] = in[2 * k + 1];
    }

    box_audio_fft_complex(fft);
    for (k = 0; k <= n; k ++) {
        int32_t k1 = k % n, k2 = (n - k) % n;
        float zr = fft->re[k1], zi = fft->im[k1];
        float cr = fft->re[k2], ci = -fft->im[k2];
        float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
        float odr = 0.5f * (zi - ci), odi = -0.5f * (zr - cr);
        re[k] = er + wc[k] * odr - ws[k] * odi;
        im[k] = ei + wc[k] * odi + ws[k] * odr;
    }
}

void box_audio_kweight_init(
    box_audio_kweight_t *k,
    double sample_rate)
{
    double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
    double kk = tan(M_PI * f0 / sample_rate);
    double vh = pow(10.0, g / 20.0);
    double vb = pow(vh, 0.4996667741545416);
    double a0 = 1.0 + kk / q + kk * kk;
    memset(k, 0, sizeof(*k));
    k->shelf.b0 = (vh + vb * kk / q + kk * kk) / a0;
    k->shelf.b1 = 2.0 * (kk * kk - vh) / a0;
    k->shelf.b2 = (vh - vb * kk / q + kk * kk) / a0;
    k->shelf.a1 = 2.0 * (kk * kk - 1.0) / a0;
    k->shelf.a2 = (1.0 - kk / q + kk * kk) / a0;
    f0 = 38.13547087602444;
    q = 0.5003270373238773;
    kk = tan(M_PI * f0 / sample_rate);
    a0 = 1.0 + kk / q + kk * kk;
    k->high_pass.b0 = 1.0;
    k->high_pass.b1 = -2.0;
    k->high_pass.b2 = 1.0;
    k->high_pass.a1 = 2.0 * (kk * kk - 1.0) / a0;
    k->high_pass.a2 = (1.0 - kk / q + kk * kk) / a0;
}

/** Filter a sample with a biquad.
 *
 * @param f The biquad
 * @param x The sample
 * @return The filtered sample
 */
static double box_audio_biquad(
    box_audio_biquad_t *f,
    double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;

    return y;
}

double box_audio_kweight(
    box_audio_kweight_t *k,
    double x)
{
    return box_audio_biquad(&k->high_pass, box_audio_biquad(&k->shelf, x));
}

float box_audio_loudness(
    double power)
{
    float result = BOX_AUDIO_ANALYZE_FLOOR;
    if (power > BOX_AUDIO_FLOOR_POWER) {
        result = (float)(-0.691 + 10.0 * log10(power));
    }

    return result < BOX_AUDIO_ANALYZE_FLOOR ? BOX_AUDIO_ANALYZE_FLOOR : result;
}

void box_audio_true_peak_init(
    box_audio_true_peak_t *tp)
{
    int32_t p, t, half = BOX_AUDIO_TRUE_PEAK_TAPS / 2;
    for (p = 0; p < 3; p ++) {
        double d = (p + 1) / 4.0, sum = 0;
        for (t = 0; t < BOX_AUDIO_TRUE_PEAK_TAPS; t ++) {
            double u = d - (t - (half - 1));
            double sinc = sin(M_PI * u) / (M_PI * u);
            double w = 0.42 + 0.5 * cos(M_PI * u / half) + 0.08 * cos(2.0 * M_PI * u / half);
            tp->taps[p][t] = (float)(sinc * w);
            sum += sinc * w;
        }

        for (t = 0; t < BOX_AUDIO_TRUE_PEAK_TAPS; t ++) {
            tp->taps[p][t] = (float)(tp->taps[p][t] / sum);
        }
    }
}

float box_audio_true_peak_between(
    const box_audio_true_peak_t *tp,
    const float *data,
    int32_t frames,
    int32_t channels,
    int32_t channel,
    int32_t i)
{
    int32_t p, t, half = BOX_AUDIO_TRUE_PEAK_TAPS / 2, first = i - (half - 1);
    bool inside = first >= 0 && first + BOX_AUDIO_TRUE_PEAK_TAPS <= frames;
    float result = 0;
    for (p = 0; p < 3; p ++) {
        float acc = 0;
        for (t = 0; t < BOX_AUDIO_TRUE_PEAK_TAPS; t ++) {
            int32_t j = first + t;
            if (inside || (j >= 0 && j < frames)) {
                acc += tp->taps[p][t] * data[(int64_t)j * channels + channel];
            }
        }

        acc = fabsf(acc);
        result = acc > result ? acc : result;
    }

    return result;
}

/** Set up the scratch of a spectrum.
 *
 * @param s The scratch
 * @param size Size of the FFT
 * @return 0 on success, -1 when out of memory
 */
static int box_audio_spectrum_init(
    box_audio_spectrum_t *s,
    int32_t size)
{
    int32_t i, bins = size / 2 + 1;
    int result = -1;
    memset(s, 0, sizeof(*s));
    if (box_audio_fft_init(&s->fft, size)) {
        goto done;
    }

    s->window = malloc(sizeof(float) * (size_t)size);
    s->frame = malloc(sizeof(float) * (size_t)size);
    s->re = malloc(sizeof(float) * (size_t)bins);
    s->im = malloc(sizeof(float) * (size_t)bins);
    s->power = malloc(sizeof(double) * (size_t)bins);
    if (!s->window || !s->frame || !s->re || !s->im || !s->power) {
        goto done;
    }

    for (i = 0; i < size; i ++) {
        s->window[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / size));
    }

    result = 0;
done:
    return result;
}

/** Free the scratch of a spectrum.
 *
 * @param s The scratch
 */
static void box_audio_spectrum_fini(
    box_audio_spectrum_t *s)
{
    box_audio_fft_fini(&s->fft);
    free(s->window);
    free(s->frame);
    free(s->re);
    free(s->im);
    free(s->power);
    memset(s, 0, sizeof(*s));
}

/** Sum the power per bin of frames of a part of a recording, Hann windowed with half
 * overlap, each channel on its own; a part shorter than the FFT is one frame.
 *
 * @param s The scratch, receives the power per bin
 * @param data Interleaved frames
 * @param channels Number of channels
 * @param start First frame of the part
 * @param end Frame after the part
 */
static void box_audio_spectrum_power(
    box_audio_spectrum_t *s,
    const float *data,
    int32_t channels,
    int32_t start,
    int32_t end)
{
    int32_t size = s->fft.size, bins = size / 2 + 1, len = end - start;
    int32_t count = len >= size ? (len - size) / (size / 2) + 1 : 1;
    int32_t width = len >= size ? size : len, f, c, n, k;
    memset(s->power, 0, sizeof(double) * (size_t)bins);
    for (f = 0; f < count; f ++) {
        int64_t off = (int64_t)start + (int64_t)f * (size / 2);
        for (c = 0; c < channels; c ++) {
            for (n = 0; n < size; n ++) {
                float w = 0;
                if (n < width) {
                    w = width == size ? s->window[n] :
                        (float)(0.5 - 0.5 * cos(2.0 * M_PI * n / width));
                    w *= data[(off + n) * channels + c];
                }

                s->frame[n] = w;
            }

            box_audio_fft_real(&s->fft, s->frame, s->re, s->im);
            for (k = 0; k < bins; k ++) {
                s->power[k] += (double)s->re[k] * s->re[k] + (double)s->im[k] * s->im[k];
            }
        }
    }
}

/** Measure the spectrum of a part of a recording: power per band with each bin split over
 * the bands by its overlap, the total, the centroid and the rolloff.
 *
 * @param s The scratch
 * @param data Interleaved frames
 * @param channels Number of channels
 * @param rate Frames per second
 * @param start First frame of the part
 * @param end Frame after the part
 * @param out Receives the measures
 */
static void box_audio_spectrum_run(
    box_audio_spectrum_t *s,
    const float *data,
    int32_t channels,
    float rate,
    int32_t start,
    int32_t end,
    box_audio_spectrum_result_t *out)
{
    int32_t size = s->fft.size, bins = size / 2 + 1, k, b;
    double df = (double)rate / size, moment = 0, cumulative = 0;
    memset(out, 0, sizeof(*out));
    box_audio_spectrum_power(s, data, channels, start, end);
    for (k = 0; k < bins; k ++) {
        double lo = (k - 0.5) * df, hi = (k + 0.5) * df, p = s->power[k];
        out->total += p;
        moment += p * k * df;
        for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
            double l = lo > box_audio_band_edges[b] ? lo : box_audio_band_edges[b];
            double h = hi < box_audio_band_edges[b + 1] ? hi : box_audio_band_edges[b + 1];
            if (h > l) {
                out->bands[b] += p * (h - l) / df;
            }
        }
    }

    out->centroid = out->total > 0 ? (float)(moment / out->total) : 0;
    for (k = 0; k < bins && out->total > 0; k ++) {
        cumulative += s->power[k];
        if (cumulative >= 0.85 * out->total) {
            out->rolloff = (float)(k * df);
            break;
        }
    }
}

/** First pass over the samples: peaks, DC, RMS, clipping, stereo measures, silence and the
 * K-weighted power and level of every hop.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_levels(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    box_audio_kweight_t k[2];
    const float *x = ctx->data;
    int32_t ch = ctx->channels, i, c, first = -1, last = -1;
    double sum[2] = {0, 0}, sq = 0, ll = 0, rr = 0, lr = 0, mid = 0, side = 0;
    double kst = 0, kmono = 0, hop_sq = 0;
    float peak = 0, full = 0.99995f;
    box_audio_kweight_init(&k[0], ctx->rate);
    box_audio_kweight_init(&k[1], ctx->rate);
    for (i = 0; i < ctx->frames; i ++) {
        int32_t h = i / ctx->hop;
        double kx[2] = {0, 0};
        for (c = 0; c < ch; c ++) {
            float v = x[(int64_t)i * ch + c], a = fabsf(v);
            sum[c] += v;
            sq += (double)v * v;
            hop_sq += (double)v * v;
            peak = a > peak ? a : peak;
            if (a > 0.001f) {
                first = first < 0 ? i : first;
                last = i;
            }

            if (a > 1.0f || (a >= full && ((i > 0 && x[(int64_t)(i - 1) * ch + c] * v > 0 &&
                fabsf(x[(int64_t)(i - 1) * ch + c]) >= full) || (i + 1 < ctx->frames &&
                x[(int64_t)(i + 1) * ch + c] * v > 0 &&
                fabsf(x[(int64_t)(i + 1) * ch + c]) >= full))))
            {
                out->clipped ++;
            }

            kx[c] = box_audio_kweight(&k[c], v);
            ctx->hop_kw[h] += kx[c] * kx[c];
        }

        if (ch == 2) {
            double l = x[(int64_t)i * 2], r = x[(int64_t)i * 2 + 1];
            ll += l * l;
            rr += r * r;
            lr += l * r;
            mid += 0.25 * (l + r) * (l + r);
            side += 0.25 * (l - r) * (l - r);
            kst += kx[0] * kx[0] + kx[1] * kx[1];
            kmono += 0.5 * (kx[0] + kx[1]) * (kx[0] + kx[1]);
        }

        if ((i + 1) % ctx->hop == 0 || i + 1 == ctx->frames) {
            int32_t n = i + 1 - h * ctx->hop;
            ctx->hop_level[h] = box_audio_db_power(hop_sq / ((double)n * ch));
            hop_sq = 0;
        }
    }

    for (c = 0; c < ch; c ++) {
        out->dc[c] = (float)(sum[c] / ctx->frames);
    }

    out->sample_peak = box_audio_db(peak);
    out->rms = box_audio_db_power(sq / ((double)ctx->frames * ch));
    out->crest = out->rms > BOX_AUDIO_ANALYZE_FLOOR ? out->sample_peak - out->rms : 0;
    out->correlation = 1;
    out->side_mid = BOX_AUDIO_ANALYZE_FLOOR;
    out->mono_loss = 0;
    if (ch == 2) {
        out->correlation = ll > 0 && rr > 0 ? (float)(lr / sqrt(ll * rr)) : 0;
        out->side_mid = mid > BOX_AUDIO_FLOOR_POWER ?
            box_audio_db_power(side / mid) : (side > BOX_AUDIO_FLOOR_POWER ? 100.0f :
            BOX_AUDIO_ANALYZE_FLOOR);
        out->side_mid = out->side_mid > 100.0f ? 100.0f : out->side_mid;
        if (kst > BOX_AUDIO_FLOOR_POWER) {
            double floor = kst * 1e-10;
            out->mono_loss = (float)(10.0 * log10(kst / (kmono > floor ? kmono : floor)));
        }
    }

    out->leading = (float)((first < 0 ? ctx->frames : first) / (double)ctx->rate);
    out->trailing = (float)((last < 0 ? ctx->frames : ctx->frames - 1 - last) /
        (double)ctx->rate);
    for (c = 0; c < ch; c ++) {
        float a = fabsf(x[c]), b = fabsf(x[(int64_t)(ctx->frames - 1) * ch + c]);
        out->first = a > out->first ? a : out->first;
        out->last = b > out->last ? b : out->last;
    }
}

/** Get the power of the gating blocks of a length, a step of 100 ms apart; a recording
 * shorter than one block is one block of all of it.
 *
 * @param ctx The analysis
 * @param len Hops of a block
 * @return Number of blocks written to ctx->blocks
 */
static int32_t box_audio_analyze_blocks(
    box_audio_analyze_ctx_t *ctx,
    int32_t len)
{
    int32_t full = ctx->frames / ctx->hop, count = 0, b, h;
    if (full < len) {
        double sum = 0;
        for (h = 0; h < ctx->hops; h ++) {
            sum += ctx->hop_kw[h];
        }

        ctx->blocks[0] = sum / ctx->frames;
        count = 1;
    } else {
        count = (full - len) / BOX_AUDIO_STEP_HOPS + 1;
        for (b = 0; b < count; b ++) {
            double sum = 0;
            for (h = b * BOX_AUDIO_STEP_HOPS; h < b * BOX_AUDIO_STEP_HOPS + len; h ++) {
                sum += ctx->hop_kw[h];
            }

            ctx->blocks[b] = sum / ((double)len * ctx->hop);
        }
    }

    return count;
}

/** Compare two floats for qsort.
 *
 * @param a The first
 * @param b The second
 * @return Their order
 */
static int box_audio_float_cmp(
    const void *a,
    const void *b)
{
    float x = *(const float*)a, y = *(const float*)b;

    return (x > y) - (x < y);
}

/** Gate the blocks: the integrated loudness, momentary and short-term maxima and the
 * loudness range.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_gating(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    int32_t count = box_audio_analyze_blocks(ctx, BOX_AUDIO_BLOCK_HOPS), b, n = 0;
    double sum = 0, rel_sum = 0;
    float rel;
    out->momentary_max = BOX_AUDIO_ANALYZE_FLOOR;
    out->integrated = BOX_AUDIO_ANALYZE_FLOOR;
    for (b = 0; b < count; b ++) {
        float l = box_audio_loudness(ctx->blocks[b]);
        out->momentary_max = l > out->momentary_max ? l : out->momentary_max;
        if (l > -70.0f) {
            sum += ctx->blocks[b];
            n ++;
        }
    }

    if (n) {
        rel = box_audio_loudness(sum / n) - 10.0f;
        n = 0;
        for (b = 0; b < count; b ++) {
            float l = box_audio_loudness(ctx->blocks[b]);
            if (l > -70.0f && l > rel) {
                rel_sum += ctx->blocks[b];
                n ++;
            }
        }

        out->integrated = n ? box_audio_loudness(rel_sum / n) : BOX_AUDIO_ANALYZE_FLOOR;
    }

    count = box_audio_analyze_blocks(ctx, BOX_AUDIO_SHORT_HOPS);
    out->short_term_max = BOX_AUDIO_ANALYZE_FLOOR;
    sum = 0;
    n = 0;
    for (b = 0; b < count; b ++) {
        float l = box_audio_loudness(ctx->blocks[b]);
        out->short_term_max = l > out->short_term_max ? l : out->short_term_max;
        if (l > -70.0f) {
            sum += ctx->blocks[b];
            n ++;
        }
    }

    out->range = 0;
    if (n) {
        rel = box_audio_loudness(sum / n) - 20.0f;
        n = 0;
        for (b = 0; b < count; b ++) {
            float l = box_audio_loudness(ctx->blocks[b]);
            if (l > -70.0f && l > rel) {
                ctx->values[n ++] = l;
            }
        }

        if (n > 1) {
            qsort(ctx->values, (size_t)n, sizeof(float), box_audio_float_cmp);
            out->range = ctx->values[(int32_t)((n - 1) * 0.95 + 0.5)] -
                ctx->values[(int32_t)((n - 1) * 0.1 + 0.5)];
        }
    }
}

/** Find the true peak: the sample peak and the 4x oversampled points between the samples
 * that can reach it.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_true_peak(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_true_peak_t tp;
    const float *x = ctx->data;
    int32_t ch = ctx->channels, i, c;
    float peak = powf(10.0f, ctx->out->sample_peak / 20.0f);
    box_audio_true_peak_init(&tp);
    for (i = 0; i + 1 < ctx->frames; i ++) {
        for (c = 0; c < ch; c ++) {
            float a = fabsf(x[(int64_t)i * ch + c]), b = fabsf(x[(int64_t)(i + 1) * ch + c]);
            float m = a > b ? a : b;
            if (m * 4.0f >= peak) {
                float p = box_audio_true_peak_between(&tp, x, ctx->frames, ch, c, i);
                peak = p > peak ? p : peak;
            }
        }
    }

    ctx->out->true_peak = box_audio_db(peak);
}

/** Find the frame where an onset starts: the first frame of the hop before it or of its hop
 * that reaches a tenth of the peak of its hop.
 *
 * @param ctx The analysis
 * @param h The hop of the onset
 * @return The frame
 */
static int32_t box_audio_onset_frame(
    box_audio_analyze_ctx_t *ctx,
    int32_t h)
{
    int32_t ch = ctx->channels, start = (h > 0 ? h - 1 : 0) * ctx->hop, i, c;
    int32_t end = (h + 1) * ctx->hop < ctx->frames ? (h + 1) * ctx->hop : ctx->frames;
    int32_t result = h * ctx->hop;
    float peak = 0;
    for (i = h * ctx->hop; i < end; i ++) {
        for (c = 0; c < ch; c ++) {
            float a = fabsf(ctx->data[(int64_t)i * ch + c]);
            peak = a > peak ? a : peak;
        }
    }

    for (i = start; i < end; i ++) {
        float a = 0;
        for (c = 0; c < ch; c ++) {
            float v = fabsf(ctx->data[(int64_t)i * ch + c]);
            a = v > a ? v : a;
        }

        if (a >= 0.1f * peak) {
            result = i;
            break;
        }
    }

    return result;
}

/** Find the onsets: hops whose level rises 10 dB over the mean of the 3 before, loud enough
 * and 50 ms after the previous onset.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_onsets(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    float top = BOX_AUDIO_ANALYZE_FLOOR, floor_level;
    int32_t h, j, last = -1000;
    for (h = 0; h < ctx->hops; h ++) {
        top = ctx->hop_level[h] > top ? ctx->hop_level[h] : top;
    }

    floor_level = top - 50.0f > -60.0f ? top - 50.0f : -60.0f;
    ctx->first_onset = ctx->second_onset = ctx->last_hop = -1;
    for (h = 0; h < ctx->hops; h ++) {
        double prev = 0;
        float rise;
        for (j = h - 3; j < h; j ++) {
            prev += j >= 0 ? pow(10.0, ctx->hop_level[j] / 10.0) : 0;
        }

        prev /= 3;
        rise = ctx->hop_level[h] - box_audio_db_power(prev > 1e-10 ? prev : 1e-10);
        if (rise >= 10.0f && ctx->hop_level[h] >= floor_level && h - last >= 5) {
            int32_t frame = box_audio_onset_frame(ctx, h);
            last = h;
            if (out->onset_count < BOX_AUDIO_ANALYZE_ONSETS) {
                out->onsets[out->onset_count].time = (float)(frame / (double)ctx->rate);
                out->onsets[out->onset_count].strength = rise;
            }

            if (ctx->first_onset < 0) {
                ctx->first_onset = frame;
            } else if (ctx->second_onset < 0) {
                ctx->second_onset = frame;
            }

            out->onset_count ++;
            ctx->last_hop = h;
            out->tail_onset = (float)(frame / (double)ctx->rate);
        }
    }
}

/** Measure the attack of the first onset, 10% to 90% of its peak within a second or until
 * the next onset.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_attack(
    box_audio_analyze_ctx_t *ctx)
{
    int32_t ch = ctx->channels, start = ctx->first_onset, end, i, c, t10 = -1, t90 = -1;
    float peak = 0;
    ctx->out->attack = -1;
    if (start < 0) {
        goto done;
    }

    end = start + (int32_t)ctx->rate;
    end = end < ctx->frames ? end : ctx->frames;
    end = ctx->second_onset > start && ctx->second_onset < end ? ctx->second_onset : end;
    for (i = start; i < end; i ++) {
        for (c = 0; c < ch; c ++) {
            float a = fabsf(ctx->data[(int64_t)i * ch + c]);
            peak = a > peak ? a : peak;
        }
    }

    for (i = start; i < end && peak > 0 && t90 < 0; i ++) {
        float a = 0;
        for (c = 0; c < ch; c ++) {
            float v = fabsf(ctx->data[(int64_t)i * ch + c]);
            a = v > a ? v : a;
        }

        if (t10 < 0 && a >= 0.1f * peak) {
            t10 = i;
        }

        if (a >= 0.9f * peak) {
            t90 = i;
        }
    }

    if (t10 >= 0 && t90 >= 0) {
        ctx->out->attack = (float)((t90 - t10) / (double)ctx->rate);
    }

done:
    return;
}

/** Measure the tail after the last onset: from the loudest hop of the 500 ms after it to
 * where the level falls 60 dB below that, interpolated between hops.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_tail(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    const float *level = ctx->hop_level;
    int32_t h, peak, end;
    double seconds = ctx->hop / (double)ctx->rate;
    float threshold;
    out->tail = -1;
    if (ctx->last_hop < 0) {
        goto done;
    }

    peak = ctx->last_hop;
    end = ctx->last_hop + 50 < ctx->hops ? ctx->last_hop + 50 : ctx->hops;
    for (h = ctx->last_hop; h < end; h ++) {
        peak = level[h] > level[peak] ? h : peak;
    }

    threshold = level[peak] - 60.0f;
    end = ctx->hops - 1;
    while (end > peak && level[end] < threshold) {
        end --;
    }

    if (end == ctx->hops - 1) {
        out->tail_cut = true;
        out->tail_drop = level[peak] - level[end];
        out->tail = (float)((ctx->frames - (peak * ctx->hop + ctx->hop / 2)) /
            (double)ctx->rate);
    } else {
        float frac = level[end] - level[end + 1] > 0 ?
            (level[end] - threshold) / (level[end] - level[end + 1]) : 0.5f;
        out->tail = (float)((end - peak + frac) * seconds);
    }

done:
    return;
}

/** Get the prediction error of a sample from the two before it.
 *
 * @param x Interleaved frames
 * @param ch Number of channels
 * @param c The channel
 * @param n The frame, at least 2
 * @return The absolute error
 */
static double box_audio_jump_error(
    const float *x,
    int32_t ch,
    int32_t c,
    int32_t n)
{
    return fabs((double)x[(int64_t)n * ch + c] - 2.0 * x[(int64_t)(n - 1) * ch + c] +
        x[(int64_t)(n - 2) * ch + c]);
}

/** Find the discontinuities: samples whose prediction error from the two before them is at
 * least 0.05 and 8 times the mean error of the 32 samples on each side, 10 ms apart.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_jumps(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    const float *x = ctx->data;
    int32_t ch = ctx->channels, n, c, m, w = BOX_AUDIO_JUMP_WINDOW, last = -ctx->frames;
    int32_t gap = (int32_t)(ctx->rate * 0.01f);
    double win[2] = {0, 0};
    if (ctx->frames < 3) {
        goto done;
    }

    for (c = 0; c < ch; c ++) {
        for (m = 2; m < ctx->frames && m <= 2 + w; m ++) {
            win[c] += box_audio_jump_error(x, ch, c, m);
        }
    }

    for (n = 2; n < ctx->frames; n ++) {
        int32_t lo = n - w > 2 ? n - w : 2;
        int32_t hi = n + w < ctx->frames - 1 ? n + w : ctx->frames - 1;
        float step = 0;
        for (c = 0; c < ch; c ++) {
            double e = box_audio_jump_error(x, ch, c, n);
            double local = hi > lo ? (win[c] - e) / (hi - lo) : 0;
            if (e >= 0.05 && e > 8.0 * local) {
                float s = fabsf(x[(int64_t)n * ch + c] - x[(int64_t)(n - 1) * ch + c]);
                step = s > step ? s : step;
            }

            if (n + 1 + w < ctx->frames) {
                win[c] += box_audio_jump_error(x, ch, c, n + 1 + w);
            }

            if (n - w >= 2) {
                win[c] -= box_audio_jump_error(x, ch, c, n - w);
            }
        }

        if (step > 0 && n - last >= gap) {
            if (out->jump_count < BOX_AUDIO_ANALYZE_JUMPS) {
                out->jumps[out->jump_count].time = (float)(n / (double)ctx->rate);
                out->jumps[out->jump_count].size = box_audio_db(step);
            }

            out->jump_count ++;
            last = n;
        }
    }

done:
    return;
}

/** Set up the scratch of the pitch analysis for a sample rate.
 *
 * @param p The scratch
 * @param rate Frames per second
 * @return 0 on success, -1 when out of memory
 */
static int box_audio_pitch_init(
    box_audio_pitch_t *p,
    float rate)
{
    int32_t size = 256, max_lag;
    int result = -1;
    memset(p, 0, sizeof(*p));
    while (size * 2 <= rate * 0.06f) {
        size *= 2;
    }

    max_lag = (int32_t)(rate / 50.0f);
    p->size = size;
    p->min_lag = (int32_t)(rate / 2000.0f) > 2 ? (int32_t)(rate / 2000.0f) : 2;
    p->max_lag = max_lag < size / 2 ? max_lag : size / 2;
    if (box_audio_fft_init(&p->fft, size * 2)) {
        goto done;
    }

    p->samples = malloc(sizeof(float) * (size_t)size);
    p->frame = malloc(sizeof(float) * (size_t)size * 2);
    p->re = malloc(sizeof(float) * (size_t)(size + 1));
    p->im = malloc(sizeof(float) * (size_t)(size + 1));
    p->nsdf = malloc(sizeof(float) * (size_t)(size + 1));
    if (p->samples && p->frame && p->re && p->im && p->nsdf) {
        result = 0;
    }

done:
    return result;
}

/** Free the scratch of the pitch analysis.
 *
 * @param p The scratch
 */
static void box_audio_pitch_fini(
    box_audio_pitch_t *p)
{
    box_audio_fft_fini(&p->fft);
    free(p->samples);
    free(p->frame);
    free(p->re);
    free(p->im);
    free(p->nsdf);
    memset(p, 0, sizeof(*p));
}

/** Compute the normalized square difference function of the samples in p->samples: the
 * autocorrelation through the FFT, over the energy of the overlapping samples.
 *
 * @param p The scratch, receives p->nsdf up to max_lag + 1
 */
static void box_audio_pitch_nsdf(
    box_audio_pitch_t *p)
{
    int32_t w = p->size, n = 2 * w, k, t;
    const float *x = p->samples;
    double m = 0;
    for (k = 0; k < w; k ++) {
        m += 2.0 * x[k] * x[k];
        p->frame[k] = x[k];
        p->frame[w + k] = 0;
    }

    box_audio_fft_real(&p->fft, p->frame, p->re, p->im);
    for (k = 0; k <= w; k ++) {
        p->frame[k] = p->re[k] * p->re[k] + p->im[k] * p->im[k];
    }

    for (k = 1; k < w; k ++) {
        p->frame[n - k] = p->frame[k];
    }

    box_audio_fft_real(&p->fft, p->frame, p->re, p->im);
    for (t = 0; t <= p->max_lag + 1 && t < w; t ++) {
        p->nsdf[t] = m > 1e-20 ? (float)(2.0 * p->re[t] / n / m) : 0;
        m -= (double)x[t] * x[t] + (double)x[w - 1 - t] * x[w - 1 - t];
    }
}

/** Pick the period of a frame from its normalized square difference function: the first
 * peak of a positive lobe after the first zero crossing that reaches 90% of the highest one,
 * interpolated with a parabola.
 *
 * @param p The scratch with the function of the frame
 * @param rate Frames per second
 * @param hz Receives the frequency
 * @param clarity Receives the height of the peak, 0..1
 * @return Whether the frame has a pitch: a clarity of 0.8 or more within 50 to 2000 Hz
 */
static bool box_audio_pitch_pick(
    const box_audio_pitch_t *p,
    float rate,
    float *hz,
    float *clarity)
{
    const float *n = p->nsdf;
    int32_t max = p->max_lag, i = 1, count = 0, k, best = -1;
    int32_t lags[BOX_AUDIO_PITCH_PEAKS];
    float top = 0, a, b, c, den, d, lag;
    bool result = false;
    while (i < max && n[i] > 0) {
        i ++;
    }

    while (i < max && count < BOX_AUDIO_PITCH_PEAKS) {
        int32_t peak;
        while (i < max && n[i] <= 0) {
            i ++;
        }

        peak = i;
        while (i < max && n[i] > 0) {
            peak = n[i] > n[peak] ? i : peak;
            i ++;
        }

        if (peak < max && n[peak] > 0 && peak >= p->min_lag) {
            lags[count ++] = peak;
            top = n[peak] > top ? n[peak] : top;
        }
    }

    for (k = 0; k < count && best < 0; k ++) {
        if (n[lags[k]] >= 0.9f * top) {
            best = lags[k];
        }
    }

    if (best < 1) {
        goto done;
    }

    a = n[best - 1];
    b = n[best];
    c = n[best + 1];
    den = a - 2.0f * b + c;
    d = den != 0 ? 0.5f * (a - c) / den : 0;
    d = d < -0.5f ? -0.5f : (d > 0.5f ? 0.5f : d);
    lag = (float)best + d;
    *hz = rate / lag;
    *clarity = b - 0.25f * (a - c) * d;
    result = *clarity >= 0.8f && *hz >= 50.0f && *hz <= 2000.0f;
done:
    return result;
}

/** Fill the samples of a pitch frame from the mid, or the side when it is louder.
 *
 * @param ctx The analysis
 * @param p The scratch
 * @param start First frame
 * @param sign 1 for the mid, -1 for the side
 * @return The mean square of the samples
 */
static double box_audio_pitch_fill(
    box_audio_analyze_ctx_t *ctx,
    box_audio_pitch_t *p,
    int32_t start,
    float sign)
{
    int32_t k, ch = ctx->channels;
    double sum = 0;
    for (k = 0; k < p->size; k ++) {
        int64_t i = ((int64_t)start + k) * ch;
        float v = ch == 1 ? ctx->data[i] : 0.5f * (ctx->data[i] + sign * ctx->data[i + 1]);
        p->samples[k] = v;
        sum += (double)v * v;
    }

    return sum / p->size;
}

/** Find the pitch: the median of the frames with a pitch among the loud ones, its spread in
 * cents and the harmonic to noise ratio of the mean clarity.
 *
 * @param ctx The analysis
 */
static void box_audio_analyze_pitch(
    box_audio_analyze_ctx_t *ctx)
{
    box_audio_analysis_t *out = ctx->out;
    box_audio_pitch_t p;
    float hz[BOX_AUDIO_PITCH_FRAMES], clarity[BOX_AUDIO_PITCH_FRAMES];
    float level[BOX_AUDIO_PITCH_FRAMES], sorted[BOX_AUDIO_PITCH_FRAMES];
    float sign = ctx->channels == 2 && out->side_mid > 0 ? -1.0f : 1.0f;
    int32_t count, stride, f, loud = 0, voiced = 0;
    double top = 0, threshold, csum = 0, spread = 0, c;
    if (box_audio_pitch_init(&p, ctx->rate) || ctx->frames < p.size) {
        goto done;
    }

    count = (ctx->frames - p.size) / (p.size / 2) + 1;
    stride = (count + BOX_AUDIO_PITCH_FRAMES - 1) / BOX_AUDIO_PITCH_FRAMES;
    count = (count + stride - 1) / stride;
    for (f = 0; f < count; f ++) {
        level[f] = (float)box_audio_pitch_fill(ctx, &p, f * stride * (p.size / 2), sign);
        top = level[f] > top ? level[f] : top;
    }

    threshold = top * 1e-3 > 1e-5 ? top * 1e-3 : 1e-5;
    for (f = 0; f < count; f ++) {
        if (level[f] < threshold) {
            continue;
        }

        loud ++;
        box_audio_pitch_fill(ctx, &p, f * stride * (p.size / 2), sign);
        box_audio_pitch_nsdf(&p);
        if (box_audio_pitch_pick(&p, ctx->rate, &hz[voiced], &clarity[voiced])) {
            sorted[voiced] = hz[voiced];
            voiced ++;
        }
    }

    out->voiced = loud ? (float)voiced / (float)loud : 0;
    if (!voiced) {
        goto done;
    }

    qsort(sorted, (size_t)voiced, sizeof(float), box_audio_float_cmp);
    out->pitch = sorted[voiced / 2];
    for (f = 0; f < voiced; f ++) {
        double cents = 1200.0 * log2(hz[f] / out->pitch);
        spread += cents * cents;
        csum += clarity[f];
    }

    out->pitch_spread = (float)sqrt(spread / voiced);
    c = csum / voiced;
    c = c < 1e-6 ? 1e-6 : (c > 0.999999 ? 0.999999 : c);
    out->hnr = (float)(10.0 * log10(c / (1.0 - c)));
    out->pitched = out->voiced >= 0.5f && out->pitch_spread <= 50.0f;
done:
    box_audio_pitch_fini(&p);
}

int box_audio_analyze(
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate,
    box_audio_analysis_t *out)
{
    box_audio_analyze_ctx_t ctx;
    box_audio_spectrum_t s;
    box_audio_spectrum_result_t r;
    int32_t b, blocks;
    int result = -1;
    memset(&ctx, 0, sizeof(ctx));
    memset(&s, 0, sizeof(s));
    memset(out, 0, sizeof(*out));
    if (!data || frames < 1 || channels < 1 || channels > 2 || !(sample_rate > 0)) {
        goto done;
    }

    ctx.data = data;
    ctx.frames = frames;
    ctx.channels = channels;
    ctx.rate = sample_rate;
    ctx.hop = (int32_t)(sample_rate / 100.0f + 0.5f);
    ctx.hop = ctx.hop > 0 ? ctx.hop : 1;
    ctx.hops = (frames + ctx.hop - 1) / ctx.hop;
    ctx.out = out;
    blocks = ctx.hops / BOX_AUDIO_STEP_HOPS + 2;
    ctx.hop_kw = calloc((size_t)ctx.hops, sizeof(double));
    ctx.hop_level = calloc((size_t)ctx.hops, sizeof(float));
    ctx.blocks = malloc(sizeof(double) * (size_t)blocks);
    ctx.values = malloc(sizeof(float) * (size_t)blocks);
    if (!ctx.hop_kw || !ctx.hop_level || !ctx.blocks || !ctx.values ||
        box_audio_spectrum_init(&s, BOX_AUDIO_ANALYZE_FFT))
    {
        goto done;
    }

    out->frames = frames;
    out->channels = channels;
    out->sample_rate = sample_rate;
    box_audio_analyze_levels(&ctx);
    box_audio_analyze_gating(&ctx);
    box_audio_analyze_true_peak(&ctx);
    box_audio_spectrum_run(&s, data, channels, sample_rate, 0, frames, &r);
    for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
        out->bands[b] = r.total > 0 ? box_audio_db_power(r.bands[b] / r.total) :
            BOX_AUDIO_ANALYZE_FLOOR;
    }

    out->centroid = r.centroid;
    out->rolloff = r.rolloff;
    box_audio_analyze_onsets(&ctx);
    box_audio_analyze_attack(&ctx);
    box_audio_analyze_tail(&ctx);
    box_audio_analyze_jumps(&ctx);
    box_audio_analyze_pitch(&ctx);
    result = 0;
done:
    free(ctx.hop_kw);
    free(ctx.hop_level);
    free(ctx.blocks);
    free(ctx.values);
    box_audio_spectrum_fini(&s);

    return result;
}

int box_audio_analyze_segments(
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate,
    box_audio_analyze_segment_t *segments,
    int32_t count)
{
    box_audio_spectrum_t s;
    box_audio_spectrum_result_t r;
    box_audio_kweight_t k[2];
    double *acc = NULL;
    int32_t i, c, seg = 0, b;
    int result = -1;
    memset(&s, 0, sizeof(s));
    if (!data || frames < 1 || channels < 1 || channels > 2 || !(sample_rate > 0) ||
        count < 0 || (count && !segments))
    {
        goto done;
    }

    acc = calloc((size_t)(count ? count : 1), sizeof(double));
    if (!acc || box_audio_spectrum_init(&s, BOX_AUDIO_ANALYZE_FFT)) {
        goto done;
    }

    box_audio_kweight_init(&k[0], sample_rate);
    box_audio_kweight_init(&k[1], sample_rate);
    for (i = 0; i < frames; i ++) {
        double e = 0;
        for (c = 0; c < channels; c ++) {
            double kx = box_audio_kweight(&k[c], data[(int64_t)i * channels + c]);
            e += kx * kx;
        }

        while (seg < count && i >= segments[seg].end) {
            seg ++;
        }

        if (seg < count && i >= segments[seg].start) {
            acc[seg] += e;
        }
    }

    for (seg = 0; seg < count; seg ++) {
        int32_t start = segments[seg].start > 0 ? segments[seg].start : 0;
        int32_t end = segments[seg].end < frames ? segments[seg].end : frames;
        segments[seg].loudness = BOX_AUDIO_ANALYZE_FLOOR;
        for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
            segments[seg].bands[b] = BOX_AUDIO_ANALYZE_FLOOR;
        }

        if (end <= start) {
            continue;
        }

        segments[seg].loudness = box_audio_loudness(acc[seg] / (end - start));
        box_audio_spectrum_run(&s, data, channels, sample_rate, start, end, &r);
        for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS && r.total > 0; b ++) {
            segments[seg].bands[b] = box_audio_db_power(r.bands[b] / r.total);
        }
    }

    result = 0;
done:
    free(acc);
    box_audio_spectrum_fini(&s);

    return result;
}

float box_audio_note_name(
    float hz,
    char *name)
{
    static const char *names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    double midi;
    int32_t r;
    float result = 0;
    if (!(hz > 1.0f) || hz > 100000.0f) {
        snprintf(name, 8, "?");
        goto done;
    }

    midi = 69.0 + 12.0 * log2(hz / 440.0);
    r = (int32_t)floor(midi + 0.5);
    snprintf(name, 8, "%s%d", names[((r % 12) + 12) % 12],
        (r >= 0 ? r / 12 : (r - 11) / 12) - 1);
    result = (float)((midi - r) * 100.0);
done:
    return result;
}

/** Read a little endian 16 bit value.
 *
 * @param p The bytes
 * @return The value
 */
static uint32_t box_audio_le16(
    const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/** Read a little endian 32 bit value.
 *
 * @param p The bytes
 * @return The value
 */
static uint32_t box_audio_le32(
    const uint8_t *p)
{
    return box_audio_le16(p) | (box_audio_le16(p + 2) << 16);
}

/** Read a little endian 64 bit value.
 *
 * @param p The bytes
 * @return The value
 */
static uint64_t box_audio_le64(
    const uint8_t *p)
{
    return (uint64_t)box_audio_le32(p) | ((uint64_t)box_audio_le32(p + 4) << 32);
}

/** Read a sample of a WAV file as a float, full scale at 1.
 *
 * @param p The bytes of the sample
 * @param tag 1 for PCM, 3 for float
 * @param bits Bits of the sample
 * @return The sample, 0 when it is not finite
 */
static float box_audio_wav_sample(
    const uint8_t *p,
    uint32_t tag,
    uint32_t bits)
{
    double v = 0;
    if (tag == 3 && bits == 32) {
        uint32_t u = box_audio_le32(p);
        float f;
        memcpy(&f, &u, sizeof(f));
        v = f;
    } else if (tag == 3) {
        uint64_t u = box_audio_le64(p);
        memcpy(&v, &u, sizeof(v));
    } else if (bits == 8) {
        v = ((int32_t)p[0] - 128) / 127.0;
    } else if (bits == 16) {
        v = (int16_t)box_audio_le16(p) / 32767.0;
    } else if (bits == 24) {
        int32_t s = (int32_t)(box_audio_le16(p) | ((uint32_t)p[2] << 16));
        s = s & 0x800000 ? s - 0x1000000 : s;
        v = s / 8388607.0;
    } else {
        v = (int32_t)box_audio_le32(p) / 2147483647.0;
    }

    if (tag == 1 && v < -1.0) {
        v = -1.0;
    }

    return isfinite(v) ? (float)v : 0.0f;
}

/** Read the song chunk of a capture.
 *
 * @param p The data of the chunk
 * @param out Receives the song
 */
static void box_audio_wav_song(
    const uint8_t *p,
    box_audio_wav_t *out)
{
    uint64_t beat = box_audio_le64(p);
    uint32_t tempo = box_audio_le32(p + 8), bar = box_audio_le32(p + 12);
    memcpy(&out->song.start_beat, &beat, sizeof(double));
    memcpy(&out->song.tempo, &tempo, sizeof(float));
    memcpy(&out->song.bar_beats, &bar, sizeof(float));
    out->has_song = isfinite(out->song.start_beat) && out->song.tempo > 0 &&
        out->song.bar_beats > 0;
}

const char* box_audio_wav_read(
    const uint8_t *bytes,
    size_t size,
    box_audio_wav_t *out)
{
    const char *result = "not a WAV file";
    size_t pos = 12, data_pos = 0, data_size = 0, frames, i;
    uint32_t tag = 0, channels = 0, rate = 0, align = 0, bits = 0;
    bool fmt = false, data = false;
    memset(out, 0, sizeof(*out));
    if (!bytes || size < 12 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4)) {
        goto done;
    }

    while (pos + 8 <= size) {
        const uint8_t *chunk = bytes + pos;
        size_t len = box_audio_le32(chunk + 4), avail = size - pos - 8;
        if (!memcmp(chunk, "fmt ", 4) && len >= 16 && len <= avail) {
            tag = box_audio_le16(chunk + 8);
            channels = box_audio_le16(chunk + 10);
            rate = box_audio_le32(chunk + 12);
            align = box_audio_le16(chunk + 20);
            bits = box_audio_le16(chunk + 22);
            tag = tag == 0xFFFE && len >= 40 ? box_audio_le16(chunk + 32) : tag;
            fmt = true;
        } else if (!memcmp(chunk, "data", 4)) {
            data_pos = pos + 8;
            data_size = len < avail ? len : avail;
            data = true;
        } else if (!memcmp(chunk, BOX_AUDIO_WAV_SONG_CHUNK, 4) &&
            len >= BOX_AUDIO_WAV_SONG_BYTES && len <= avail)
        {
            box_audio_wav_song(chunk + 8, out);
        }

        if (len > avail) {
            break;
        }

        pos += 8 + len + (len & 1);
    }

    if (!fmt || !data) {
        goto done;
    }

    if (tag != 1 && tag != 3) {
        result = "unsupported WAV format: compressed samples";
        goto done;
    }

    if (channels < 1 || channels > 2) {
        result = "unsupported WAV format: more than 2 channels";
        goto done;
    }

    if (!rate || align != channels * bits / 8 || (tag == 3 && bits != 32 && bits != 64) ||
        (tag == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32))
    {
        result = "unsupported WAV format: sample size";
        goto done;
    }

    frames = data_size / align;
    if (!frames) {
        result = "no samples";
        goto done;
    }

    if (frames > (size_t)INT32_MAX / channels) {
        result = "unsupported WAV format: too long";
        goto done;
    }

    out->data = malloc(sizeof(float) * frames * channels);
    if (!out->data) {
        result = "out of memory";
        goto done;
    }

    for (i = 0; i < frames * channels; i ++) {
        out->data[i] = box_audio_wav_sample(bytes + data_pos + i * (bits / 8), tag, bits);
    }

    out->frames = (int32_t)frames;
    out->channels = (int32_t)channels;
    out->sample_rate = (float)rate;
    result = NULL;
done:
    if (result) {
        out->has_song = false;
    }

    return result;
}

void box_audio_wav_fini(
    box_audio_wav_t *wav)
{
    free(wav->data);
    memset(wav, 0, sizeof(*wav));
}
