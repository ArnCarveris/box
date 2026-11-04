/**
 * @file audio/core/reverb.c
 * @brief The reverb kind of effect: a feedback delay network of 8 prime length lines, a
 * Hadamard feedback matrix, a one pole damping filter per line and a predelay line.
 *
 * The delay memory comes from the transport, as the memory of its effect slot. The feedback
 * gain of every line is below 1 and the matrix is orthogonal, so no setting can make the
 * network grow. Its params are a box_audio_reverb_params_t.
 */

#include "internal.h"

/** Lengths of the delay lines in seconds at size 1, spread so that no two line up */
static const float box_audio_reverb_base[BOX_AUDIO_REVERB_LINES] = {
    0.0391f, 0.0453f, 0.0539f, 0.0617f, 0.0699f, 0.0773f, 0.0861f, 0.0977f
};

/** Signs of the input into each line, a vector that is no row of the matrix, so the first
 * reflection already spreads over every line. Of the 128 sign vectors this one gives one of
 * the weakest echoes at the length of a line in both outputs, at every size. */
static const float box_audio_reverb_in[BOX_AUDIO_REVERB_LINES] = {
    1, -1, 1, 1, 1, -1, 1, 1
};

/** Signs of the lines in the left output, orthogonal to the right one */
static const float box_audio_reverb_left[BOX_AUDIO_REVERB_LINES] = {
    1, -1, 1, 1, -1, 1, -1, -1
};

/** Signs of the lines in the right output */
static const float box_audio_reverb_right[BOX_AUDIO_REVERB_LINES] = {
    1, 1, -1, 1, -1, -1, 1, -1
};

/** Scale of the matrix, the input and the outputs: 1 / sqrt(8), which keeps them orthogonal */
#define BOX_AUDIO_REVERB_NORM 0.35355339059327376f

/** Size of the shortest lines as a share of the largest ones, at size 0 */
#define BOX_AUDIO_REVERB_SIZE_MIN 0.2f

/** Frames a line may be longer than its base length, room for the next prime */
#define BOX_AUDIO_REVERB_PRIME_ROOM 128

/** Largest feedback gain of a line, so the loop gain stays under unity */
#define BOX_AUDIO_REVERB_GAIN_MAX 0.9999f

/** Largest pole of the damping filters */
#define BOX_AUDIO_REVERB_DAMP_MAX 0.98f

/** Cutoff in Hz of the damping filters at a damping just above 0, and its ratio at 1 */
#define BOX_AUDIO_REVERB_CUTOFF 18000.0
#define BOX_AUDIO_REVERB_CUTOFF_RATIO 36.0

/** Seconds a decay or a damping takes to move most of the way to a new value */
#define BOX_AUDIO_REVERB_SMOOTH 0.05

/** Seconds a change of the mix takes */
#define BOX_AUDIO_REVERB_MIX_RAMP 0.02f

/** A tail whose lines sum to less than this, -140 dB, has decayed to silence */
#define BOX_AUDIO_REVERB_SILENCE 1e-7f

/** ln(1000), for a decay to -60 dB */
#define BOX_AUDIO_REVERB_LN1000 6.907755278982137

/** ln(2) */
#define BOX_AUDIO_REVERB_LN2 0.6931471805599453

/** 2 pi */
#define BOX_AUDIO_REVERB_TAU 6.283185307179586

/** Exponential without libm, so the wasm of the core needs no imports.
 *
 * @param x The exponent
 * @return e to the x, within 1e-9 relative, 0 below -700
 */
static double box_audio_reverb_exp(
    double x)
{
    double result = 0, term = 1, sum = 1, r;
    int32_t n, i;
    if (!(x > -700)) {
        goto done;
    }

    if (x > 700) {
        x = 700;
    }

    n = (int32_t)(x / BOX_AUDIO_REVERB_LN2);
    if ((double)n * BOX_AUDIO_REVERB_LN2 > x) {
        n --;
    }

    r = x - (double)n * BOX_AUDIO_REVERB_LN2;
    for (i = 1; i < 16; i ++) {
        term *= r / (double)i;
        sum += term;
    }

    result = sum;
    for (i = 0; i < n; i ++) {
        result *= 2;
    }

    for (i = 0; i > n; i --) {
        result *= 0.5;
    }

done:
    return result;
}

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_reverb_clamp(
    float v,
    float lo,
    float hi)
{
    if (!(v >= lo)) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }

    return v;
}

