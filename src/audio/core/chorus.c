/**
 * @file audio/core/chorus.c
 * @brief The chorus kind of effect: up to BOX_AUDIO_CHORUS_VOICES_MAX taps of a stereo line,
 * each moved by a sine LFO around a delay, spread in phase and panned across the stereo field.
 *
 * The lines live on the memory of the slot, read with linear interpolation and, for the feedback
 * of the first tap, with a cubic one; like the delay it reads frames older than it wrote since it
 * was silent as silence. Every
 * BOX_AUDIO_MODULATION_CONTROL frames it aims each tap at its next place and moves it there.
 *
 * The phase of the LFO is a fixed point count of turns that advances by a whole step per frame,
 * the same whatever the render sizes and over the skipped blocks of an idle chorus; a change of
 * the rate changes the step, not the phase. Its params are a box_audio_chorus_params_t.
 */

#include "internal.h"

/** A tap that reads under this on both channels reads silence, for the tail of the feedback */
#define BOX_AUDIO_CHORUS_SILENCE 1e-7f

/** Turns per unit of the phase of the LFO */
#define BOX_AUDIO_CHORUS_TURN (1.0 / 4294967296.0)

/** Gain of each of n voices, 1 / sqrt(n), so their power sums to that of one */
static const float box_audio_chorus_gain[BOX_AUDIO_CHORUS_VOICES_MAX + 1] = {
    1.0f, 1.0f, 0.70710678118654752440f, 0.57735026918962576451f, 0.5f
};

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_chorus_clamp(
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
static int32_t box_audio_chorus_length(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_CHORUS_MAX * 0.001f * sample_rate) + 2;
}

int32_t box_audio_chorus_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_chorus_length(rate) * 2;
}

/** Clamp the params of an effect into the target of its chorus; NaN becomes the low end. Sets
 * the step of the LFO, so a change of the rate keeps its phase.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_chorus_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_chorus_t *ch = &fx->data.chorus;
    const float *p = fx->params;
    float voices = box_audio_chorus_clamp(p[0], 1, BOX_AUDIO_CHORUS_VOICES_MAX);
    ch->target.voices = (float)(int32_t)(voices + 0.5f);
    ch->target.rate = box_audio_chorus_clamp(p[1], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    ch->target.depth = box_audio_chorus_clamp(p[2], 0, BOX_AUDIO_CHORUS_DEPTH_MAX);
    ch->target.delay = box_audio_chorus_clamp(p[3], 0, BOX_AUDIO_CHORUS_MAX);
    ch->target.mix = box_audio_chorus_clamp(p[4], 0, 1);
    ch->target.spread = box_audio_chorus_clamp(p[5], 0, 1);
    ch->target.feedback = p[6] == p[6] ? box_audio_chorus_clamp(p[6],
        -BOX_AUDIO_CHORUS_FEEDBACK_MAX, BOX_AUDIO_CHORUS_FEEDBACK_MAX) : 0.0f;
    ch->step = (uint32_t)((double)ch->target.rate / (double)core->sample_rate * 4294967296.0);
}

/** Get where a voice reads at a phase of the LFO of the first voice, at the depth and the
 * delay that run.
 *
 * @param ch The chorus
 * @param v The voice
 * @param phase Phase in 2^-32 turns
 * @return Frames back, 1..length - 2
 */
static double box_audio_chorus_tap(
    const box_audio_chorus_t *ch,
    int32_t v,
    uint32_t phase)
{
    uint32_t offset = (uint32_t)((double)v * 4294967296.0 / (double)ch->voices);
    double turns = (double)(uint32_t)(phase + offset) * BOX_AUDIO_CHORUS_TURN;
    double result = (double)ch->delay.value +
        (double)ch->depth.value * (double)box_audio_core_sin(turns);
    double longest = (double)(ch->length - 2);
    if (!(result >= 1)) {
        result = 1;
    } else if (result > longest) {
        result = longest;
    }

    return result;
}

