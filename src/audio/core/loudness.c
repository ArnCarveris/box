/**
 * @file audio/core/loudness.c
 * @brief The loudness meters of the buses: the K-weighting of BS.1770 at the output of a bus
 * with BOX_AUDIO_PARAM_LOUDNESS on, whose squares go into its meter. Without libm.
 */

#include "internal.h"

/** Filter a sample with a biquad of the K-weighting.
 *
 * @param c Its coefficients: b0, b1, b2, a1 and a2
 * @param z Its state: x[n-1], x[n-2], y[n-1] and y[n-2]
 * @param x The sample
 * @return The filtered sample
 */
static inline double box_audio_kweight_biquad(
    const double *c,
    double *z,
    double x)
{
    double y = c[0] * x + c[1] * z[0] + c[2] * z[1] - c[3] * z[2] - c[4] * z[3];
    z[1] = z[0];
    z[0] = x;
    z[3] = z[2];
    z[2] = y;

    return y;
}

void box_audio_core_kweight_init(
    box_audio_core_t *core)
{
    double turns = 1681.974450955533 / (double)core->sample_rate;
    double q = 0.7071752369554196, g = 3.999843853973347;
    double kk = (double)box_audio_core_sin(turns * 0.5) /
        (double)box_audio_core_sin(turns * 0.5 + 0.25);
    double vh = box_audio_synth_exp2(g / 20.0 * 3.321928094887362);
    double vb = box_audio_synth_exp2(box_audio_synth_log2(vh) * 0.4996667741545416);
    double a0 = 1.0 + kk / q + kk * kk;
    core->kweight[0][0] = (vh + vb * kk / q + kk * kk) / a0;
    core->kweight[0][1] = 2.0 * (kk * kk - vh) / a0;
    core->kweight[0][2] = (vh - vb * kk / q + kk * kk) / a0;
    core->kweight[0][3] = 2.0 * (kk * kk - 1.0) / a0;
    core->kweight[0][4] = (1.0 - kk / q + kk * kk) / a0;
    turns = 38.13547087602444 / (double)core->sample_rate;
    q = 0.5003270373238773;
    kk = (double)box_audio_core_sin(turns * 0.5) /
        (double)box_audio_core_sin(turns * 0.5 + 0.25);
    a0 = 1.0 + kk / q + kk * kk;
    core->kweight[1][0] = 1.0;
    core->kweight[1][1] = -2.0;
    core->kweight[1][2] = 1.0;
    core->kweight[1][3] = 2.0 * (kk * kk - 1.0) / a0;
    core->kweight[1][4] = (1.0 - kk / q + kk * kk) / a0;
}

float box_audio_core_kweight(
    box_audio_core_t *core,
    box_audio_bus_t *b,
    int32_t frames)
{
    double sum = 0;
    int32_t i, c;
    for (c = 0; c < 2; c ++) {
        const float *x = c ? b->r : b->l;
        double *shelf = b->kweight[c][0], *high_pass = b->kweight[c][1];
        for (i = 0; i < frames; i ++) {
            double y = box_audio_kweight_biquad(core->kweight[1], high_pass,
                box_audio_kweight_biquad(core->kweight[0], shelf, (double)x[i]));
            sum += y * y;
        }

        for (i = 0; i < 4; i ++) {
            shelf[i] = shelf[i] < 1e-30 && shelf[i] > -1e-30 ? 0 : shelf[i];
            high_pass[i] = high_pass[i] < 1e-30 && high_pass[i] > -1e-30 ? 0 : high_pass[i];
        }
    }

    return (float)sum;
}

void box_audio_core_loudness(
    box_audio_bus_t *b,
    bool on)
{
    int32_t c, f, j;
    if (on && !b->loudness) {
        for (c = 0; c < 2; c ++) {
            for (f = 0; f < 2; f ++) {
                for (j = 0; j < 4; j ++) {
                    b->kweight[c][f][j] = 0;
                }
            }
        }
    }

    b->loudness = on;
}
