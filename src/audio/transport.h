/**
 * @file audio/transport.h
 * @brief How the main thread reaches the core: one small interface for every output.
 *
 * A transport sends command batches, hands over sounds, reads events and reports its state,
 * always on the main thread. The native transport runs the core on a miniaudio device, the web
 * transport in the AudioWorklet, the null transport on the main thread. See docs/audio.md.
 */

#ifndef BOX_AUDIO_TRANSPORT_H
#define BOX_AUDIO_TRANSPORT_H

#include "core/core.h"

/** Events the main thread keeps until read_events; the oldest are dropped beyond this */
#define BOX_AUDIO_TRANSPORT_EVENTS 4096

/** Bytes of a command batch the native transport sends to the audio thread at once; a larger
 * batch is split at command boundaries */
#define BOX_AUDIO_TRANSPORT_CHUNK 4096

/** Bytes of the ring of commands to the audio thread */
#define BOX_AUDIO_TRANSPORT_COMMAND_RING (64 * 1024)

/** Events the ring of events from the audio thread holds */
#define BOX_AUDIO_TRANSPORT_EVENT_RING 4096

/** Frames a transport renders at once into its buffers; a device may ask for any count */
#define BOX_AUDIO_TRANSPORT_FRAMES 512

/** Event of a transport, not of the core: the audio thread refused a sound, id is the sound.
 * The transport frees its frames and does not report it */
#define BOX_AUDIO_EVENT_SOUND_REFUSED 100

/** State of the audio output */
typedef enum box_audio_state_t {
    BOX_AUDIO_STATE_OFF = 0,        /**< No output: a world that runs no app, or --no-audio */
    BOX_AUDIO_STATE_NO_DEVICE = 1,  /**< No device: the null transport renders on the main
                                     * thread, headless runs and machines without audio */
    BOX_AUDIO_STATE_STARTING = 2,   /**< The output is not playing yet: on the web the worklet
                                     * loads or the page waits for a gesture */
    BOX_AUDIO_STATE_RUNNING = 3,    /**< A device plays */
    BOX_AUDIO_STATE_LOST = 4,       /**< The device disappeared; the main thread renders until
                                     * the default device reopens */
    BOX_AUDIO_STATE_FAILED = 5,     /**< The page could not start its audio */
    BOX_AUDIO_STATE_WAITING = 6,    /**< The page waits for a gesture to start its audio */
    BOX_AUDIO_STATE_SUSPENDED = 7   /**< The page is hidden, its audio faded out and suspended */
} box_audio_state_t;

/** What the core of a transport does, measured where it renders */
typedef struct box_audio_transport_stats_t {
    int32_t voices;  /**< Voices that play, without the ones that fade out */
    float load;      /**< Share of real time the render takes, smoothed, 0 when not measured */
    int32_t chunks;  /**< Chunks of streams the main thread holds for the core, 0 on the web,
                      * whose worklet holds them */
} box_audio_transport_stats_t;

/** Kind of a transport */
typedef enum box_audio_transport_kind_t {
    BOX_AUDIO_TRANSPORT_NULL = 0,    /**< The core runs on the main thread, no device */
    BOX_AUDIO_TRANSPORT_NATIVE = 1,  /**< A miniaudio device on macOS and Linux */
    BOX_AUDIO_TRANSPORT_WEB = 2      /**< The AudioWorklet of the page */
} box_audio_transport_kind_t;

/** Backends the native transport may open */
typedef enum box_audio_backend_t {
    BOX_AUDIO_BACKEND_DEFAULT = 0,     /**< The backends of miniaudio for the platform */
    BOX_AUDIO_BACKEND_UNAVAILABLE = 1  /**< A backend the platform does not have, so the open
                                        * fails on every machine; for the tests */
} box_audio_backend_t;

typedef struct box_audio_transport_t box_audio_transport_t;

/** A transport. The functions run on the main thread */
struct box_audio_transport_t {
    box_audio_transport_kind_t kind;  /**< What the transport is */
    float sample_rate;                /**< Frames per second of the core, 0 when not known */

    /** Send a batch of commands of the core; the bytes are copied.
     * @return Whether the transport took the batch */
    bool (*commands)(box_audio_transport_t *t, const void *data, int32_t size);

