/**
 * @file audio/core/beat_repeat.c
 * @brief The beat repeat kind of effect: a stutter that captures a slice of the beat at a
 * grid point of the beat clock and repeats it, each repeat quieter, pitched or reversed.
 *
 * The line of each channel lives on the memory of the effect slot and records the input until
 * a capture starts, at the frame the rise of active computed from box_audio_core_beat, the
 * same whatever the render sizes. Its params are a box_audio_beat_repeat_params_t.
 *
 * The first repeat is the input as it is captured; then the line holds the slice and the room
 * its crossfades read past either end. Frames older than the ones it wrote in a row read as
 * silence, so the memory needs no clearing.
 */

#include "internal.h"

/** Fastest read of a repeat, 2 to the BOX_AUDIO_BEAT_REPEAT_PITCH_TOTAL / 12 */
#define BOX_AUDIO_BEAT_REPEAT_RATIO 16

/** Gain under which a repeat counts as silent, -140 dB */
#define BOX_AUDIO_BEAT_REPEAT_SILENCE 1e-7f

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_beat_repeat_clamp(
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

/** Get the largest frames of a crossfade at a sample rate.
 *
 * @param sample_rate Frames per second
 * @return Frames
 */
static int32_t box_audio_beat_repeat_fade_max(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_BEAT_REPEAT_FADE * sample_rate) + 1;
}

/** Get the frames the crossfades read past either end of a slice at a sample rate, at the
 * fastest read and with the frames of the interpolation.
 *
 * @param sample_rate Frames per second
 * @return Frames
 */
static int32_t box_audio_beat_repeat_room(
    float sample_rate)
{
    return box_audio_beat_repeat_fade_max(sample_rate) * BOX_AUDIO_BEAT_REPEAT_RATIO + 4;
}

/** Get the frames of the longest slice at a sample rate.
 *
 * @param sample_rate Frames per second
 * @return Frames
 */
static int32_t box_audio_beat_repeat_slice_max(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_BEAT_REPEAT_MAX * sample_rate);
}

/** Get the frames of each line at a sample rate: the longest slice and the room on either end
 * of it.
 *
 * @param sample_rate Frames per second
 * @return Frames of a line
 */
static int32_t box_audio_beat_repeat_length(
    float sample_rate)
{
    return box_audio_beat_repeat_slice_max(sample_rate) +
        2 * box_audio_beat_repeat_room(sample_rate) + 8;
}

int32_t box_audio_beat_repeat_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_beat_repeat_length(rate) * 2;
}

/** Clamp the params of an effect into the target of its beat repeat; NaN becomes the low end,
 * or 0 for the pitch.
 *
 * @param fx The effect
 */
static void box_audio_beat_repeat_target(
    box_audio_effect_t *fx)
{
    box_audio_beat_repeat_t *br = &fx->data.beat_repeat;
    const float *p = fx->params;
    br->target.active = box_audio_beat_repeat_clamp(p[0], 0, 1);
    br->target.slice = box_audio_beat_repeat_clamp(p[1], BOX_AUDIO_BEAT_REPEAT_SLICE_MIN,
        BOX_AUDIO_BEAT_REPEAT_SLICE_MAX);
    br->target.grid = p[2] < 0 ? br->target.slice :
        box_audio_beat_repeat_clamp(p[2], 0, BOX_AUDIO_BEAT_REPEAT_SLICE_MAX);
    br->target.decay = box_audio_beat_repeat_clamp(p[3], 0, 1);
    br->target.pitch = p[4] == p[4] ? box_audio_beat_repeat_clamp(p[4],
        -BOX_AUDIO_BEAT_REPEAT_PITCH_MAX, BOX_AUDIO_BEAT_REPEAT_PITCH_MAX) : 0;
    br->target.reverse = box_audio_beat_repeat_clamp(p[5], 0, 1) >= 0.5f ? 1.0f : 0.0f;
    br->target.gate = box_audio_beat_repeat_clamp(p[6], BOX_AUDIO_BEAT_REPEAT_GATE_MIN, 1);
    br->target.mix = box_audio_beat_repeat_clamp(p[7], 0, 1);
}