/** Get the room of each line and of the predelay line at a sample rate.
 *
 * @param sample_rate Frames per second
 * @param room Receives the floats of each line
 * @param pre Receives the floats of the predelay line
 * @return Floats of all of them
 */
static int32_t box_audio_reverb_layout(
    float sample_rate,
    int32_t *room,
    int32_t *pre)
{
    int32_t i, result;
    *pre = (int32_t)(BOX_AUDIO_REVERB_PREDELAY_MAX * sample_rate) + 1;
    result = *pre;
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        room[i] = (int32_t)(box_audio_reverb_base[i] * sample_rate) + BOX_AUDIO_REVERB_PRIME_ROOM;
        result += room[i];
    }

    return result;
}

int32_t box_audio_reverb_floats(
    float sample_rate)
{
    int32_t room[BOX_AUDIO_REVERB_LINES], pre;
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_reverb_layout(rate, room, &pre);
}

/** Check whether a number is prime.
 *
 * @param n The number
 * @return Whether it is prime
 */
static bool box_audio_reverb_prime(
    int32_t n)
{
    bool result = n >= 2;
    int32_t d;
    for (d = 2; result && d * d <= n; d ++) {
        result = n % d != 0;
    }

    return result;
}

/** Set the feedback gain each line needs for the decay of the target, at its length.
 *
 * @param core The core
 * @param rv The reverb
 */
static void box_audio_reverb_gains(
    const box_audio_core_t *core,
    box_audio_reverb_t *rv)
{
    double frames = (double)rv->target.decay * (double)core->sample_rate, fc;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        float g = (float)box_audio_reverb_exp(
            -BOX_AUDIO_REVERB_LN1000 * (double)rv->length[i] / frames);
        rv->gain_target[i] = g < BOX_AUDIO_REVERB_GAIN_MAX ? g : BOX_AUDIO_REVERB_GAIN_MAX;
    }

    rv->damp_target = 0;
    if (rv->target.damping > 0) {
        fc = BOX_AUDIO_REVERB_CUTOFF * box_audio_reverb_exp(
            -(double)rv->target.damping * 3.58351893845611);
        rv->damp_target = (float)box_audio_reverb_exp(
            -BOX_AUDIO_REVERB_TAU * fc / (double)core->sample_rate);
        if (rv->damp_target > BOX_AUDIO_REVERB_DAMP_MAX) {
            rv->damp_target = BOX_AUDIO_REVERB_DAMP_MAX;
        }
    }
}

/** Clear the lines, the filters and the predelay line, as far as they are in use.
 *
 * @param fx The effect
 */
static void box_audio_reverb_clear(
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    int32_t i, k;
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        for (k = 0; k < rv->length[i]; k ++) {
            rv->line[i][k] = 0;
        }

        rv->pos[i] = 0;
        rv->lp[i] = 0;
    }

    for (k = 0; k < rv->pre_length; k ++) {
        rv->pre[k] = 0;
    }

    rv->pre_pos = 0;
    fx->quiet = 0;
    rv->still = 0;
}

