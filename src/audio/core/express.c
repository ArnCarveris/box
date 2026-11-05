/**
 * @file audio/core/express.c
 * @brief The expression of voices: the ramps of the bend, the mod and the pressure of a voice,
 * which its source takes every BOX_AUDIO_EXPRESS_FRAMES frames while they move.
 */

#include "internal.h"

/** Give the expression of a voice to its source: the factor of the bend, the mod and the
 * pressure at the values of its ramps.
 *
 * @param core The core
 * @param v The voice
 */
static void box_audio_voice_express(
    box_audio_core_t *core,
    box_audio_voice_t *v)
{
    double bend = v->bend.value != 0 ?
        box_audio_synth_exp2((double)v->bend.value * (1.0 / 12)) : 1.0;
    box_audio_source_express(core, &v->source, bend, v->mod.value, v->pressure.value);
}

void box_audio_voice_express_reset(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    bool apply)
{
    bool moved = v->bend.value != 0 || v->mod.value != 1 || v->pressure.value != 0;
    box_audio_ramp_set(&v->bend, 0);
    box_audio_ramp_set(&v->mod, 1);
    box_audio_ramp_set(&v->pressure, 0);
    v->expressive = false;
    if (apply && moved) {
        box_audio_voice_express(core, v);
    }
}

void box_audio_voice_express_next(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    int32_t frames)
{
    box_audio_ramp_t *ramps[3] = { &v->bend, &v->mod, &v->pressure };
    int32_t k;
    for (k = 0; k < 3; k ++) {
        box_audio_ramp_t *r = ramps[k];
        if (r->left > frames) {
            r->left -= frames;
            r->value += r->step * (float)frames;
        } else if (r->left > 0) {
            r->left = 0;
            r->value = r->target;
        }
    }

    box_audio_voice_express(core, v);
    v->expressive = v->bend.left > 0 || v->mod.left > 0 || v->pressure.left > 0;
}

void box_audio_voice_ramp(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    uint32_t param,
    float value,
    uint32_t frames)
{
    box_audio_ramp_t *r = param == BOX_AUDIO_PARAM_BEND ? &v->bend :
        (param == BOX_AUDIO_PARAM_MOD ? &v->mod : &v->pressure);
    int32_t n = frames < (uint32_t)INT32_MAX ? (int32_t)frames : INT32_MAX;
    float lo = param == BOX_AUDIO_PARAM_BEND ? -BOX_AUDIO_BEND_MAX : 0.0f;
    float hi = param == BOX_AUDIO_PARAM_BEND ? BOX_AUDIO_BEND_MAX : 1.0f;
    value = !(value >= lo) ? lo : (value > hi ? hi : value);
    if (n > 0) {
        box_audio_ramp_to(r, value, n);
    } else {
        box_audio_ramp_set(r, value);
    }

    box_audio_voice_express(core, v);
    v->expressive = v->bend.left > 0 || v->mod.left > 0 || v->pressure.left > 0;
}
