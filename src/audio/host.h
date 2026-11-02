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
    box_audio_event_t *events;           /**< Events not read yet, a ring */
    int32_t event_first;                 /**< Oldest event */
    int32_t event_count;                 /**< Events kept */
    double clock;                        /**< Seconds of the frame deltas so far */
    int64_t rendered;                    /**< Frames the main thread rendered for the deltas */
} box_audio_host_t;

/** Allocate the event queue of a host.
 *
 * @param host The host, zeroed
 * @return 0 on success, nonzero when the allocation failed
 */
int box_audio_host_init(
    box_audio_host_t *host);

/** Free the sounds and the events of a host, once no core uses the sounds.
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
