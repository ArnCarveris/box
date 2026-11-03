/**
 * @file audio/core/source.c
 * @brief Sources of voices: a sample buffer, or a synth patch (synth.c). A new kind adds a case
 * to each function.
 */

#include "internal.h"

void box_audio_source_sample(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    bool loop)
{
    box_audio_sound_t *s = &core->sounds[sound];
    source->kind = BOX_AUDIO_SOURCE_SAMPLE;
    source->data.sample.sound = sound;
    source->data.sample.position = 0;
    source->data.sample.rate = s->rate;
    source->data.sample.step = s->rate * (double)pitch;
    source->data.sample.loop = loop;
    s->users ++;
}

void box_audio_source_pitch(
    box_audio_source_t *source,
    float pitch)
{
    if (source->kind == BOX_AUDIO_SOURCE_SAMPLE) {
        source->data.sample.step = source->data.sample.rate * (double)pitch;
    } else if (source->kind == BOX_AUDIO_SOURCE_SYNTH) {
        box_audio_synth_pitch(&source->data.synth, pitch);
    }
}

/** Wrap a position into a sound of frames frames.
 *
 * @param position Position, at least 0
 * @param frames Frames of the sound
 * @return The position within 0..frames
 */
static double box_audio_wrap(
    double position,
    int32_t frames)
{
    double turns = position / (double)frames;
    position -= (double)(int64_t)turns * (double)frames;
    if (position >= (double)frames || position < 0) {
        position = 0;
    }

    return position;
}

void box_audio_source_seek(
    box_audio_core_t *core,
    box_audio_source_t *source,
    float position)
{
    box_audio_sample_source_t *src = &source->data.sample;
    int32_t frames;
    double p = position > 0 && position < 1e9f ? (double)position : 0;
    if (source->kind != BOX_AUDIO_SOURCE_SAMPLE) {
        goto done;
    }

    frames = core->sounds[src->sound].frames;
    if (frames <= 0) {
        goto done;
    }

    if (src->loop) {
        p = box_audio_wrap(p, frames);
    } else if (p > (double)frames) {
        p = (double)frames;
    }

    src->position = p;
done:
    return;
}

/** Render a block of a sample source.
 *
 * @param core The core
 * @param src The source
 * @param frames Frames to render
 * @param stereo Set to whether the sound is stereo
 * @return Frames it rendered
 */
static int32_t box_audio_sample_render(
    box_audio_core_t *core,
    box_audio_sample_source_t *src,
    int32_t frames,
    bool *stereo)
{
    const box_audio_sound_t *s = &core->sounds[src->sound];
    const float *data = s->data;
    int32_t count = s->frames, i;
    double position = src->position, step = src->step;
    float *l = core->src_l, *r = core->src_r;
    bool loop = src->loop;
    *stereo = s->channels == 2;
    for (i = 0; i < frames; i ++) {
        int32_t at, next;
        float frac, a, b;
        if (position >= (double)count) {
            if (!loop) {
                break;
            }

            position = box_audio_wrap(position, count);
        }

        at = (int32_t)position;
        frac = (float)(position - (double)at);
        next = at + 1 < count ? at + 1 : (loop ? 0 : -1);
        if (*stereo) {
            a = data[at * 2];
            b = next >= 0 ? data[next * 2] : 0;
            l[i] = box_audio_clean(a + (b - a) * frac);
            a = data[at * 2 + 1];
            b = next >= 0 ? data[next * 2 + 1] : 0;
            r[i] = box_audio_clean(a + (b - a) * frac);
        } else {
            a = data[at];
            b = next >= 0 ? data[next] : 0;
            l[i] = box_audio_clean(a + (b - a) * frac);
        }

        position += step;
    }

    src->position = position;

    return i;
}

int32_t box_audio_source_render(
    box_audio_core_t *core,
    box_audio_source_t *source,
    int32_t frames,
    bool *stereo)
{
    int32_t done = 0, i;
    *stereo = false;
    if (source->kind == BOX_AUDIO_SOURCE_SAMPLE) {
        done = box_audio_sample_render(core, &source->data.sample, frames, stereo);
    } else if (source->kind == BOX_AUDIO_SOURCE_SYNTH) {
        done = box_audio_synth_render(core, &source->data.synth, frames);
    }

    for (i = done; i < frames; i ++) {
        core->src_l[i] = 0;
        core->src_r[i] = 0;
    }

    return done;
}

void box_audio_source_end(
    box_audio_core_t *core,
    box_audio_source_t *source)
{
    if (source->kind == BOX_AUDIO_SOURCE_SAMPLE) {
        box_audio_sound_t *s = &core->sounds[source->data.sample.sound];
        s->users --;
        if (s->users <= 0 && s->state == BOX_AUDIO_SOUND_RELEASED) {
            s->users = 0;
            s->state = BOX_AUDIO_SOUND_FREE;
            s->data = NULL;
            box_audio_core_event(core, BOX_AUDIO_EVENT_SOUND_FREE, source->data.sample.sound, 0);
        }
    }

    source->kind = BOX_AUDIO_SOURCE_NONE;
}
