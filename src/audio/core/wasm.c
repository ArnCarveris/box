/**
 * @file audio/core/wasm.c
 * @brief Exports of the standalone wasm of the core, which the web runs in an AudioWorklet.
 *
 * Built only by tools/audio_core.py with BOX_AUDIO_CORE_WASM; the main module compiles it to
 * nothing. The worklet calls render once per quantum and copies the static buffers out.
 */

#ifdef BOX_AUDIO_CORE_WASM

#include "core.h"

/** The core of the worklet */
static box_audio_core_t box_audio_core_wasm;

/** Left channel of the last render */
static float box_audio_core_wasm_l[BOX_AUDIO_CORE_QUANTUM];

/** Right channel of the last render */
static float box_audio_core_wasm_r[BOX_AUDIO_CORE_QUANTUM];

/** Start the core at the sample rate of the AudioContext.
 *
 * @param sample_rate Frames per second
 */
void box_audio_core_wasm_init(
    float sample_rate)
{
    box_audio_core_init(&box_audio_core_wasm, sample_rate);
}

/** Render a quantum into the channel buffers.
 *
 * @param frames Frames to render, at most BOX_AUDIO_CORE_QUANTUM
 */
void box_audio_core_wasm_render(
    int32_t frames)
{
    if (frames > BOX_AUDIO_CORE_QUANTUM) {
        frames = BOX_AUDIO_CORE_QUANTUM;
    }

    box_audio_core_render(&box_audio_core_wasm, box_audio_core_wasm_l, box_audio_core_wasm_r,
        frames);
}

/** Get the left channel buffer.
 *
 * @return Its address in the memory of the core
 */
float* box_audio_core_wasm_left(void)
{
    return box_audio_core_wasm_l;
}

/** Get the right channel buffer.
 *
 * @return Its address in the memory of the core
 */
float* box_audio_core_wasm_right(void)
{
    return box_audio_core_wasm_r;
}

/** Set the frequency of the tone.
 *
 * @param frequency Frequency in Hz
 */
void box_audio_core_wasm_set_frequency(
    float frequency)
{
    box_audio_core_set_frequency(&box_audio_core_wasm, frequency);
}

/** Set the peak level of the tone.
 *
 * @param level Peak level, 0..1
 */
void box_audio_core_wasm_set_level(
    float level)
{
    box_audio_core_set_level(&box_audio_core_wasm, level);
}

#else

/** Keeps the translation unit of the main module from being empty */
typedef int box_audio_core_wasm_unused_t;

#endif
