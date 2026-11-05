/**
 * @file audio/core/rotary.c
 * @brief The rotary kind of effect: a rotating speaker cabinet. A tube preamp, a crossover
 * into a drum and a horn that turn and accelerate with their own inertia, and two mics on each
 * rotor that hear its Doppler shift, its level and its tone change as it turns.
 *
 * The preamp is the clipper of saturate.h on the sum of both channels, blended in by the drive;
 * a Linkwitz-Riley crossover of svf.h stages feeds a line per rotor, read as the chorus reads.
 * Every BOX_AUDIO_MODULATION_CONTROL frames, also while idle, the rotors turn on.
 */

#include "internal.h"

/** log2(e), for exp by box_audio_synth_exp2 */
#define BOX_AUDIO_ROTARY_LOG2E 1.4426950408889634074

/** 2 pi */
#define BOX_AUDIO_ROTARY_TAU 6.28318530717958647692

/** Time constants of a move of the rate of the horn per second of acceleration: the rate is
 * within 5% of its target after the acceleration */
#define BOX_AUDIO_ROTARY_CONSTANTS 3.0

/** Index of the drum and of the horn among the rotors */
#define BOX_AUDIO_ROTARY_DRUM 0
#define BOX_AUDIO_ROTARY_HORN 1

/** Share of the level each rotor loses facing away from a mic */
static const float box_audio_rotary_am[2] = {
    BOX_AUDIO_ROTARY_DRUM_AM, BOX_AUDIO_ROTARY_HORN_AM
};

/** Share of the lowpass each rotor reaches facing away from a mic */
static const float box_audio_rotary_dull[2] = {
    BOX_AUDIO_ROTARY_DRUM_DULL, BOX_AUDIO_ROTARY_HORN_DULL
};

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_rotary_clamp(
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

/** Get the frames of each line at a sample rate: the longest read, a frame for the
 * interpolation and a frame for the write position.
 *
 * @param sample_rate Frames per second
 * @return Frames of a line
 */
static int32_t box_audio_rotary_length(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_ROTARY_MAX * 0.001f * sample_rate) + 2;
}

int32_t box_audio_rotary_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_rotary_length(rate) * 2;
}

/** Get the share of the way to its target a rotor moves per control block.
 *
 * @param seconds Time constant
 * @param sample_rate Frames per second
 * @return The share, 0..1
 */
static double box_audio_rotary_coef(
    double seconds,
    float sample_rate)
{
    double blocks = seconds * (double)sample_rate / BOX_AUDIO_MODULATION_CONTROL;

    return 1.0 - box_audio_synth_exp2(-BOX_AUDIO_ROTARY_LOG2E / blocks);
}

/** Clamp the params of an effect into the target of its rotary, and aim the rotors at the
 * rates of the speed.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_rotary_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_rotary_t *rt = &fx->data.rotary;
    box_audio_rotary_params_t *t = &rt->target;
    const float *p = fx->params;
    double tau;
    t->speed = box_audio_rotary_clamp(p[0], 0, 1);
    t->horn_slow = box_audio_rotary_clamp(p[1], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    t->horn_fast = box_audio_rotary_clamp(p[2], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    t->drum_slow = box_audio_rotary_clamp(p[3], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    t->drum_fast = box_audio_rotary_clamp(p[4], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    t->acceleration = box_audio_rotary_clamp(p[5], BOX_AUDIO_ROTARY_ACCELERATION_MIN,
        BOX_AUDIO_ROTARY_ACCELERATION_MAX);
    t->balance = box_audio_rotary_clamp(p[6], -1, 1);
    t->spread = box_audio_rotary_clamp(p[7], 0, 1);
    t->drive = box_audio_rotary_clamp(p[8], 0, 1);
    t->mix = box_audio_rotary_clamp(p[9], 0, 1);
    rt->rotor[BOX_AUDIO_ROTARY_DRUM].target = (double)t->drum_slow +
        (double)t->speed * (double)(t->drum_fast - t->drum_slow);
    rt->rotor[BOX_AUDIO_ROTARY_HORN].target = (double)t->horn_slow +
        (double)t->speed * (double)(t->horn_fast - t->horn_slow);
    tau = (double)t->acceleration / BOX_AUDIO_ROTARY_CONSTANTS;
    rt->rotor[BOX_AUDIO_ROTARY_HORN].coef = box_audio_rotary_coef(tau, core->sample_rate);
    rt->rotor[BOX_AUDIO_ROTARY_DRUM].coef = box_audio_rotary_coef(
        tau * BOX_AUDIO_ROTARY_DRUM_INERTIA, core->sample_rate);
}

/** Set the preamp for a drive: a gain of up to BOX_AUDIO_ROTARY_DRIVE_DB and a bias of up to
 * BOX_AUDIO_ROTARY_BIAS.
 *
 * @param rt The rotary
 * @param drive 0..1
 */
