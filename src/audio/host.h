/**
 * @file audio/host.h
 * @brief The main thread side of a transport: the frames of sounds and the events it keeps.
 */

#ifndef BOX_AUDIO_HOST_H
#define BOX_AUDIO_HOST_H

#include "transport.h"

/** What the main thread owns for a core that may run elsewhere */
typedef struct box_audio_host_t {
    float *sounds[BOX_AUDIO_SOUND_MAX];  /**< Frames of the sounds, freed on SOUND_FREE */
    float *reverbs[BOX_AUDIO_REVERB_MAX]; /**< Delay memory of the reverbs, freed at the end */
    box_audio_event_t *events;           /**< Events not read yet, a ring */
    int32_t event_first;                 /**< Oldest event */
    int32_t event_count;                 /**< Events kept */
    double clock;                        /**< Seconds of the frame deltas so far */
    int64_t rendered;                    /**< Frames the main thread rendered for the deltas */
} box_audio_host_t;

/** A capture of the frames a core renders, written on the thread that renders */
typedef struct box_audio_capture_t {
    float *data;     /**< Interleaved stereo frames, NULL when no capture records */
    int32_t frames;  /**< Frames to record */
    int32_t at;      /**< Frames recorded */
} box_audio_capture_t;

/** Weight of a new measurement in the smoothed load */
#define BOX_AUDIO_LOAD_SMOOTHING (0.1f)

/** Copy rendered frames into a capture, as far as it has room. Does not allocate.
 *
 * @param capture The capture
 * @param left Left channel
 * @param right Right channel
 * @param frames Number of frames
 * @return Whether the capture is complete
 */
bool box_audio_capture_write(
    box_audio_capture_t *capture,
    const float *left,
    const float *right,
    int32_t frames);

/** Smooth the load of a render: the seconds it took over the seconds it rendered.
 *
 * @param load The smoothed load so far
 * @param seconds Seconds the render took
 * @param frames Frames it rendered
 * @param sample_rate Frames per second
 * @return The new smoothed load
 */
float box_audio_load_update(
    float load,
    double seconds,
    int32_t frames,
    float sample_rate);

/** Allocate the event queue of a host.
 *
 * @param host The host, zeroed
 * @return 0 on success, nonzero when the allocation failed
 */
int box_audio_host_init(
    box_audio_host_t *host);

/** Free the sounds, the reverb memory and the events of a host, once no core uses them.
 *
 * @param host The host
 */
void box_audio_host_fini(
    box_audio_host_t *host);

/** Copy the frames of a sound that is not in use.
 *
 * @param host The host
 * @param id Id of the sound
 * @param data The frames
 * @param frames Number of frames
 * @param channels 1 or 2
 * @return The copy, owned by the host, NULL when the sound is in use or invalid
 */
float* box_audio_host_sound(
    box_audio_host_t *host,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels);

/** Allocate the cleared delay memory of a reverb slot that has none.
 *
 * @param host The host
 * @param slot Slot of the reverb
 * @param sample_rate Frames per second of the core
 * @param floats Receives the floats of the memory
 * @return The memory, owned by the host, NULL when the slot has memory or is invalid
 */
float* box_audio_host_reverb(
    box_audio_host_t *host,
    uint32_t slot,
    float sample_rate,
    int32_t *floats);

/** Free the frames of a sound.
 *
 * @param host The host
 * @param id Id of the sound
 */
void box_audio_host_sound_free(
    box_audio_host_t *host,
    uint32_t id);

/** Take an event of the core: free the frames a SOUND_FREE or a refusal names and keep the
 * events the transport reports.
 *
 * @param host The host
 * @param event The event
 */
void box_audio_host_event(
    box_audio_host_t *host,
    const box_audio_event_t *event);

/** Take the kept events, oldest first.
 *
 * @param host The host
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
int32_t box_audio_host_read(
    box_audio_host_t *host,
    box_audio_event_t *events,
    int32_t max);

/** Advance the clock of the host by a frame delta.
 *
 * @param host The host
 * @param delta Seconds of the frame
 * @param sample_rate Frames per second
 * @return Frames to render for the delta, at most one second
 */
int32_t box_audio_host_frames(
    box_audio_host_t *host,
    float delta,
    float sample_rate);

#endif