/** Get the floor of a beat without libm.
 *
 * @param x A finite value within the range of an int64_t
 * @return The floor
 */
static double box_audio_beat_repeat_floor(
    double x)
{
    double t = (double)(int64_t)x;

    return t > x ? t - 1 : t;
}

/** Arm a capture at the rise of active: the frame of the next grid point of the beat clock
 * after the clock of the core, the clock itself on a grid of 0 or within half a frame of a
 * point, and the frames of the slice at the tempo of the clock.
 *
 * @param core The core
 * @param br The beat repeat
 */
static void box_audio_beat_repeat_arm(
    const box_audio_core_t *core,
    box_audio_beat_repeat_t *br)
{
    double tempo = box_audio_core_tempo(core);
    double per_beat = (double)core->sample_rate / (tempo > 0 ? tempo : BOX_AUDIO_SYNTH_TEMPO);
    double beat = box_audio_core_beat(core, core->clock);
    double grid = (double)br->target.grid, frames = (double)br->target.slice * per_beat;
    double longest = (double)box_audio_beat_repeat_slice_max(core->sample_rate);
    double off, span;
    br->begin = core->clock;
    if (!(frames >= 8)) {
        frames = 8;
    } else if (frames > longest) {
        frames = longest;
    }

    br->wait = (int32_t)(frames + 0.5);
    if (grid > 0) {
        span = grid * per_beat;
        off = (beat - box_audio_beat_repeat_floor(beat / grid) * grid) * per_beat;
        if (off >= 0.5 && off < span - 0.5) {
            br->begin += (uint64_t)(span - off + 0.5);
        }
    }

    br->armed = true;
}

/** Get the frames a repeat sounds: gate of the slice, and no further than a read at a ratio
 * reaches the end of the slice.
 *
 * @param br The beat repeat
 * @param ratio Frames of the slice per frame of output
 * @return Frames, at least 1
 */
static int32_t box_audio_beat_repeat_sound(
    const box_audio_beat_repeat_t *br,
    double ratio)
{
    int32_t result = (int32_t)(br->target.gate * (float)br->slice + 0.5f);
    int32_t reach = (int32_t)((double)br->slice / ratio);
    result = result < reach ? result : reach;

    return result > 1 ? result : 1;
}

/** Start the capture at its frame: the first repeat is the input as it comes in.
 *
 * @param core The core
 * @param br The beat repeat
 */
static void box_audio_beat_repeat_begin(
    const box_audio_core_t *core,
    box_audio_beat_repeat_t *br)
{
    int32_t fade = (int32_t)(BOX_AUDIO_BEAT_REPEAT_FADE * core->sample_rate);
    br->slice = br->wait;
    fade = fade < br->slice / 4 ? fade : br->slice / 4;
    br->fade = fade > 1 ? fade : 1;
    br->origin = br->pos;
    br->before = br->frozen ? 0 : (br->valid < br->room ? br->valid : br->room);
    br->captured = 0;
    br->at = 0;
    br->pass.ratio = 1;
    br->pass.gain = 1;
    br->pass.semitones = 0;
    br->pass.reverse = false;
    br->pass.fade_in = false;
    br->pass.sound = box_audio_beat_repeat_sound(br, 1);
    br->last = br->pass;
    br->last.gain = 0;
    br->armed = false;
    br->state = BOX_AUDIO_BEAT_REPEAT_HOLD;
    box_audio_ramp_set(&br->share, 1);
}

/** Make the repeat that follows the one that played at the params of the target.
 *
 * @param br The beat repeat
 */