static void box_audio_rotary_curve(
    box_audio_rotary_t *rt,
    float drive)
{
    float gain = (float)box_audio_synth_exp2((double)(drive * BOX_AUDIO_ROTARY_DRIVE_DB) *
        (3.3219280948873623 / 20.0));
    box_audio_saturate_curve(&rt->curve, gain, drive * BOX_AUDIO_ROTARY_BIAS);
    rt->curve_at = drive;
}

/** Get the gain of the drum and of the horn at a balance.
 *
 * @param balance -1..1
 * @param drum Receives the gain of the drum
 * @param horn Receives the gain of the horn
 */
static void box_audio_rotary_balance(
    float balance,
    float *drum,
    float *horn)
{
    *drum = balance > 0 ? 1 - balance : 1.0f;
    *horn = balance < 0 ? 1 + balance : 1.0f;
}

/** Get where a mic of a rotor reads, its gain and the share of its lowpass, at a phase of the
 * rotor and a spread. The left mic sits a quarter turn times the spread one way, the right one
 * as far the other way; the drum turns the other way than the horn.
 *
 * @param rt The rotary
 * @param r The rotor
 * @param m The mic, 0 left, 1 right
 * @param phase Turns of the rotor
 * @param spread 0..1
 * @param tap Receives frames back, 1..length - 2
 * @param gain Receives the gain
 * @param dull Receives the share of the lowpass
 */
static void box_audio_rotary_aim(
    const box_audio_rotary_t *rt,
    int32_t r,
    int32_t m,
    double phase,
    float spread,
    double *tap,
    float *gain,
    float *dull)
{
    double angle = (m ? 0.25 : -0.25) * (double)spread;
    double turn = (r == BOX_AUDIO_ROTARY_HORN ? phase : -phase) - angle;
    float facing = box_audio_core_sin(turn + 0.25);
    float away = 0.5f * (1.0f - facing);
    double back = (double)rt->center[r] - (double)rt->depth[r] * (double)facing;
    double longest = (double)(rt->length - 2);
    if (!(back >= 1)) {
        back = 1;
    } else if (back > longest) {
        back = longest;
    }

    *tap = back;
    *gain = 1.0f - box_audio_rotary_am[r] * away;
    *dull = box_audio_rotary_dull[r] * away;
}

/** Put every mic where it is at the phases of the rotors, without a move.
 *
 * @param rt The rotary
 */
static void box_audio_rotary_place(
    box_audio_rotary_t *rt)
{
    int32_t r, m;
    for (r = 0; r < 2; r ++) {
        for (m = 0; m < 2; m ++) {
            box_audio_rotary_mic_t *mic = &rt->mic[r][m];
            box_audio_rotary_aim(rt, r, m, rt->rotor[r].phase, rt->spread.value,
                &mic->tap_next, &mic->gain_next, &mic->dull_next);
            mic->tap = mic->tap_next;
            mic->gain = mic->gain_next;
            mic->dull = mic->dull_next;
            mic->tap_slope = 0;
            mic->gain_slope = 0;
            mic->dull_slope = 0;
        }
    }
}

/** Move the rates of the rotors toward their targets and turn them on by a control block.
 *
 * @param rt The rotary
 * @param sample_rate Frames per second
 */
static void box_audio_rotary_turn(
    box_audio_rotary_t *rt,
    float sample_rate)
{
    double seconds = BOX_AUDIO_MODULATION_CONTROL / (double)sample_rate;
    int32_t r;
    for (r = 0; r < 2; r ++) {
        box_audio_rotary_rotor_t *rotor = &rt->rotor[r];
        rotor->rate += (rotor->target - rotor->rate) * rotor->coef;
        rotor->phase += rotor->rate * seconds;
        rotor->phase -= (double)(int32_t)rotor->phase;
    }
}

/** Absolute value of a sample.
 *
 * @param x The sample
 * @return The absolute value
 */
