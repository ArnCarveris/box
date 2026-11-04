/**
 * @file util.c
 * @brief Helpers of the audio core tests: offline renders, sounds and measurements.
 */

#include "util.h"

float audio_l[AUDIO_FRAMES];
float audio_r[AUDIO_FRAMES];
float audio_sound_a[AUDIO_FRAMES * 2];
float audio_sound_b[AUDIO_FRAMES * 2];

box_audio_core_t* audio_new(void) {
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);

    return core;
}

void audio_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, audio_l + at + i, audio_r + at + i, n);
    }
}

void audio_sine(
    float *data,
    int32_t frames,
    double frequency,
    float level)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        data[i] = level * (float)sin(2 * AUDIO_PI * frequency * i / AUDIO_RATE);
    }
}

void audio_fill(
    float *data,
    int32_t count,
    float value)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        data[i] = value;
    }
}

void audio_play(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play(&w, voice, sound, bus, volume, pan, pitch, flags, 0));
    box_audio_core_apply(core, w.data, w.size);
}

void audio_bus_set(
    box_audio_core_t *core,
    uint32_t bus,
    box_audio_param_t param,
    float value)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_bus_set(&w, bus, param, value, 0));
    box_audio_core_apply(core, w.data, w.size);
}

void audio_voice_set(
    box_audio_core_t *core,
    uint32_t voice,
    box_audio_param_t param,
    float value)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_set(&w, voice, param, value, 0));
    box_audio_core_apply(core, w.data, w.size);
}

void audio_order(
    box_audio_core_t *core,
    const box_audio_route_t *routes,
    int32_t count)
{
    uint8_t bytes[1024];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_order(&w, routes, count));
    box_audio_core_apply(core, w.data, w.size);
}

float audio_max_step(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t i;
    float result = 0;
    for (i = from; i < to; i ++) {
        float prev = i > 0 ? x[i - 1] : 0;
        float d = fabsf(x[i] - prev);
        result = d > result ? d : result;
    }

    return result;
}

float audio_peak(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t i;
    float result = 0;
    for (i = from; i < to; i ++) {
        float v = fabsf(x[i]);
        result = v > result ? v : result;
    }

    return result;
}

double audio_frequency(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t i, count = 0;
    double first = -1, last = -1, result = 0;
    for (i = from + 1; i < to; i ++) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            double at = i - 1 + x[i - 1] / (double)(x[i - 1] - x[i]);
            if (first < 0) {
                first = at;
            } else {
                count ++;
            }

            last = at;
        }
    }

    if (count && last > first) {
        result = count * (double)AUDIO_RATE / (last - first);
    }

    return result;
}

int32_t audio_events(
    box_audio_core_t *core,
    int32_t type,
    box_audio_event_t *last)
{
    box_audio_event_t events[64];
    int32_t n, i, count = 0;
    while ((n = box_audio_core_read_events(core, events, 64)) > 0) {
        for (i = 0; i < n; i ++) {
            if (events[i].type == type) {
                count ++;
                if (last) {
                    *last = events[i];
                }
            }
        }
    }

    return count;
}

void audio_workload(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames)
{
    static uint8_t bytes[4096];
    box_audio_route_t routes[] = {
        AUDIO_ROUTE(3, 1, 2), AUDIO_OUT(4, 2), AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0),
        AUDIO_MASTER
    };
    box_audio_event_t events[64];
    box_audio_writer_t w;
    int32_t at = 0, block = 0, i;
    audio_sine(audio_sound_a, 4800, 220, 0.6f);
    for (i = 0; i < 3000; i ++) {
        audio_sound_b[i * 2] = 0.5f * (float)sin(i * 0.013) * (float)sin(i * 0.0021);
        audio_sound_b[i * 2 + 1] = 0.3f * (float)cos(i * 0.029);
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 3000, 2, 44100));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_order(&w, routes, 5));
    test_assert(box_audio_write_bus_set(&w, 1, BOX_AUDIO_PARAM_SEND, 0.3f, 0));
    test_assert(box_audio_write_bus_set(&w, 3, BOX_AUDIO_PARAM_SEND, 0.7f, 0));
    test_assert(box_audio_write_bus_set(&w, 2, BOX_AUDIO_PARAM_PAN, -0.4f, 0));
    box_audio_core_apply(core, w.data, w.size);
    while (at < frames) {
        int32_t n = 37 + (block * 53) % 200;
        n = n < frames - at ? n : frames - at;
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 6; i ++) {
            uint32_t voice = (uint32_t)(block * 6 + i);
            box_audio_write_play(&w, voice, 1 + (voice % 2), 1 + voice % 4,
                0.05f + 0.01f * (float)(voice % 7), -1 + 0.13f * (float)(voice % 16),
                0.5f + 0.1f * (float)(voice % 13), voice % 3 ? BOX_AUDIO_PLAY_LOOP : 0, 0);
        }

        box_audio_write_voice_set(&w, (uint32_t)(block * 6 - 5), BOX_AUDIO_PARAM_PITCH, 1.3f, 0);
        box_audio_write_voice_set(&w, (uint32_t)(block * 6 - 4), BOX_AUDIO_PARAM_PAN, 0.8f, 0);
        box_audio_write_stop(&w, (uint32_t)(block * 6 - 9), 0);
        box_audio_write_bus_set(&w, 1 + (uint32_t)block % 4, BOX_AUDIO_PARAM_VOLUME,
            0.5f + 0.1f * (float)(block % 6), 0);
        box_audio_write_bus_set(&w, 4, BOX_AUDIO_PARAM_MUTE, (float)(block % 2), 0);
        box_audio_core_apply(core, w.data, w.size);
        box_audio_core_render(core, left + at, right + at, n);
        while (box_audio_core_read_events(core, events, 64) > 0) {
        }

        at += n;
        block ++;
    }
}

uint64_t audio_hash(
    const float *x,
    int32_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t bits;
    int32_t i, b;
    for (i = 0; i < n; i ++) {
        memcpy(&bits, &x[i], sizeof(bits));
        for (b = 0; b < 4; b ++) {
            h ^= (bits >> (8 * b)) & 0xFF;
            h *= 0x100000001B3ull;
        }
    }

    return h;
}