    /** Hand over a sound; the frames are copied and freed after its release.
     * @return Whether the transport took the sound */
    bool (*sound)(box_audio_transport_t *t, uint32_t id, const float *data, int32_t frames,
        int32_t channels, float sample_rate);

    /** Hand over a chunk of a stream that a STREAM command opened for a voice; the frames are
     * copied and freed once the core consumed them or the stream closed.
     * @return Whether the transport took the chunk */
    bool (*stream_chunk)(box_audio_transport_t *t, uint32_t stream, uint32_t voice,
        const float *data, int32_t frames, int32_t channels, bool last);

    /** Report what a stream does as far as the main thread knows, and free the chunks the
     * core is done with. A status of another voice than the last one opened is not current */
    void (*stream_status)(box_audio_transport_t *t, uint32_t stream,
        box_audio_stream_status_t *status);

    /** Give the core at least floats of memory for an effect slot, before the effect command
     * of a kind that needs it. The transport allocates it where the core runs and frees it
     * after the core; a slot that has as many floats keeps its memory and the call returns
     * true, a slot that needs more gets new memory.
     * @return Whether the memory went out */
    bool (*effect_memory)(box_audio_transport_t *t, uint32_t slot, int32_t floats);

    /** Take the events of the core, oldest first.
     * @return Number of events written */
    int32_t (*read_events)(box_audio_transport_t *t, box_audio_event_t *events, int32_t max);

    /** Report the state of the output */
    box_audio_state_t (*state)(box_audio_transport_t *t);

    /** Once per frame: flush commands, collect events, render the frames of delta seconds
     * when no device does, and reopen a lost device */
    void (*update)(box_audio_transport_t *t, float delta);

    /** Close the output and free the transport */
    void (*free)(box_audio_transport_t *t);

    /** Report the voices and the load of the core */
    void (*stats)(box_audio_transport_t *t, box_audio_transport_stats_t *stats);

    /** Report the clock of the core: the frame it renders now, as far as the main thread can
     * tell, which a PLAY, STOP or set with a when is scheduled against. It stops while the
     * output does not run and starts at 0 with a new core.
     * @return Frames of the core clock */
    uint64_t (*clock)(box_audio_transport_t *t);

    /** Start to record the next frames the core renders, into a buffer the transport owns.
     * @return Whether the capture started; false while another one records */
    bool (*capture)(box_audio_transport_t *t, int32_t frames);

    /** Take a finished capture: interleaved stereo frames, which the caller frees with
     * ecs_os_free. @return The frames, NULL while the capture records or when none runs */
    float* (*capture_take)(box_audio_transport_t *t, int32_t *frames);
};

/** Called by the null transport with every block it renders */
typedef void (*box_audio_output_t)(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx);

/** How to create a null transport */
typedef struct box_audio_null_desc_t {
    float sample_rate;          /**< Frames per second, 48000 when not positive */
    box_audio_output_t output;  /**< Receives the rendered frames, may be NULL */
    void *ctx;                  /**< Context of output */
} box_audio_null_desc_t;

/** How to create a native transport */
typedef struct box_audio_native_desc_t {
    box_audio_backend_t backend;  /**< Backends to try */
} box_audio_native_desc_t;

/** Create a null transport: the core on the main thread, update renders floor(time x rate)
 * frames in total, so fixed deltas give bit identical output.
 *
 * @param desc How to create it, may be NULL
 * @return The transport, NULL when the allocation failed
 */
box_audio_transport_t* box_audio_transport_null_new(
    const box_audio_null_desc_t *desc);

/** Open the default output device with miniaudio and start the core on its audio thread.
 *
 * No device is not an error: a failure logs one warning and returns NULL. NULL without a
 * warning on the web, which does not compile the device part.
 *
 * @param desc How to open it, may be NULL
 * @return The transport, NULL when there is no device
 */
box_audio_transport_t* box_audio_transport_native_new(
    const box_audio_native_desc_t *desc);

/** Create the transport of the AudioWorklet of the page. Starts the audio of the page
 * (box_audio_web_start) when it did not yet, so that what the first frame sends arrives.
 *
 * @return The transport, NULL on native builds
 */
box_audio_transport_t* box_audio_transport_web_new(void);

/** Free a transport.
 *
 * @param t The transport, may be NULL
 */
void box_audio_transport_free(
    box_audio_transport_t *t);

#endif