static float box_audio_rotary_abs(
    float x)
{
    return x < 0 ? -x : x;
}

/** Clear the preamp, the crossover, the lowpasses of the mics and the lines.
 *
 * @param rt The rotary
 */
static void box_audio_rotary_clear(
    box_audio_rotary_t *rt)
{
    int32_t i, m;
    box_audio_oversample_clear(&rt->os);
    rt->dc.x1 = 0;
    rt->dc.y1 = 0;
    for (i = 0; i < 16; i ++) {
        rt->dry[i] = 0;
    }

    rt->dry_pos = 0;
    for (i = 0; i < 2; i ++) {
        rt->low[i].ic1 = 0;
        rt->low[i].ic2 = 0;
        rt->high[i].ic1 = 0;
        rt->high[i].ic2 = 0;
        for (m = 0; m < 2; m ++) {
            rt->mic[i][m].lp = 0;
        }
    }

    rt->valid = 0;
}

/** Sum the levels left in the preamp, the crossover and the mics, and add the peak of the
 * lines, to tell when a tail, such as the decay of the DC blocker, has died out.
 *
 * @param rt The rotary
 * @return The level of the state
 */
static float box_audio_rotary_level(
    const box_audio_rotary_t *rt)
{
    float sum = box_audio_oversample_level(&rt->os), peak = 0;
    int32_t i, m;
    sum += box_audio_rotary_abs(rt->dc.x1) + box_audio_rotary_abs(rt->dc.y1);
    for (i = 0; i < 16; i ++) {
        sum += box_audio_rotary_abs(rt->dry[i]);
    }

    for (i = 0; i < 2; i ++) {
        sum += box_audio_rotary_abs(rt->low[i].ic1) + box_audio_rotary_abs(rt->low[i].ic2) +
            box_audio_rotary_abs(rt->high[i].ic1) + box_audio_rotary_abs(rt->high[i].ic2);
        for (m = 0; m < 2; m ++) {
            sum += box_audio_rotary_abs(rt->mic[i][m].lp);
        }
    }

    for (i = 0; i < rt->length; i ++) {
        float d = box_audio_rotary_abs(rt->line[BOX_AUDIO_ROTARY_DRUM][i]);
        float h = box_audio_rotary_abs(rt->line[BOX_AUDIO_ROTARY_HORN][i]);
        peak = d > peak ? d : peak;
        peak = h > peak ? h : peak;
    }

    return sum + peak;
}

void box_audio_rotary_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_rotary_t *rt = &fx->data.rotary;
    float ms = 0.001f * core->sample_rate, g, drum, horn;
    int32_t i;
    box_audio_rotary_target(core, fx);
    rt->length = box_audio_rotary_length(core->sample_rate);
    rt->line[0] = fx->memory;
    rt->line[1] = fx->memory + rt->length;
    rt->pos = 0;
    rt->control = 0;
    rt->stale = false;
    rt->center[BOX_AUDIO_ROTARY_DRUM] = BOX_AUDIO_ROTARY_DRUM_DELAY * ms;
    rt->depth[BOX_AUDIO_ROTARY_DRUM] = BOX_AUDIO_ROTARY_DRUM_DEPTH * ms;
    rt->center[BOX_AUDIO_ROTARY_HORN] = BOX_AUDIO_ROTARY_HORN_DELAY * ms;
    rt->depth[BOX_AUDIO_ROTARY_HORN] = BOX_AUDIO_ROTARY_HORN_DEPTH * ms;
    rt->tone[BOX_AUDIO_ROTARY_DRUM] = (float)(1.0 - box_audio_synth_exp2(
        -BOX_AUDIO_ROTARY_LOG2E * BOX_AUDIO_ROTARY_TAU * BOX_AUDIO_ROTARY_DRUM_TONE /
        (double)core->sample_rate));
    rt->tone[BOX_AUDIO_ROTARY_HORN] = (float)(1.0 - box_audio_synth_exp2(
        -BOX_AUDIO_ROTARY_LOG2E * BOX_AUDIO_ROTARY_TAU * BOX_AUDIO_ROTARY_HORN_TONE /
        (double)core->sample_rate));
    g = box_audio_svf_g(BOX_AUDIO_ROTARY_CROSSOVER, core->sample_rate);
    for (i = 0; i < 2; i ++) {
        rt->low[i] = (box_audio_svf_t){ .k = (float)BOX_AUDIO_SVF_SQRT2 };
        rt->high[i] = (box_audio_svf_t){ .k = (float)BOX_AUDIO_SVF_SQRT2 };
        box_audio_svf_coefs(&rt->low[i], g);
        box_audio_svf_coefs(&rt->high[i], g);
        rt->rotor[i].phase = 0;
        rt->rotor[i].rate = rt->rotor[i].target;
    }

    box_audio_rotary_clear(rt);
    rt->dc_coef = (float)(1.0 - BOX_AUDIO_ROTARY_TAU *
        (double)BOX_AUDIO_SATURATOR_DC / (double)core->sample_rate);
    box_audio_rotary_balance(rt->target.balance, &drum, &horn);
    box_audio_ramp_set(&rt->drive, rt->target.drive);
    box_audio_ramp_set(&rt->spread, rt->target.spread);
    box_audio_ramp_set(&rt->drum, drum);
    box_audio_ramp_set(&rt->horn, horn);
    box_audio_ramp_set(&rt->mix, rt->target.mix);
    box_audio_ramp_set(&rt->feed, 1);
    box_audio_rotary_curve(rt, rt->target.drive);
    box_audio_rotary_place(rt);
    if (fade_in) {
        box_audio_ramp_set(&rt->feed, 0);
        box_audio_ramp_to(&rt->feed, 1,
            (int32_t)(BOX_AUDIO_EFFECT_FADE * 0.5f * core->sample_rate));
    }
}

