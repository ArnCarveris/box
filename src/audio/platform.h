/**
 * @file audio/platform.h
 * @brief The browser audio output: an AudioWorklet that runs the core from its own wasm.
 */

#ifndef BOX_AUDIO_PLATFORM_H
#define BOX_AUDIO_PLATFORM_H

#include <stdbool.h>
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

/** Send a chunk of a stream to the worklet as a transferred ArrayBuffer; the worklet copies it
 * into the heap of its core and frees it there once the core consumed it. Nothing on native
 * builds.
 *
 * @param stream Index of the stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param last Whether the stream ends with this chunk
 */
void box_audio_web_chunk(
    uint32_t stream,
    const float *data,
    int32_t frames,
    int32_t channels,
    bool last);

/** Start to fetch a file relative to the page into the memory of the page, with fetch.
 * Returns 0 on native builds.
 *
 * @param path The path, relative to the page
 * @return Id of the fetch, 0 when it could not start
 */
int32_t box_audio_web_fetch(
    const char *path);

/** Check a fetch. Once it is done the bytes wait in the page for box_audio_web_fetch_take.
 *
 * @param id Id of the fetch
 * @param size Receives the bytes of the file once it arrived
 * @param status Receives the HTTP status, 0 when the request failed without one
 * @return 0 while it runs, 1 when the file arrived, -1 when it failed
 */
int32_t box_audio_web_fetch_poll(
    int32_t id,
    int32_t *size,
    int32_t *status);

/** Copy the bytes of a fetch that arrived and forget it.
 *
 * @param id Id of the fetch
 * @param dst Receives the bytes, the size box_audio_web_fetch_poll reported
 */
void box_audio_web_fetch_take(
    int32_t id,
    void *dst);

/** Forget a fetch, done or not; its bytes are dropped when it arrives.
 *
 * @param id Id of the fetch
 */
void box_audio_web_fetch_cancel(
    int32_t id);
/** Ask the worklet to allocate the delay memory of a reverb slot in the heap of its core, which
 * keeps it as long as the page. Nothing on native builds.
 *
 * @param slot Slot of the reverb
 */
void box_audio_web_reverb(
    uint32_t slot);

#endif