/** Set the gains of the voices from the spread that runs: voice v of n sits at
 * spread * (2 v / (n - 1) - 1), and a voice off the center moves the other channel of its tap
 * toward its side, so the sum of both channels stays.
 *
 * @param ch The chorus
 */
static void box_audio_chorus_pan(
    box_audio_chorus_t *ch)
{
    float g = ch->gain;
    int32_t v;
    for (v = 0; v < ch->voices; v ++) {
        float p = ch->voices > 1 ?
            ch->spread.value * (2.0f * (float)v / (float)(ch->voices - 1) - 1.0f) : 0.0f;
        float *m = ch->pan[v];
        if (p < 0) {
            m[0] = g;
            m[1] = -p * g;
            m[2] = 0;
            m[3] = (1 + p) * g;
        } else {
            m[0] = (1 - p) * g;
            m[1] = 0;
            m[2] = p * g;
            m[3] = g;
        }
    }
}

/** Put every tap where it reads at a phase, without a move.
 *
 * @param ch The chorus
 * @param phase Phase in 2^-32 turns
 */
static void box_audio_chorus_place(
    box_audio_chorus_t *ch,
    uint32_t phase)
{
    int32_t v;
    for (v = 0; v < ch->voices; v ++) {
        ch->tap[v] = box_audio_chorus_tap(ch, v, phase);
        ch->next[v] = ch->tap[v];
        ch->slope[v] = 0;
    }
}

/** Apply the voices of the target, its taps at the phase that runs.
 *
 * @param ch The chorus
 */
static void box_audio_chorus_settle(
    box_audio_chorus_t *ch)
{
    ch->voices = (int32_t)ch->target.voices;
    ch->gain = box_audio_chorus_gain[ch->voices];
    ch->swap = false;
    box_audio_chorus_pan(ch);
    box_audio_chorus_place(ch, ch->phase);
    box_audio_ramp_set(&ch->fade, 1);
}

/** Get the control blocks of a ramp of BOX_AUDIO_MODULATION_RAMP.
 *
 * @param core The core
 * @return Control blocks
 */
static int32_t box_audio_chorus_controls(
    const box_audio_core_t *core)
{
    return (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate) /
        BOX_AUDIO_MODULATION_CONTROL;
}

void box_audio_chorus_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_chorus_t *ch = &fx->data.chorus;
    float ms = 0.001f * core->sample_rate;
    box_audio_chorus_target(core, fx);
    ch->length = box_audio_chorus_length(core->sample_rate);
    ch->line[0] = fx->memory;
    ch->line[1] = fx->memory + ch->length;
    ch->pos = 0;
    ch->valid = 0;
    ch->phase = 0;
    ch->control = 0;
    ch->stale = false;
    ch->still = 0;
    box_audio_ramp_set(&ch->depth, ch->target.depth * ms);
    box_audio_ramp_set(&ch->delay, ch->target.delay * ms);
    box_audio_ramp_set(&ch->spread, ch->target.spread);
    box_audio_ramp_set(&ch->mix, ch->target.mix);
    box_audio_ramp_set(&ch->feedback, ch->target.feedback);
    box_audio_ramp_set(&ch->feed, 1);
    box_audio_chorus_settle(ch);
    if (fade_in) {
        box_audio_ramp_set(&ch->feed, 0);
        box_audio_ramp_to(&ch->feed, 1,
            (int32_t)(BOX_AUDIO_EFFECT_FADE * 0.5f * core->sample_rate));
    }
}

void box_audio_chorus_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_chorus_t *ch = &fx->data.chorus;
    float ms = 0.001f * core->sample_rate;
    int32_t ramp = (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate);
    int32_t controls = box_audio_chorus_controls(core);
    box_audio_chorus_target(core, fx);
    box_audio_ramp_to(&ch->depth, ch->target.depth * ms, controls);
    box_audio_ramp_to(&ch->delay, ch->target.delay * ms, controls);
    box_audio_ramp_to(&ch->spread, ch->target.spread, controls);
    box_audio_ramp_to(&ch->mix, ch->target.mix, ramp);
    box_audio_ramp_to(&ch->feedback, ch->target.feedback, ramp);
    if ((int32_t)ch->target.voices != ch->voices) {
        if (fx->idle) {
            box_audio_chorus_settle(ch);
        } else if (!ch->swap) {
            ch->swap = true;
            box_audio_ramp_to(&ch->fade, 0, ramp);
        }
    }
}

