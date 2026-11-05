/**
 * @file audio/core/lowpass.c
 * @brief The low-pass of voices, BOX_AUDIO_PARAM_CUTOFF: a 12 dB per octave SVF per channel
 * after the source of a voice and before its volume and pan, which a voice whose filter is
 * open skips.
 */

#include "internal.h"

/** Share of the rate a low-pass of a voice opens to before it stops running, where the
 * response is flat through the audible band */
#define BOX_AUDIO_CUTOFF_OPEN_RATIO 0.49

/** Share of the rate a cutoff of a voice is clamped to */
#define BOX_AUDIO_CUTOFF_MAX_RATIO 0.45f

void box_audio_voice_filter_reset(
    box_audio_voice_t *v)
{
    v->cutoff = 0;
    v->filtered = false;
    v->fresh = true;
    v->prime = false;
    box_audio_ramp_set(&v->lowpass, 0);
    v->lp[0] = (box_audio_svf_t){ .k = (float)BOX_AUDIO_SVF_SQRT2 };
    v->lp[1] = v->lp[0];
}

void box_audio_voice_cutoff(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    float value)
{
    float rate = core->sample_rate, max = BOX_AUDIO_CUTOFF_MAX_RATIO * rate;
    float open = box_audio_svf_g(BOX_AUDIO_CUTOFF_OPEN_RATIO * (double)rate, rate), g;
    int32_t frames = (int32_t)(BOX_AUDIO_CUTOFF_RAMP * rate);
    bool closed = value > 0 && value < BOX_AUDIO_CUTOFF_OPEN;
    v->cutoff = 0;
    if (closed) {
        v->cutoff = value > BOX_AUDIO_CUTOFF_MIN ? value : BOX_AUDIO_CUTOFF_MIN;
        v->cutoff = v->cutoff < max ? v->cutoff : max;
    }

    g = (float)box_audio_synth_log2(v->cutoff > 0 ? box_audio_svf_g(v->cutoff, rate) : open);
    open = (float)box_audio_synth_log2(open);
    if (v->fresh) {
        box_audio_ramp_set(&v->lowpass, g);
        v->filtered = v->cutoff > 0;
        v->lp[0].ic1 = v->lp[0].ic2 = v->lp[1].ic1 = v->lp[1].ic2 = 0;
    } else if (!v->filtered) {
        if (v->cutoff > 0) {
            box_audio_ramp_set(&v->lowpass, open);
            v->lp[0].ic1 = v->lp[0].ic2 = v->lp[1].ic1 = v->lp[1].ic2 = 0;
            v->prime = true;
            box_audio_ramp_to(&v->lowpass, g, frames > 0 ? frames : 1);
            v->filtered = true;
        }
    } else {
        box_audio_ramp_to(&v->lowpass, g, frames > 0 ? frames : 1);
    }

    box_audio_svf_coefs(&v->lp[0], (float)box_audio_synth_exp2(v->lowpass.value));
    v->lp[1].a1 = v->lp[0].a1;
    v->lp[1].a2 = v->lp[0].a2;
    v->lp[1].a3 = v->lp[0].a3;
}

void box_audio_voice_filter(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    bool stereo,
    int32_t frames)
{
    box_audio_svf_t a = v->lp[0], b = v->lp[1];
    float *l = core->src_l, *r = core->src_r;
    bool open = !(v->cutoff > 0);
    int32_t i;
    if (v->prime && frames > 0) {
        a.ic2 = l[0];
        b.ic2 = stereo ? r[0] : l[0];
        v->prime = false;
    }

    for (i = 0; i < frames && v->filtered; i ++) {
        if (v->lowpass.left > 0) {
            box_audio_svf_coefs(&a,
                (float)box_audio_synth_exp2(box_audio_ramp_next(&v->lowpass)));
            b.a1 = a.a1;
            b.a2 = a.a2;
            b.a3 = a.a3;
            v->filtered = !open || v->lowpass.left > 0;
        }

        l[i] = box_audio_svf_next(&a, BOX_AUDIO_SYNTH_LOWPASS, l[i]);
        if (stereo) {
            r[i] = box_audio_svf_next(&b, BOX_AUDIO_SYNTH_LOWPASS, r[i]);
        }
    }

    if (!stereo) {
        b.ic1 = a.ic1;
        b.ic2 = a.ic2;
    }

    v->lp[0] = a;
    v->lp[1] = b;
}
