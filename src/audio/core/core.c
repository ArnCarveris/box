/**
 * @file audio/core/core.c
 * @brief The audio core: a sine tone rendered from a phase accumulator.
 */

#include "core.h"

/** Pi, for the sine approximation */
#define BOX_AUDIO_CORE_PI 3.14159265358979323846

void box_audio_core_init(
    box_audio_core_t *core,
    float sample_rate)
{
    core->sample_rate = sample_rate > 0 ? sample_rate : 48000.0f;
    core->frequency = 0;
    core->level = 0;
    core->gain = 0;
    core->ramp = 1.0f / (BOX_AUDIO_CORE_RAMP * core->sample_rate);
    core->phase = 0;
    core->step = 0;
}

void box_audio_core_set_frequency(
    box_audio_core_t *core,
    float frequency)
{
    float nyquist = core->sample_rate * 0.5f;
    if (!(frequency > 0)) {
        frequency = 0;
    } else if (frequency > nyquist) {
        frequency = nyquist;
    }

    core->frequency = frequency;
    core->step = (double)frequency / (double)core->sample_rate;
}

void box_audio_core_set_level(
    box_audio_core_t *core,
    float level)
{
    if (!(level > 0)) {
        level = 0;
    } else if (level > 1) {
        level = 1;
    }

    core->level = level;
}

float box_audio_core_sin(
    double turns)
{
    double x, x2;
    turns -= (double)(int64_t)turns;
    if (turns < 0) {
        turns += 1;
    }

    if (turns >= 0.5) {
        turns -= 1;
    }

    x = turns * 2 * BOX_AUDIO_CORE_PI;
    if (x > BOX_AUDIO_CORE_PI * 0.5) {
        x = BOX_AUDIO_CORE_PI - x;
    } else if (x < -BOX_AUDIO_CORE_PI * 0.5) {
        x = -BOX_AUDIO_CORE_PI - x;
    }

    x2 = x * x;

    return (float)(x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 *
        (1 - x2 / 110 * (1 - x2 / 156)))))));
}

void box_audio_core_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames)
{
    int32_t i;
    float gain = core->gain, target = core->level, ramp = core->ramp;
    double phase = core->phase, step = core->step;
    for (i = 0; i < frames; i ++) {
        float sample;
        if (gain < target) {
            gain = gain + ramp < target ? gain + ramp : target;
        } else if (gain > target) {
            gain = gain - ramp > target ? gain - ramp : target;
        }

        sample = gain > 0 ? gain * box_audio_core_sin(phase) : 0;
        left[i] = sample;
        right[i] = sample;
        phase += step;
        if (phase >= 1) {
            phase -= 1;
        }
    }

    core->gain = gain;
    core->phase = phase;
}
