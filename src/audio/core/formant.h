/**
 * @file audio/core/formant.h
 * @brief The formant filter of the synth: three bandpasses of svf.h in parallel at the first
 * three formants of a vowel, each at its own level, normalized for a saw at 110 Hz.
 *
 * The formants are the tenor table of the Csound manual, Appendix "Formant Values". A vowel
 * between two of them interpolates the frequencies, linear levels and bandwidths linearly.
 * Private to src/audio/core; included by internal.h.
 */

#ifndef BOX_AUDIO_CORE_FORMANT_H
#define BOX_AUDIO_CORE_FORMANT_H

#include "svf.h"

/** Formants of a vowel the filter runs */
#define BOX_AUDIO_FORMANTS 3

/** Vowels of the table: a, e, i, o, u */
#define BOX_AUDIO_FORMANT_VOWELS 5

/** Hz of the fundamental of the saw the level is normalized for */
#define BOX_AUDIO_FORMANT_REFERENCE 110.0

/** The normalization of the level in Hz. A saw at f0, as a spectrum falling as 1 / f^2, puts
 * about f0 B / (pi F^2) of its power of 1 / 3 into a bandpass of width B at F, so the gain is
 * sqrt(NORM / sum(level^2 B / F^2)) with NORM pi / (3 f0), raised by 1 dB for the dips. */
#define BOX_AUDIO_FORMANT_NORM \
    (3.14159265358979323846 / (3.0 * BOX_AUDIO_FORMANT_REFERENCE) * 1.12201845430196343559)

/** Share of the bandwidths of the table at resonance 1 */
#define BOX_AUDIO_FORMANT_NARROW 0.1

/** A formant of the table */
typedef struct box_audio_formant_entry_t {
    float frequency;  /**< Hz */
    float level;      /**< Linear level, from the dB of the table */
    float bandwidth;  /**< Hz */
} box_audio_formant_entry_t;

/** The tenor formants of the Csound manual, Appendix "Formant Values": per vowel the first
 * three formants, at 0, -6, -7 dB and so on as the table has them */
static const box_audio_formant_entry_t
    box_audio_formant_table[BOX_AUDIO_FORMANT_VOWELS][BOX_AUDIO_FORMANTS] =
{
    /* a: 650, 1080, 2650 Hz at 0, -6, -7 dB, 80, 90, 120 Hz wide */
    {{650.0f, 1.0f, 80.0f}, {1080.0f, 0.501187f, 90.0f}, {2650.0f, 0.446684f, 120.0f}},
    /* e: 400, 1700, 2600 Hz at 0, -14, -12 dB, 70, 80, 100 Hz wide */
    {{400.0f, 1.0f, 70.0f}, {1700.0f, 0.199526f, 80.0f}, {2600.0f, 0.251189f, 100.0f}},
    /* i: 290, 1870, 2800 Hz at 0, -15, -18 dB, 40, 90, 100 Hz wide */
    {{290.0f, 1.0f, 40.0f}, {1870.0f, 0.177828f, 90.0f}, {2800.0f, 0.125893f, 100.0f}},
    /* o: 400, 800, 2600 Hz at 0, -10, -12 dB, 40, 80, 100 Hz wide */
    {{400.0f, 1.0f, 40.0f}, {800.0f, 0.316228f, 80.0f}, {2600.0f, 0.251189f, 100.0f}},
    /* u: 350, 600, 2700 Hz at 0, -20, -17 dB, 40, 60, 100 Hz wide */
    {{350.0f, 1.0f, 40.0f}, {600.0f, 0.1f, 60.0f}, {2700.0f, 0.141254f, 100.0f}}
};

/** The state of a formant filter */
typedef struct box_audio_formant_t {
    box_audio_svf_t svf[BOX_AUDIO_FORMANTS];    /**< The bandpasses of the left channel */
    box_audio_svf_t svf_r[BOX_AUDIO_FORMANTS];  /**< The bandpasses of the right channel */
    float level[BOX_AUDIO_FORMANTS];            /**< Level of each, the normalization in it */
    float narrow;                               /**< Share of the bandwidths of the table, from
                                                 *   the resonance */
    float vowel;                                /**< Vowel of the coefficients, -1 for none */
} box_audio_formant_t;