void box_audio_rotary_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_rotary_t *rt = &fx->data.rotary;
    int32_t ramp = (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate);
    int32_t controls = ramp / BOX_AUDIO_MODULATION_CONTROL;
    float drum, horn;
    box_audio_rotary_target(core, fx);
    box_audio_rotary_balance(rt->target.balance, &drum, &horn);
    box_audio_ramp_to(&rt->drive, rt->target.drive, ramp);
    box_audio_ramp_to(&rt->spread, rt->target.spread, controls);
    box_audio_ramp_to(&rt->drum, drum, ramp);
    box_audio_ramp_to(&rt->horn, horn, ramp);
    box_audio_ramp_to(&rt->mix, rt->target.mix, ramp);
}

/** Start a control block: the rotors turn on, the mics reach where the last one aimed them and
 * aim at where they are at the next.
 *
 * @param core The core
 * @param rt The rotary
 */
static void box_audio_rotary_control(
    const box_audio_core_t *core,
    box_audio_rotary_t *rt)
{
    const float step = 1.0f / BOX_AUDIO_MODULATION_CONTROL;
    int32_t r, m;
    box_audio_ramp_next(&rt->spread);
    box_audio_rotary_turn(rt, core->sample_rate);
    for (r = 0; r < 2; r ++) {
        for (m = 0; m < 2; m ++) {
            box_audio_rotary_mic_t *mic = &rt->mic[r][m];
            mic->tap = mic->tap_next;
            mic->gain = mic->gain_next;
            mic->dull = mic->dull_next;
            box_audio_rotary_aim(rt, r, m, rt->rotor[r].phase, rt->spread.value,
                &mic->tap_next, &mic->gain_next, &mic->dull_next);
            mic->tap_slope = (mic->tap_next - mic->tap) * (1.0 / BOX_AUDIO_MODULATION_CONTROL);
            mic->gain_slope = (mic->gain_next - mic->gain) * step;
            mic->dull_slope = (mic->dull_next - mic->dull) * step;
        }
    }
}

/** Read a mic of a rotor for a frame and move it on.
 *
 * @param rt The rotary
 * @param r The rotor
 * @param m The mic
 * @return The sample of the mic
 */
static inline float box_audio_rotary_mic(
    box_audio_rotary_t *rt,
    int32_t r,
    int32_t m)
{
    box_audio_rotary_mic_t *mic = &rt->mic[r][m];
    float s = box_audio_line_read(rt->line[r], rt->length, rt->pos, rt->valid, mic->tap);
    float result;
    mic->lp = box_audio_clean(mic->lp + rt->tone[r] * (s - mic->lp));
    result = (s + mic->dull * (mic->lp - s)) * mic->gain;
    mic->tap += mic->tap_slope;
    mic->gain += mic->gain_slope;
    mic->dull += mic->dull_slope;

    return result;
}