/** Apply the target: the lengths of the lines for the size, the predelay and the gains, on
 * clear lines.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_reverb_apply(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    int32_t room[BOX_AUDIO_REVERB_LINES], pre, i, prev = 1;
    float scale = BOX_AUDIO_REVERB_SIZE_MIN + (1 - BOX_AUDIO_REVERB_SIZE_MIN) * rv->target.size;
    float *at = fx->memory;
    box_audio_reverb_layout(core->sample_rate, room, &pre);
    rv->pre = at;
    at += pre;
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        int32_t n = (int32_t)(box_audio_reverb_base[i] * scale * core->sample_rate);
        n = n > prev + 1 ? n : prev + 1;
        while (n < room[i] && !box_audio_reverb_prime(n)) {
            n ++;
        }

        rv->line[i] = at;
        rv->length[i] = n < room[i] ? n : room[i];
        prev = rv->length[i];
        at += room[i];
    }

    rv->pre_length = (int32_t)(rv->target.predelay * core->sample_rate + 0.5f);
    rv->pre_length = rv->pre_length < pre ? rv->pre_length : pre;
    rv->size = rv->target.size;
    rv->predelay = rv->target.predelay;
    rv->swap = false;
    box_audio_reverb_clear(fx);
    box_audio_reverb_gains(core, rv);
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        rv->gain[i] = rv->gain_target[i];
    }

    rv->damp = rv->damp_target;
    box_audio_ramp_set(&rv->feed, 1);
}

/** Fade the wet signal out to apply the target once it is silent, or at once when the
 * reverb is silent anyway.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_reverb_swap(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    if (fx->idle) {
        box_audio_reverb_apply(core, fx);
        box_audio_ramp_set(&rv->fade, 1);
    } else {
        rv->swap = true;
        box_audio_ramp_to(&rv->fade, 0,
            (int32_t)(BOX_AUDIO_REVERB_FADE * 0.5f * core->sample_rate));
    }
}

/** Clamp the params of an effect into the target of its reverb; NaN becomes the low end.
 *
 * @param fx The effect
 */
static void box_audio_reverb_target(
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    const float *p = fx->params;
    rv->target.decay = box_audio_reverb_clamp(p[0], BOX_AUDIO_REVERB_DECAY_MIN,
        BOX_AUDIO_REVERB_DECAY_MAX);
    rv->target.damping = box_audio_reverb_clamp(p[1], 0, 1);
    rv->target.predelay = box_audio_reverb_clamp(p[2], 0, BOX_AUDIO_REVERB_PREDELAY_MAX);
    rv->target.size = box_audio_reverb_clamp(p[3], 0, 1);
    rv->target.mix = box_audio_reverb_clamp(p[4], 0, 1);
}

void box_audio_reverb_init(
    box_audio_core_t *core)
{
    core->reverb_smooth = (float)(1 - box_audio_reverb_exp(-(double)BOX_AUDIO_CORE_BLOCK /
        (BOX_AUDIO_REVERB_SMOOTH * (double)core->sample_rate)));
}

void box_audio_reverb_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    box_audio_reverb_target(fx);
    box_audio_reverb_apply(core, fx);
    box_audio_ramp_set(&rv->fade, 1);
    box_audio_ramp_set(&rv->mix, rv->target.mix);
    if (fade_in) {
        box_audio_ramp_set(&rv->feed, 0);
        box_audio_ramp_to(&rv->feed, 1,
            (int32_t)(BOX_AUDIO_REVERB_FADE * 0.5f * core->sample_rate));
    }
}

void box_audio_reverb_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    int32_t i;
    box_audio_reverb_target(fx);
    box_audio_reverb_gains(core, rv);
    for (i = 0; fx->idle && i < BOX_AUDIO_REVERB_LINES; i ++) {
        rv->gain[i] = rv->gain_target[i];
    }

    rv->damp = fx->idle ? rv->damp_target : rv->damp;
    box_audio_ramp_to(&rv->mix, rv->target.mix,
        (int32_t)(BOX_AUDIO_REVERB_MIX_RAMP * core->sample_rate));
    if (rv->target.size != rv->size || rv->target.predelay != rv->predelay) {
        box_audio_reverb_swap(core, fx);
    } else if (rv->swap) {
        rv->swap = false;
        box_audio_ramp_to(&rv->fade, 1,
            (int32_t)(BOX_AUDIO_REVERB_FADE * 0.5f * core->sample_rate));
    }
}

/** Move the gains and the pole of the damping a step toward their targets, once per block.
 *
 * @param core The core
 * @param rv The reverb
 */
static void box_audio_reverb_smooth(
    const box_audio_core_t *core,
    box_audio_reverb_t *rv)
{
    float k = core->reverb_smooth;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_REVERB_LINES; i ++) {
        rv->gain[i] += (rv->gain_target[i] - rv->gain[i]) * k;
    }

    rv->damp += (rv->damp_target - rv->damp) * k;
}