/** Square root of a value of about 1 to 10^4 by Newton's method, without the math library,
 * the same on every platform.
 *
 * @param v The value, above 0
 * @return Its square root
 */
static inline double box_audio_formant_sqrt(
    double v)
{
    double x = v > 1 ? v : 1.0;
    int32_t i;
    for (i = 0; i < 24; i ++) {
        x = 0.5 * (x + v / x);
    }

    return x;
}

/** Clear a formant filter for a resonance; box_audio_formant_vowel sets its coefficients.
 *
 * @param f The filter
 * @param resonance 0..1
 */
static inline void box_audio_formant_init(
    box_audio_formant_t *f,
    float resonance)
{
    *f = (box_audio_formant_t){0};
    f->narrow = (float)(1.0 - (1.0 - BOX_AUDIO_FORMANT_NARROW) * (double)resonance);
    f->vowel = -1;
}

/** Set the coefficients of a formant filter for a vowel, of both channels, without touching
 * their state. The same vowel again does nothing.
 *
 * @param f The filter
 * @param vowel 0..BOX_AUDIO_SYNTH_VOWEL_MAX
 * @param rate Frames per second
 * @param top Highest frequency of a formant in Hz, under Nyquist
 */
static inline void box_audio_formant_vowel(
    box_audio_formant_t *f,
    float vowel,
    float rate,
    double top)
{
    const box_audio_formant_entry_t *a, *b;
    double power = 0, t, freq, level, width;
    int32_t i, n;
    if (vowel != f->vowel) {
        f->vowel = vowel;
        i = (int32_t)vowel;
        i = i < BOX_AUDIO_FORMANT_VOWELS - 2 ? (i > 0 ? i : 0) : BOX_AUDIO_FORMANT_VOWELS - 2;
        t = (double)vowel - (double)i;
        t = t < 1 ? (t > 0 ? t : 0.0) : 1.0;
        for (n = 0; n < BOX_AUDIO_FORMANTS; n ++) {
            box_audio_svf_t *s = &f->svf[n], *r = &f->svf_r[n];
            a = &box_audio_formant_table[i][n];
            b = &box_audio_formant_table[i + 1][n];
            freq = (double)a->frequency + ((double)b->frequency - (double)a->frequency) * t;
            level = (double)a->level + ((double)b->level - (double)a->level) * t;
            width = ((double)a->bandwidth + ((double)b->bandwidth - (double)a->bandwidth) * t) *
                (double)f->narrow;
            power += level * level * width / (freq * freq);
            freq = freq < top ? freq : top;
            s->k = (float)(width / freq);
            box_audio_svf_coefs(s, box_audio_svf_g(freq, rate));
            r->k = s->k;
            r->a1 = s->a1;
            r->a2 = s->a2;
            r->a3 = s->a3;
            f->level[n] = (float)level;
        }

        t = box_audio_formant_sqrt(BOX_AUDIO_FORMANT_NORM / power);
        for (n = 0; n < BOX_AUDIO_FORMANTS; n ++) {
            f->level[n] = (float)((double)f->level[n] * t);
        }
    }
}

/** Filter a frame through the bandpasses of a channel of a formant filter.
 *
 * @param f The filter
 * @param svf Its bandpasses of the channel, svf or svf_r
 * @param x The input
 * @param drive 0..1, as box_audio_svf_drive
 * @return The output
 */
static inline float box_audio_formant_next(
    const box_audio_formant_t *f,
    box_audio_svf_t *svf,
    float x,
    float drive)
{
    return f->level[0] * box_audio_svf_drive(&svf[0], BOX_AUDIO_SYNTH_BANDPASS, x, drive) +
        f->level[1] * box_audio_svf_drive(&svf[1], BOX_AUDIO_SYNTH_BANDPASS, x, drive) +
        f->level[2] * box_audio_svf_drive(&svf[2], BOX_AUDIO_SYNTH_BANDPASS, x, drive);
}

#endif