/** Read a line frames back, as box_audio_line_read.
 *
 * @param ch The chorus
 * @param line The line
 * @param back Frames back, 1..length - 2
 * @return The sample
 */
static inline float box_audio_chorus_read(
    const box_audio_chorus_t *ch,
    const float *line,
    double back)
{
    return box_audio_line_read(line, ch->length, ch->pos, ch->valid, back);
}

/** Read a line frames back for the feedback, with Catmull-Rom interpolation between the frames
 * either side; its gain is at most 1 at any fraction, and it keeps the highs that a linear read
 * would damp more on every pass through the loop. Under 2 frames back the frame on the near side
 * is not written yet, and it reads with linear interpolation.
 *
 * @param ch The chorus
 * @param line The line
 * @param back Frames back, 1..length - 2
 * @return The sample
 */
static inline float box_audio_chorus_read_cubic(
    const box_audio_chorus_t *ch,
    const float *line,
    double back)
{
    int32_t whole = (int32_t)back, i0, i1, i2, i3;
    float t, y0, y1, y2, y3, result;
    if (whole < 2 || whole + 2 > ch->length - 1) {
        result = box_audio_chorus_read(ch, line, back);
    } else {
        t = (float)(back - (double)whole);
        i1 = ch->pos - whole;
        i1 += i1 < 0 ? ch->length : 0;
        i0 = i1 + 1 < ch->length ? i1 + 1 : 0;
        i2 = i1 > 0 ? i1 - 1 : ch->length - 1;
        i3 = i2 > 0 ? i2 - 1 : ch->length - 1;
        y0 = whole - 1 <= ch->valid ? line[i0] : 0.0f;
        y1 = whole <= ch->valid ? line[i1] : 0.0f;
        y2 = whole + 1 <= ch->valid ? line[i2] : 0.0f;
        y3 = whole + 2 <= ch->valid ? line[i3] : 0.0f;
        result = y1 + 0.5f * t * ((y2 - y0) + t * ((2 * y0 - 5 * y1 + 4 * y2 - y3) +
            t * (3 * (y1 - y2) + y3 - y0)));
    }

    return result;
}

/** Start a control block: the taps reach where the last one aimed them, and aim at where they
 * read at the next.
 *
 * @param ch The chorus
 */
static void box_audio_chorus_control(
    box_audio_chorus_t *ch)
{
    uint32_t ahead = ch->phase + (uint32_t)BOX_AUDIO_MODULATION_CONTROL * ch->step;
    int32_t v;
    box_audio_ramp_next(&ch->depth);
    box_audio_ramp_next(&ch->delay);
    if (ch->spread.left > 0) {
        box_audio_ramp_next(&ch->spread);
        box_audio_chorus_pan(ch);
    }

    for (v = 0; v < ch->voices; v ++) {
        ch->tap[v] = ch->next[v];
        ch->next[v] = box_audio_chorus_tap(ch, v, ahead);
        ch->slope[v] = (ch->next[v] - ch->tap[v]) * (1.0 / BOX_AUDIO_MODULATION_CONTROL);
    }
}

/** Run the chorus over a block. The first tap of each channel times the feedback adds to the
 * input of its line: one tap through a read whose gain is at most 1 keeps the loop stable under
 * a feedback of 1 at any voices. The feedback reads the tap with a cubic, the voices with a
 * linear interpolation.
 *
 * @param core The core
 * @param ch The chorus
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 * @return Frames in a row up to the end of the block in which nothing fed back: the feedback
 * was 0 or the first tap read under BOX_AUDIO_CHORUS_SILENCE
 */
