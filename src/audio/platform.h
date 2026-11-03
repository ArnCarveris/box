/**
 * @file audio/platform.h
 * @brief The browser audio output: an AudioWorklet that runs the core from its own wasm.
 */

#ifndef BOX_AUDIO_PLATFORM_H
#define BOX_AUDIO_PLATFORM_H

#include <stdint.h>

/** Create the AudioContext of the page and load the worklet of the core, once per page.
 *
 * The context stays suspended until box_audio_web_gesture. Nothing on native builds.
 */
void box_audio_web_start(void);

/** Resume the AudioContext from an input event handler, a user gesture to the browser. Cheap
 * once the context runs; nothing on native builds.
 */
void box_audio_web_gesture(void);

/** Send a batch of commands of the core (core/core.h) to the worklet, which applies it
 * between two quanta. The bytes are copied. Nothing on native builds.
 *
 * @param commands The bytes
 * @param size Number of bytes
 */
void box_audio_web_apply(
    const void *commands,
    int32_t size);

/** Send a sound to the worklet, which copies it into the heap of its core and frees it there
 * once a release made it unused. The frames are copied. Nothing on native builds.
 *
 * @param id Id of the sound
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound
 */
void box_audio_web_sound(
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate);

/** Ask the worklet to allocate the delay memory of a reverb slot in the heap of its core, which
 * keeps it as long as the page. Nothing on native builds.
 *
 * @param slot Slot of the reverb
 */
void box_audio_web_reverb(
    uint32_t slot);

#endif
