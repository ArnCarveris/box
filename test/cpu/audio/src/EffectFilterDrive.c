/**
 * @file EffectFilterDrive.c
 * @brief The drive of the filter kind of effect, rendered directly on an effect outside of a
 * bus: bit-identical to the linear filter at drive 0, and bounded at resonance 1 with drive.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>
#include <stdlib.h>

/** A noise of the identical case */
static float fl_noise[AUDIO_RATE];

/** Buffers the effect renders in place */
static float fl_a_l[AUDIO_FRAMES], fl_a_r[AUDIO_FRAMES];

/** The parameters of a filter without drive.
 *
 * @param kind A box_audio_synth_filter_t
 * @param cutoff Hz
 * @param resonance 0..1
 * @param slope A box_audio_synth_slope_t
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_filter_params_t fl_params(
    box_audio_synth_filter_t kind,
    float cutoff,
    float resonance,
    box_audio_synth_slope_t slope,
    float mix)
{
    return (box_audio_filter_params_t){ (float)kind, cutoff, resonance, (float)slope, mix };
}

/** Start a filter effect of a core outside of a bus, to render it directly.
 *
 * @param core The core
 * @param fx The effect
 * @param p The parameters
 */
static void fl_direct(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    box_audio_filter_params_t p)
{
    memset(fx, 0, sizeof(*fx));
    memcpy(fx->params, &p, sizeof(p));
    box_audio_filter_start(core, fx, false);
}

/** Send new parameters to a filter effect started by fl_direct.
 *
 * @param core The core
 * @param fx The effect
 * @param p The parameters
 */
static void fl_direct_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    box_audio_filter_params_t p)
{
    memcpy(fx->params, &p, sizeof(p));
    box_audio_filter_command(core, fx);
}

/** At drive 0 the effect renders bit-identical to the linear stages of svf.h, both slopes,
 * both channels. */
void EffectFilterDrive_zero_identical(void) {
    // audio/filter
    box_audio_core_t *core = audio_new();
    box_audio_effect_t *fx = calloc(1, sizeof(box_audio_effect_t));
    int32_t slope, i, s, stages;
    srand(5);
    for (i = 0; i < AUDIO_RATE / 2; i ++) {
        fl_noise[i] = ((float)rand() / (float)RAND_MAX * 2 - 1) * 0.5f;
    }

    for (slope = 0; slope < 2; slope ++) {
        box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_BANDPASS, 900, 0.8f,
            (box_audio_synth_slope_t)slope, 1);
        box_audio_svf_t svf[2], svf_r[2];
        float g = box_audio_svf_g(box_audio_synth_exp2(box_audio_synth_log2(900.0)),
            AUDIO_RATE);
        p.drive = 0;
        stages = box_audio_svf_init(svf, (uint32_t)slope, 0.8f);
        for (s = 0; s < stages; s ++) {
            box_audio_svf_coefs(&svf[s], g);
        }

        svf_r[0] = svf[0];
        svf_r[1] = svf[1];
        for (i = 0; i < AUDIO_RATE / 2; i ++) {
            fl_a_l[i] = fl_noise[i];
            fl_a_r[i] = -fl_noise[AUDIO_RATE / 2 - 1 - i];
        }

        fl_direct(core, fx, p);
        box_audio_filter_render(core, fx, fl_a_l, fl_a_r, AUDIO_RATE / 2, false);
        for (i = 0; i < AUDIO_RATE / 2; i ++) {
            float a = fl_noise[i], b = -fl_noise[AUDIO_RATE / 2 - 1 - i], ya = a, yb = b;
            if (stages > 1) {
                ya = box_audio_svf_next(&svf[1], BOX_AUDIO_SYNTH_BANDPASS, ya);
                yb = box_audio_svf_next(&svf_r[1], BOX_AUDIO_SYNTH_BANDPASS, yb);
            }

            ya = box_audio_svf_next(&svf[0], BOX_AUDIO_SYNTH_BANDPASS, ya);
            yb = box_audio_svf_next(&svf_r[0], BOX_AUDIO_SYNTH_BANDPASS, yb);
            test_assert(fl_a_l[i] == a + (ya - a) * 1.0f);
            test_assert(fl_a_r[i] == b + (yb - b) * 1.0f);
        }
    }

    free(fx);
    box_audio_core_free(core);
}

/** At resonance 1 a loud sine at the cutoff peaks far lower with drive 1 than without, at 44.1,
 * 48 and 96 kHz and both slopes, and stays finite while the cutoff then sweeps every block. */
void EffectFilterDrive_bounded(void) {
    // audio/filter
    const float rates[] = { 44100, 48000, 96000 };
    box_audio_effect_t *fx = calloc(1, sizeof(box_audio_effect_t));
    int32_t r, slope, d, i, block;
    for (r = 0; r < 3; r ++) {
        box_audio_core_desc_t desc = { .sample_rate = rates[r] };
        box_audio_core_t *core = box_audio_core_new(&desc);
        test_assert(core != NULL);
        for (slope = 0; slope < 2; slope ++) {
            double peak[2];
            for (d = 0; d < 2; d ++) {
                box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_LOWPASS, 800, 1,
                    (box_audio_synth_slope_t)slope, 1);
                double w = 2 * AUDIO_PI * 800 / (double)rates[r];
                p.drive = (float)d;
                peak[d] = 0;
                fl_direct(core, fx, p);
                for (block = 0; block < 40; block ++) {
                    for (i = 0; i < AUDIO_FRAMES / 4; i ++) {
                        int32_t n = block * (AUDIO_FRAMES / 4) + i;
                        fl_a_l[i] = (float)(0.5 * sin(w * (double)n));
                        fl_a_r[i] = fl_a_l[i];
                    }

                    box_audio_filter_render(core, fx, fl_a_l, fl_a_r, AUDIO_FRAMES / 4, false);
                    for (i = 0; i < AUDIO_FRAMES / 4; i ++) {
                        test_assert(isfinite(fl_a_l[i]) && isfinite(fl_a_r[i]));
                        peak[d] = fabs((double)fl_a_l[i]) > peak[d] ?
                            fabs((double)fl_a_l[i]) : peak[d];
                    }

                    if (block >= 10) {
                        p.cutoff = 20 * powf(1000, (float)(block % 10) / 9.0f);
                        fl_direct_command(core, fx, p);
                    }
                }
            }

            printf("%d dB lowpass at resonance 1, a sine of 0.5 at its cutoff and a sweep at "
                "%.0f Hz: peak %.3f at drive 0, %.3f at drive 1\n", slope ? 24 : 12,
                (double)rates[r], peak[0], peak[1]);
            test_assert(peak[1] > 0.01);
            test_assert(peak[1] < peak[0] * 0.5);
            test_assert(peak[1] < 2);
        }

        box_audio_core_free(core);
    }

    free(fx);
}