/** Run the network over a block.
 *
 * @param rv The reverb
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 * @return Frames at the end of the block in which the lines summed under
 * BOX_AUDIO_REVERB_SILENCE
 */
static int32_t box_audio_reverb_block(
    box_audio_reverb_t *rv,
    float *l,
    float *r,
    int32_t frames)
{
    float v[BOX_AUDIO_REVERB_LINES], o[BOX_AUDIO_REVERB_LINES], a, b;
    float damp = rv->damp;
    int32_t i, k, h, j, still = 0;
    for (i = 0; i < frames; i ++) {
        float x = (l[i] + r[i]) * 0.5f * box_audio_ramp_next(&rv->feed);
        float wl = 0, wr = 0, sum = 0, wet, dry;
        if (rv->pre_length) {
            float p = rv->pre[rv->pre_pos];
            rv->pre[rv->pre_pos] = x;
            rv->pre_pos = rv->pre_pos + 1 < rv->pre_length ? rv->pre_pos + 1 : 0;
            x = p;
        }

        x *= BOX_AUDIO_REVERB_NORM;
        for (k = 0; k < BOX_AUDIO_REVERB_LINES; k ++) {
            o[k] = rv->line[k][rv->pos[k]];
            rv->lp[k] = box_audio_clean(o[k] + damp * (rv->lp[k] - o[k]));
            v[k] = rv->lp[k] * rv->gain[k];
            wl += box_audio_reverb_left[k] * o[k];
            wr += box_audio_reverb_right[k] * o[k];
            sum += o[k] < 0 ? -o[k] : o[k];
        }

        for (h = 1; h < BOX_AUDIO_REVERB_LINES; h *= 2) {
            for (k = 0; k < BOX_AUDIO_REVERB_LINES; k += h * 2) {
                for (j = k; j < k + h; j ++) {
                    a = v[j];
                    b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
            }
        }

        for (k = 0; k < BOX_AUDIO_REVERB_LINES; k ++) {
            rv->line[k][rv->pos[k]] = box_audio_clean(
                v[k] * BOX_AUDIO_REVERB_NORM + x * box_audio_reverb_in[k]);
            rv->pos[k] = rv->pos[k] + 1 < rv->length[k] ? rv->pos[k] + 1 : 0;
        }

        still = sum < BOX_AUDIO_REVERB_SILENCE ? still + 1 : 0;
        wet = box_audio_ramp_next(&rv->mix);
        dry = 1 - wet;
        wet *= box_audio_ramp_next(&rv->fade) * BOX_AUDIO_REVERB_NORM;
        l[i] = l[i] * dry + wl * wet;
        r[i] = r[i] * dry + wr * wet;
    }

    return still;
}

bool box_audio_reverb_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    int32_t still, longest = rv->length[BOX_AUDIO_REVERB_LINES - 1];
    bool result = false;
    box_audio_reverb_smooth(core, rv);
    still = box_audio_reverb_block(rv, l, r, frames);
    rv->still = still < frames ? still : (rv->still < (1 << 30) ? rv->still + frames : rv->still);
    if (rv->swap && rv->fade.left == 0 && rv->fade.value == 0) {
        box_audio_reverb_apply(core, fx);
        box_audio_ramp_to(&rv->fade, 1.0f,
            (int32_t)(BOX_AUDIO_REVERB_FADE * 0.5f * core->sample_rate));
        box_audio_ramp_set(&rv->feed, 0);
        box_audio_ramp_to(&rv->feed, 1,
            (int32_t)(BOX_AUDIO_REVERB_FADE * 0.5f * core->sample_rate));
    } else if (silent && fx->quiet >= rv->pre_length + longest && rv->still >= longest) {
        box_audio_reverb_clear(fx);
        result = true;
    }

    return result;
}

void box_audio_reverb_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_reverb_t *rv = &fx->data.reverb;
    if (rv->swap) {
        box_audio_reverb_apply(core, fx);
        box_audio_ramp_set(&rv->fade, 1);
    }

    box_audio_ramp_set(&rv->mix, rv->mix.target);
}