static void box_audio_beat_repeat_next(
    box_audio_beat_repeat_t *br)
{
    box_audio_beat_repeat_pass_t *p = &br->pass;
    br->last = *p;
    p->semitones = box_audio_beat_repeat_clamp(p->semitones + br->target.pitch,
        -BOX_AUDIO_BEAT_REPEAT_PITCH_TOTAL, BOX_AUDIO_BEAT_REPEAT_PITCH_TOTAL);
    p->ratio = p->semitones == 0 ? 1.0 :
        box_audio_synth_exp2((double)p->semitones / 12.0);
    p->gain *= br->target.decay;
    p->gain = p->gain >= BOX_AUDIO_BEAT_REPEAT_SILENCE ? p->gain : 0.0f;
    p->reverse = br->target.reverse >= 0.5f;
    p->fade_in = true;
    p->sound = box_audio_beat_repeat_sound(br, p->ratio);
    br->at = 0;
}

void box_audio_beat_repeat_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_beat_repeat_t *br = &fx->data.beat_repeat;
    (void)fade_in;
    box_audio_beat_repeat_target(fx);
    br->length = box_audio_beat_repeat_length(core->sample_rate);
    br->room = box_audio_beat_repeat_room(core->sample_rate);
    br->line[0] = fx->memory;
    br->line[1] = fx->memory + br->length;
    br->pos = 0;
    br->valid = 0;
    br->frozen = false;
    br->state = BOX_AUDIO_BEAT_REPEAT_OFF;
    br->armed = false;
    br->slice = 8;
    br->wait = 8;
    br->fade = 1;
    br->last.gain = 0;
    br->pass.gain = 0;
    box_audio_ramp_set(&br->share, 0);
    box_audio_ramp_set(&br->mix, br->target.mix);
    br->on = br->target.active >= 0.5f;
    if (br->on) {
        box_audio_beat_repeat_arm(core, br);
    }
}

void box_audio_beat_repeat_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_beat_repeat_t *br = &fx->data.beat_repeat;
    bool on;
    box_audio_beat_repeat_target(fx);
    box_audio_ramp_to(&br->mix, br->target.mix,
        (int32_t)(BOX_AUDIO_BEAT_REPEAT_RAMP * core->sample_rate));
    on = br->target.active >= 0.5f;
    if (on && !br->on) {
        box_audio_beat_repeat_arm(core, br);
    } else if (!on && br->on) {
        br->armed = false;
        if (br->state == BOX_AUDIO_BEAT_REPEAT_HOLD) {
            br->state = BOX_AUDIO_BEAT_REPEAT_RELEASE;
            box_audio_ramp_to(&br->share, 0, br->fade);
        }
    }

    br->on = on;
}

/** Read a frame of the slice of both lines with Catmull-Rom interpolation; a whole frame reads
 * exactly. Frames the line does not hold read as silence.
 *
 * @param br The beat repeat
 * @param src Frames from the start of the slice
 * @param yl Receives the left sample
 * @param yr Receives the right sample
 */
static inline void box_audio_beat_repeat_read(
    const box_audio_beat_repeat_t *br,
    double src,
    float *yl,
    float *yr)
{
    double whole = box_audio_beat_repeat_floor(src);
    int32_t w = (int32_t)whole, k, d, i;
    float t = (float)(src - whole), a[4], b[4];
    for (d = 0; d < 4; d ++) {
        a[d] = 0;
        b[d] = 0;
        k = w + d - 1;
        if ((d == 1 || t != 0) && k >= -br->before && k < br->captured) {
            i = br->origin + k;
            i += i < 0 ? br->length : 0;
            i -= i >= br->length ? br->length : 0;
            a[d] = br->line[0][i];
            b[d] = br->line[1][i];
        }
    }

    if (t == 0) {
        *yl = a[1];
        *yr = b[1];
    } else {
        float c1 = 0.5f * (a[2] - a[0]);
        float c2 = a[0] - 2.5f * a[1] + 2.0f * a[2] - 0.5f * a[3];
        float c3 = 0.5f * (a[3] - a[0]) + 1.5f * (a[1] - a[2]);
        float e1 = 0.5f * (b[2] - b[0]);
        float e2 = b[0] - 2.5f * b[1] + 2.0f * b[2] - 0.5f * b[3];
        float e3 = 0.5f * (b[3] - b[0]) + 1.5f * (b[1] - b[2]);
        *yl = ((c3 * t + c2) * t + c1) * t + a[1];
        *yr = ((e3 * t + e2) * t + e1) * t + b[1];
    }
}

