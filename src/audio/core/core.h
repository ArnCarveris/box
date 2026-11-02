/**
 * @file audio/core/core.h
 * @brief The audio core: renders the sound of the app into float buffers, one block at a time.
 *
 * Plain C without flecs, box or platform code, so the same sources build into the main module
 * and into the standalone wasm the web build runs in an AudioWorklet. Rendering does not
 * allocate, lock or log. For now the core plays one sine tone.
 */

#ifndef BOX_AUDIO_CORE_H
#define BOX_AUDIO_CORE_H

#include <stdint.h>

/** Frames the web renders per call, the render quantum of an AudioWorklet */
#define BOX_AUDIO_CORE_QUANTUM 128

/** Seconds a level change takes to reach its target, so changes do not click */
#define BOX_AUDIO_CORE_RAMP 0.005f

/** State of an audio core, owned by the caller */
typedef struct box_audio_core_t {
    float sample_rate; /**< Frames per second of the output */
    float frequency;   /**< Frequency of the tone in Hz */
    float level;       /**< Target peak level of the tone, 0..1 */
    float gain;        /**< Current peak level, moving toward level */
    float ramp;        /**< Largest change of gain per frame */
    double phase;      /**< Phase of the tone in turns, 0..1 */
    double step;       /**< Phase advance per frame in turns */
} box_audio_core_t;

/** Start a core at a sample rate, silent until a level is set.
 *
 * @param core The core
 * @param sample_rate Frames per second of the output, as the platform reports it
 */
void box_audio_core_init(
    box_audio_core_t *core,
    float sample_rate);

/** Set the frequency of the tone. The phase continues, so the change does not click.
 *
 * @param core The core
 * @param frequency Frequency in Hz, clamped to 0..half the sample rate
 */
void box_audio_core_set_frequency(
    box_audio_core_t *core,
    float frequency);

/** Set the peak level of the tone, reached over BOX_AUDIO_CORE_RAMP.
 *
 * @param core The core
 * @param level Peak level, clamped to 0..1
 */
void box_audio_core_set_level(
    box_audio_core_t *core,
    float level);

/** Render frames of the tone into the left and right channel.
 *
 * @param core The core
 * @param left Receives frames samples of the left channel
 * @param right Receives frames samples of the right channel
 * @param frames Number of frames
 */
void box_audio_core_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames);

/** Sine of a phase in turns, without libm, so the wasm of the core needs no imports.
 *
 * @param turns The phase in turns, one turn is 2 pi
 * @return The sine, within 1e-6 of sin(2 pi turns)
 */
float box_audio_core_sin(
    double turns);

#endif