static int32_t box_audio_chorus_block(
    const box_audio_core_t *core,
    box_audio_chorus_t *ch,
    float *l,
    float *r,
    int32_t frames)
{
    float *bl = ch->line[0], *br = ch->line[1];
    int32_t i, v, still = 0;
    for (i = 0; i < frames; i ++) {
        float feed = box_audio_ramp_next(&ch->feed);
        float fb = box_audio_ramp_next(&ch->feedback);
        float wl = 0, wr = 0, xl = l[i] * feed, xr = r[i] * feed, wet, fade;
        if (ch->control == 0) {
            box_audio_chorus_control(ch);
            ch->control = BOX_AUDIO_MODULATION_CONTROL;
        }

        ch->control --;
        for (v = 0; v < ch->voices; v ++) {
            const float *m = ch->pan[v];
            float a = box_audio_chorus_read(ch, bl, ch->tap[v]);
            float b = box_audio_chorus_read(ch, br, ch->tap[v]);
            wl += m[0] * a + m[1] * b;
            wr += m[2] * a + m[3] * b;
            if (v == 0 && fb != 0) {
                float c = box_audio_chorus_read_cubic(ch, bl, ch->tap[v]);
                float d = box_audio_chorus_read_cubic(ch, br, ch->tap[v]);
                float sum = (c < 0 ? -c : c) + (d < 0 ? -d : d);
                xl += fb * c;
                xr += fb * d;
                still = sum < BOX_AUDIO_CHORUS_SILENCE ? still : -1;
            }

            ch->tap[v] += ch->slope[v];
        }

        still ++;
        bl[ch->pos] = box_audio_clean(xl);
        br[ch->pos] = box_audio_clean(xr);
        ch->pos = ch->pos + 1 < ch->length ? ch->pos + 1 : 0;
        ch->valid = ch->valid < ch->length ? ch->valid + 1 : ch->valid;
        ch->phase += ch->step;
        wet = box_audio_ramp_next(&ch->mix);
        fade = box_audio_ramp_next(&ch->fade);
        l[i] = l[i] * (1 - wet) + wl * (wet * fade);
        r[i] = r[i] * (1 - wet) + wr * (wet * fade);
        if (ch->swap && ch->fade.left == 0 && ch->fade.value == 0) {
            int32_t ramp = (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate);
            box_audio_chorus_settle(ch);
            box_audio_ramp_set(&ch->fade, 0);
            box_audio_ramp_to(&ch->fade, 1, ramp);
        }
    }

    return still;
}

bool box_audio_chorus_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_chorus_t *ch = &fx->data.chorus;
    int32_t still;
    bool result = false;
    if (ch->stale) {
        ch->stale = false;
        box_audio_chorus_place(ch, ch->phase + (uint32_t)ch->control * ch->step);
    }

    still = box_audio_chorus_block(core, ch, l, r, frames);
    ch->still = still < frames ? still :
        (ch->still < (1 << 30) ? ch->still + frames : ch->still);
    if (silent && fx->quiet >= ch->length && ch->still >= ch->length) {
        ch->valid = 0;
        ch->still = 0;
        if (ch->swap) {
            box_audio_chorus_settle(ch);
        }

        result = true;
    }

    return result;
}

void box_audio_chorus_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_chorus_t *ch = &fx->data.chorus;
    (void)core;
    ch->phase += (uint32_t)frames * ch->step;
    ch->control = ((ch->control - frames) % BOX_AUDIO_MODULATION_CONTROL +
        BOX_AUDIO_MODULATION_CONTROL) % BOX_AUDIO_MODULATION_CONTROL;
    ch->stale = true;
    box_audio_ramp_set(&ch->depth, ch->depth.target);
    box_audio_ramp_set(&ch->delay, ch->delay.target);
    box_audio_ramp_set(&ch->spread, ch->spread.target);
    box_audio_ramp_set(&ch->mix, ch->mix.target);
    box_audio_ramp_set(&ch->feedback, ch->feedback.target);
    box_audio_ramp_set(&ch->feed, ch->feed.target);
    if (ch->swap || ch->voices != (int32_t)ch->target.voices) {
        box_audio_chorus_settle(ch);
    } else {
        box_audio_chorus_pan(ch);
    }
}