/** Add a frame of a repeat to the wet signal: its gain, its fade in from the repeat before
 * and its fade out after it sounded, at its read of the slice.
 *
 * @param br The beat repeat
 * @param p The repeat
 * @param j Frames into the repeat
 * @param wl Left wet sample, added to
 * @param wr Right wet sample, added to
 */
static inline void box_audio_beat_repeat_add(
    const box_audio_beat_repeat_t *br,
    const box_audio_beat_repeat_pass_t *p,
    int32_t j,
    float *wl,
    float *wr)
{
    float e = p->gain, yl, yr;
    double src;
    if (e == 0 || j >= p->sound + br->fade) {
        goto done;
    }

    if (j >= p->sound) {
        e *= (float)(p->sound + br->fade - j) / (float)br->fade;
    }

    if (p->fade_in && j < br->fade) {
        e *= (float)j / (float)br->fade;
    }

    src = (double)j * p->ratio;
    if (p->reverse) {
        src = (double)(br->slice - 1) - src;
    }

    box_audio_beat_repeat_read(br, src, &yl, &yr);
    if (e == 1.0f) {
        *wl += yl;
        *wr += yr;
    } else {
        *wl += e * yl;
        *wr += e * yr;
    }

done:
    return;
}

bool box_audio_beat_repeat_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_beat_repeat_t *br = &fx->data.beat_repeat;
    float *bl = br->line[0], *bright = br->line[1];
    int32_t i, keep = br->slice + br->room;
    (void)silent;
    for (i = 0; i < frames; i ++) {
        float xl = l[i], xr = r[i], mix = box_audio_ramp_next(&br->mix), wl = 0, wr = 0, g;
        if (br->armed && br->state == BOX_AUDIO_BEAT_REPEAT_OFF &&
            core->clock + (uint64_t)i >= br->begin)
        {
            box_audio_beat_repeat_begin(core, br);
            keep = br->slice + br->room;
        }

        if (br->state == BOX_AUDIO_BEAT_REPEAT_OFF || br->captured < keep) {
            if (br->frozen) {
                br->frozen = false;
                br->valid = 0;
            }

            bl[br->pos] = box_audio_clean(xl);
            bright[br->pos] = box_audio_clean(xr);
            br->pos = br->pos + 1 < br->length ? br->pos + 1 : 0;
            br->valid = br->valid < br->length ? br->valid + 1 : br->valid;
            br->captured += br->state != BOX_AUDIO_BEAT_REPEAT_OFF ? 1 : 0;
        } else {
            br->frozen = true;
        }

        if (br->state == BOX_AUDIO_BEAT_REPEAT_OFF) {
            continue;
        }

        box_audio_beat_repeat_add(br, &br->pass, br->at, &wl, &wr);
        if (br->at < br->fade) {
            box_audio_beat_repeat_add(br, &br->last, br->slice + br->at, &wl, &wr);
        }

        g = box_audio_ramp_next(&br->share) * mix;
        if (g == 1.0f) {
            l[i] = wl;
            r[i] = wr;
        } else {
            l[i] = xl + g * (wl - xl);
            r[i] = xr + g * (wr - xr);
        }

        br->at ++;
        if (br->at >= br->slice) {
            box_audio_beat_repeat_next(br);
        }

        if (br->state == BOX_AUDIO_BEAT_REPEAT_RELEASE && br->share.left == 0) {
            br->state = BOX_AUDIO_BEAT_REPEAT_OFF;
        }
    }

    return false;
}

void box_audio_beat_repeat_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_beat_repeat_t *br = &fx->data.beat_repeat;
    (void)core;
    (void)frames;
    br->valid = 0;
    box_audio_ramp_set(&br->mix, br->mix.target);
}
