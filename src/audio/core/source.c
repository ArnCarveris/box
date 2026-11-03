/**
 * @file audio/core/source.c
 * @brief Sources of voices: a sample buffer, a synth patch (synth.c) or the ring of chunks of
 * a stream. A new kind adds a case to each function.
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

void box_audio_source_stream(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t stream)
{
    box_audio_stream_t *s = &core->streams[stream];
    box_audio_stream_source_t *src = &source->data.stream;
    int32_t fade = (int32_t)(BOX_AUDIO_STREAM_FADE * core->sample_rate);
    fade = fade > 0 ? fade : 1;
    source->kind = BOX_AUDIO_SOURCE_STREAM;
    src->stream = stream;
    src->frac = 0;
    src->step = s->rate;
    src->a[0] = src->a[1] = 0;
    src->b[0] = src->b[1] = 0;
    src->gain = 1;
    src->fade_step = 1.0f / (float)fade;
    src->need = (int32_t)((double)fade * s->rate) + 2;
    src->phase = BOX_AUDIO_STREAM_PRIMING;
    src->drained = false;
}

/** Take the next frame of a stream. A chunk read to its end leaves the ring and counts as
 * consumed, so its memory is free for the owner.
 *
 * @param s The stream
 * @param frame Receives the frame, the left channel twice when mono
 * @return Whether the ring held a frame
 */
static bool box_audio_stream_pop(
    box_audio_stream_t *s,
    float *frame)
{
    const box_audio_chunk_t *c;
    const float *p;
    bool result = s->count > 0;
    if (!result) {
        goto done;
    }

    c = &s->chunks[s->head];
    p = c->data + (int64_t)s->at * s->channels;
    frame[0] = box_audio_clean(p[0]);
    frame[1] = s->channels == 2 ? box_audio_clean(p[1]) : frame[0];
    s->at ++;
    s->avail --;
    if (s->at >= c->frames) {
        s->chunks[s->head].data = NULL;
        s->head = (s->head + 1) % BOX_AUDIO_STREAM_CHUNKS;
        s->count --;
        s->at = 0;
        s->consumed ++;
    }

done:
    return result;
}

/** Move a stream source one frame of the stream on: b becomes a and the ring gives the next b.
 * At the end of the last chunk b repeats a once, then the source ends.
 *
 * @param s The stream
 * @param src The source
 */
static void box_audio_stream_advance(
    box_audio_stream_t *s,
    box_audio_stream_source_t *src)
{
    src->a[0] = src->b[0];
    src->a[1] = src->b[1];
    if (src->drained) {
        src->phase = BOX_AUDIO_STREAM_ENDED;
    } else if (!box_audio_stream_pop(s, src->b)) {
        if (s->last) {
            src->drained = true;
        }
    }
}

/** Render a block of a stream source. Never waits for a chunk: when the ring holds fewer
 * frames than a fade takes, the gain fades to 0 over BOX_AUDIO_STREAM_FADE and the source plays
 * silence until enough frames arrived, then it fades back in; frames are not skipped.
 *
 * @param core The core
 * @param src The source
 * @param frames Frames to render
 * @param stereo Set to whether the stream is stereo
 * @return Frames it rendered, fewer once the last chunk was read to its end
 */
static int32_t box_audio_stream_render(
    box_audio_core_t *core,
    box_audio_stream_source_t *src,
    int32_t frames,
    bool *stereo)
{
    box_audio_stream_t *s = &core->streams[src->stream];
    float *l = core->src_l, *r = core->src_r;
    int32_t i;
    *stereo = s->channels == 2;
    for (i = 0; i < frames; i ++) {
        float frac, gain;
        if (src->phase == BOX_AUDIO_STREAM_ENDED) {
            break;
        }

        if (src->phase == BOX_AUDIO_STREAM_PRIMING || src->phase == BOX_AUDIO_STREAM_STARVED) {
            if (s->avail < src->need && !(s->last && s->avail > 0)) {
                l[i] = 0;
                r[i] = 0;
                continue;
            }

            if (src->phase == BOX_AUDIO_STREAM_PRIMING) {
                box_audio_stream_pop(s, src->a);
                if (!box_audio_stream_pop(s, src->b)) {
                    src->b[0] = src->a[0];
                    src->b[1] = src->a[1];
                    src->drained = s->last;
                }

                src->phase = BOX_AUDIO_STREAM_PLAYING;
                src->gain = 1;
            } else {
                src->phase = BOX_AUDIO_STREAM_RECOVERING;
            }
        }

        if (src->phase == BOX_AUDIO_STREAM_PLAYING && !s->last && s->avail < src->need) {
            src->phase = BOX_AUDIO_STREAM_FADING;
            s->underruns ++;
            s->starved = true;
        }

        if (src->phase == BOX_AUDIO_STREAM_FADING) {
            src->gain -= src->fade_step;
            if (src->gain <= 0) {
                src->gain = 0;
                src->phase = BOX_AUDIO_STREAM_STARVED;
            }
        } else if (src->phase == BOX_AUDIO_STREAM_RECOVERING) {
            src->gain += src->fade_step;
            if (src->gain >= 1) {
                src->gain = 1;
                src->phase = BOX_AUDIO_STREAM_PLAYING;
                s->starved = false;
            }
        }

        frac = (float)src->frac;
        gain = src->gain;
        l[i] = (src->a[0] + (src->b[0] - src->a[0]) * frac) * gain;
        r[i] = (src->a[1] + (src->b[1] - src->a[1]) * frac) * gain;
        if (src->phase == BOX_AUDIO_STREAM_STARVED) {
            continue;
        }

        src->frac += src->step;
        while (src->frac >= 1 && src->phase != BOX_AUDIO_STREAM_ENDED) {
            src->frac -= 1;
            box_audio_stream_advance(s, src);
        }
    }

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
    } else if (source->kind == BOX_AUDIO_SOURCE_STREAM) {
        done = box_audio_stream_render(core, &source->data.stream, frames, stereo);
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
    } else if (source->kind == BOX_AUDIO_SOURCE_STREAM) {
        box_audio_stream_t *s = &core->streams[source->data.stream.stream];
        int32_t i;
        s->ended = source->data.stream.phase == BOX_AUDIO_STREAM_ENDED;
        s->open = false;
        s->last = false;
        s->starved = false;
        for (i = 0; i < BOX_AUDIO_STREAM_CHUNKS; i ++) {
            s->chunks[i].data = NULL;
        }

        s->head = 0;
        s->count = 0;
        s->at = 0;
        s->avail = 0;
    }

    source->kind = BOX_AUDIO_SOURCE_NONE;
}