/** Run the rotary over a block.
 *
 * @param core The core
 * @param rt The rotary
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 */
static void box_audio_rotary_block(
    const box_audio_core_t *core,
    box_audio_rotary_t *rt,
    float *l,
    float *r,
    int32_t frames)
{
    float *drum_line = rt->line[BOX_AUDIO_ROTARY_DRUM];
    float *horn_line = rt->line[BOX_AUDIO_ROTARY_HORN];
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float feed = box_audio_ramp_next(&rt->feed);
        float drive = box_audio_ramp_next(&rt->drive);
        float x = 0.5f * (l[i] + r[i]) * feed, d, y, pre, low, high, dg, hg, wl, wr, wet;
        uint32_t at = rt->dry_pos & 15, from = (rt->dry_pos + 1) & 15;
        if (drive != rt->curve_at) {
            box_audio_rotary_curve(rt, drive);
        }

        if (rt->control == 0) {
            box_audio_rotary_control(core, rt);
            rt->control = BOX_AUDIO_MODULATION_CONTROL;
        }

        rt->control --;

        d = rt->dry[from];
        rt->dry[at] = x;
        rt->dry_pos ++;
        y = box_audio_saturate_next(&rt->os, &rt->curve, x);
        y = box_audio_clean(box_audio_dc_next(&rt->dc, y, rt->dc_coef));
        pre = d + drive * (y - d);
        low = box_audio_svf_next(&rt->low[1], BOX_AUDIO_SYNTH_LOWPASS,
            box_audio_svf_next(&rt->low[0], BOX_AUDIO_SYNTH_LOWPASS, pre));
        high = box_audio_svf_next(&rt->high[1], BOX_AUDIO_SYNTH_HIGHPASS,
            box_audio_svf_next(&rt->high[0], BOX_AUDIO_SYNTH_HIGHPASS, pre));
        dg = box_audio_ramp_next(&rt->drum);
        hg = box_audio_ramp_next(&rt->horn);
        wl = dg * box_audio_rotary_mic(rt, BOX_AUDIO_ROTARY_DRUM, 0) +
            hg * box_audio_rotary_mic(rt, BOX_AUDIO_ROTARY_HORN, 0);
        wr = dg * box_audio_rotary_mic(rt, BOX_AUDIO_ROTARY_DRUM, 1) +
            hg * box_audio_rotary_mic(rt, BOX_AUDIO_ROTARY_HORN, 1);
        drum_line[rt->pos] = box_audio_clean(low);
        horn_line[rt->pos] = box_audio_clean(high);
        rt->pos = rt->pos + 1 < rt->length ? rt->pos + 1 : 0;
        rt->valid = rt->valid < rt->length ? rt->valid + 1 : rt->valid;
        wet = box_audio_ramp_next(&rt->mix);
        l[i] = l[i] * (1 - wet) + wl * wet;
        r[i] = r[i] * (1 - wet) + wr * wet;
    }
}

bool box_audio_rotary_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_rotary_t *rt = &fx->data.rotary;
    int32_t quiet = (int32_t)(BOX_AUDIO_ROTARY_QUIET * core->sample_rate);
    bool result = false;
    if (rt->stale) {
        rt->stale = false;
        box_audio_rotary_place(rt);
    }

    box_audio_rotary_block(core, rt, l, r, frames);
    if (silent && fx->quiet >= (quiet > rt->length ? quiet : rt->length) &&
        box_audio_rotary_level(rt) < BOX_AUDIO_ROTARY_SILENCE)
    {
        box_audio_rotary_clear(rt);
        result = true;
    }

    return result;
}

void box_audio_rotary_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_rotary_t *rt = &fx->data.rotary;
    int32_t left = frames;
    while (left > 0) {
        int32_t n;
        if (rt->control == 0) {
            box_audio_rotary_turn(rt, core->sample_rate);
            rt->control = BOX_AUDIO_MODULATION_CONTROL;
        }

        n = rt->control < left ? rt->control : left;
        rt->control -= n;
        left -= n;
    }

    rt->stale = true;
    box_audio_ramp_set(&rt->drive, rt->drive.target);
    box_audio_ramp_set(&rt->spread, rt->spread.target);
    box_audio_ramp_set(&rt->drum, rt->drum.target);
    box_audio_ramp_set(&rt->horn, rt->horn.target);
    box_audio_ramp_set(&rt->mix, rt->mix.target);
    box_audio_ramp_set(&rt->feed, rt->feed.target);
}
