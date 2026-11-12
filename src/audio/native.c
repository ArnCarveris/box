/**
 * @file audio/native.c
 * @brief The native output: the core on the audio thread of a miniaudio device.
 *
 * Only the device API of miniaudio is used: its engine, node graph, decoders and resampling
 * for playback are compiled out or not called. The main thread and the device callback talk
 * over two lock-free rings (ring.h), commands and sounds one way, events the other.
 *
 * The web does not compile this file's device part; see docs/audio.md for the link flags of
 * notarized macOS builds.
 */

#include "host.h"
#include "ring.h"
#include <flecs.h>
#include <string.h>

#ifndef __EMSCRIPTEN__

#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_NULL
#define MINIAUDIO_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wbad-function-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#ifdef _WIN32
#define AlphaBlend box_win32_AlphaBlend
#endif
#include <miniaudio.h>
#ifdef _WIN32
#undef AlphaBlend
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/** Seconds between two attempts to reopen a lost device */
#define BOX_AUDIO_NATIVE_RETRY 2.0f

/** Kinds of the messages of the command ring */
typedef enum box_audio_msg_kind_t {
    BOX_AUDIO_MSG_COMMANDS = 1,  /**< A batch of commands of the core */
    BOX_AUDIO_MSG_SOUND = 2,     /**< A box_audio_msg_sound_t */
    BOX_AUDIO_MSG_CAPTURE = 3,   /**< A box_audio_capture_t to record into */
    BOX_AUDIO_MSG_EFFECT = 4,    /**< A box_audio_msg_effect_t */
    BOX_AUDIO_MSG_CHUNK = 5      /**< A box_audio_msg_chunk_t */
} box_audio_msg_kind_t;

/** Header of a message of the command ring, followed by size bytes */
typedef struct box_audio_msg_t {
    uint32_t kind;  /**< A box_audio_msg_kind_t */
    uint32_t size;  /**< Bytes after the header, at most BOX_AUDIO_TRANSPORT_CHUNK */
} box_audio_msg_t;

/** A sound handed to the audio thread; the frames stay with the main thread */
typedef struct box_audio_msg_sound_t {
    const float *data;   /**< Frames, interleaved when stereo */
    uint32_t id;         /**< Id of the sound */
    int32_t frames;      /**< Number of frames */
    int32_t channels;    /**< 1 or 2 */
    float sample_rate;   /**< Frames per second of the sound */
} box_audio_msg_sound_t;

/** A chunk of a stream handed to the audio thread; the frames stay with the main thread */
typedef struct box_audio_msg_chunk_t {
    const float *data;   /**< Frames, interleaved when stereo */
    uint32_t stream;     /**< Index of the stream */
    int32_t frames;      /**< Number of frames */
    int32_t last;        /**< 1 when the stream ends with it */
} box_audio_msg_chunk_t;

/** The status of a stream as the audio thread publishes it after each render. head packs the
 * voice, open and the consumed count into one word, so the main thread frees chunks by a
 * consistent view; the other fields only inform */
typedef struct box_audio_native_stream_t {
    atomic_uint_least64_t head;  /**< voice << 32 | open << 31 | ended << 30 | consumed */
    atomic_int at;               /**< Frames read of the oldest chunk not consumed */
    atomic_int underruns;        /**< Underruns since the stream opened */
    atomic_int starved;          /**< 1 while it plays silence */
} box_audio_native_stream_t;
/** The levels of a bus as the audio thread publishes them after each callback, accumulated
 * until the main thread takes them.
 *
 * Each word is consistent on its own: a peak is raised to the largest, a sum of squares added
 * to and the frames counted up, and the main thread swaps each with 0 */
typedef struct box_audio_native_meter_t {
    atomic_uint peak_left;                 /**< Bits of the left peak, a float */
    atomic_uint peak_right;                /**< Bits of the right peak, a float */
    atomic_uint_least64_t square_left;     /**< Bits of the left sum of squares, a double */
    atomic_uint_least64_t square_right;    /**< Bits of the right sum of squares, a double */
    atomic_uint frames;                    /**< Frames of the sums */
    atomic_uint_least64_t loudness;        /**< Bits of the K-weighted sum of squares, a
                                            *   double */
} box_audio_native_meter_t;

/** The memory of an effect slot handed to the audio thread; it stays with the main thread */
typedef struct box_audio_msg_effect_t {
    float *data;         /**< The memory, cleared */
    uint32_t slot;       /**< Slot of the effect */
    int32_t floats;      /**< Floats of the memory */
} box_audio_msg_effect_t;

/** The native transport */
typedef struct box_audio_native_t {
    box_audio_transport_t base;     /**< The interface, first */
    box_audio_host_t host;          /**< Sounds and events, main thread */
    ma_context context;             /**< The miniaudio context */
    ma_device device;               /**< The output device, while device_open */
    bool device_open;               /**< Whether device is initialized */
    box_audio_core_t *core;         /**< The core, used by the audio thread while it runs */
    box_audio_ring_t commands;      /**< Messages to the audio thread */
    box_audio_ring_t events;        /**< Events from the audio thread */
    uint8_t *pending;               /**< Messages the command ring had no room for, main thread */
    int32_t pending_size;           /**< Bytes in pending */
    int32_t pending_capacity;       /**< Bytes pending holds */
    atomic_int lost;                /**< Set when the device stopped without being asked */
    atomic_int stopping;            /**< Set while the main thread stops the device */
    box_audio_state_t state;        /**< RUNNING or LOST */
    float retry;                    /**< Seconds until the next attempt to reopen */
    bool warned;                    /**< Whether the loss of the device was logged */
    box_audio_capture_t capture;    /**< The capture the audio thread records into */
    float *capture_data;            /**< Frames of the capture that records, main thread */
    int32_t capture_frames;         /**< Frames of that capture, main thread */
    atomic_int captured;            /**< Set by the audio thread when the capture is complete */
    atomic_int voices;              /**< Voices of the core after the last render */
    atomic_uint load;               /**< Bits of the smoothed load of the renders, a float */
    atomic_uint_least64_t clock;    /**< Clock of the core after the last render */
    box_audio_native_stream_t streams[BOX_AUDIO_STREAM_MAX]; /**< Status of the streams */
    box_audio_native_meter_t meters[BOX_AUDIO_BUS_MAX]; /**< Levels of the buses */
    atomic_uint reduction[BOX_AUDIO_EFFECT_MAX]; /**< Bits of the largest reduction of each
                                    *   effect, a float */
    box_audio_meter_t meter_audio[BOX_AUDIO_BUS_MAX]; /**< Meters read from the core, audio
                                    *   thread */
    box_audio_effect_meter_t effect_audio[BOX_AUDIO_EFFECT_MAX]; /**< Effect meters read from
                                    *   the core, audio thread */
    float load_audio;               /**< The smoothed load, audio thread */
    uint8_t message[BOX_AUDIO_TRANSPORT_CHUNK];   /**< A message the audio thread took */
    float left[BOX_AUDIO_TRANSPORT_FRAMES];       /**< Rendered left channel */
    float right[BOX_AUDIO_TRANSPORT_FRAMES];      /**< Rendered right channel */
} box_audio_native_t;

/** Take the messages of the command ring and apply them. Runs on the consumer of the ring.
 * A sound waits in the ring while the event ring has no room for its refusal.
 *
 * @param n The transport
 */
static void box_audio_native_receive(
    box_audio_native_t *n)
{
    box_audio_msg_t msg;
    while (box_audio_ring_peek(&n->commands, &msg, (int32_t)sizeof(msg))) {
        if (msg.kind == BOX_AUDIO_MSG_SOUND &&
            box_audio_ring_room(&n->events) < (int32_t)sizeof(box_audio_event_t))
        {
            break;
        }

        box_audio_ring_read(&n->commands, NULL, (int32_t)sizeof(msg));
        if (msg.size > BOX_AUDIO_TRANSPORT_CHUNK ||
            !box_audio_ring_read(&n->commands, n->message, (int32_t)msg.size))
        {
            break;
        }

        if (msg.kind == BOX_AUDIO_MSG_COMMANDS) {
            box_audio_core_apply(n->core, n->message, (int32_t)msg.size);
        } else if (msg.kind == BOX_AUDIO_MSG_CHUNK && msg.size == sizeof(box_audio_msg_chunk_t)) {
            box_audio_msg_chunk_t chunk;
            memcpy(&chunk, n->message, sizeof(chunk));
            box_audio_core_stream_chunk(n->core, chunk.stream, chunk.data, chunk.frames,
                chunk.last != 0);
        } else if (msg.kind == BOX_AUDIO_MSG_CAPTURE && msg.size == sizeof(box_audio_capture_t)) {
            memcpy(&n->capture, n->message, sizeof(n->capture));
        } else if (msg.kind == BOX_AUDIO_MSG_EFFECT && msg.size == sizeof(box_audio_msg_effect_t)) {
            box_audio_msg_effect_t effect;
            memcpy(&effect, n->message, sizeof(effect));
            box_audio_core_effect_memory(n->core, effect.slot, effect.data, effect.floats);
        } else if (msg.kind == BOX_AUDIO_MSG_SOUND && msg.size == sizeof(box_audio_msg_sound_t)) {
            box_audio_msg_sound_t sound;
            memcpy(&sound, n->message, sizeof(sound));
            if (!box_audio_core_sound(n->core, sound.id, sound.data, sound.frames,
                sound.channels, sound.sample_rate))
            {
                box_audio_event_t refused = { BOX_AUDIO_EVENT_SOUND_REFUSED, sound.id, 0 };
                box_audio_ring_write(&n->events, &refused, (int32_t)sizeof(refused), NULL, 0);
            }
        }
    }
}

/** Move the events of the core into the event ring, as many as it has room for; the others
 * wait in the core.
 *
 * @param n The transport
 */
static void box_audio_native_send_events(
    box_audio_native_t *n)
{
    box_audio_event_t events[32];
    int32_t room, count, max;
    do {
        room = box_audio_ring_room(&n->events) / (int32_t)sizeof(box_audio_event_t);
        max = room < 32 ? room : 32;
        count = max > 0 ? box_audio_core_read_events(n->core, events, max) : 0;
        if (count) {
            box_audio_ring_write(&n->events, events,
                count * (int32_t)sizeof(box_audio_event_t), NULL, 0);
        }
    } while (count && count == max);
}

/** Raise a word that holds the bits of a float that is not negative, whose bits order as the
 * values do, to a value. Does not lock.
 *
 * @param word The word
 * @param value The value, not negative
 */
static void box_audio_native_raise(
    atomic_uint *word,
    float value)
{
    unsigned int old = atomic_load(word), bits;
    memcpy(&bits, &value, sizeof(bits));
    while (bits > old && !atomic_compare_exchange_weak(word, &old, bits)) { }
}

/** Add a value to a word that holds the bits of a double. Does not lock.
 *
 * @param word The word
 * @param value The value
 */
static void box_audio_native_add(
    atomic_uint_least64_t *word,
    double value)
{
    uint_least64_t old = atomic_load(word), bits;
    double sum;
    do {
        memcpy(&sum, &old, sizeof(sum));
        sum += value;
        memcpy(&bits, &sum, sizeof(bits));
    } while (!atomic_compare_exchange_weak(word, &old, bits));
}

/** Take the meters of the core and add them to the words the main thread takes them from.
 *
 * @param n The transport
 */
static void box_audio_native_publish(
    box_audio_native_t *n)
{
    int32_t i;
    box_audio_core_meters(n->core, n->meter_audio, BOX_AUDIO_BUS_MAX);
    box_audio_core_effect_meters(n->core, n->effect_audio, BOX_AUDIO_EFFECT_MAX);
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        const box_audio_meter_t *m = &n->meter_audio[i];
        box_audio_native_meter_t *nm = &n->meters[i];
        if (!m->frames) {
            continue;
        }

        box_audio_native_raise(&nm->peak_left, m->peak_left);
        box_audio_native_raise(&nm->peak_right, m->peak_right);
        box_audio_native_add(&nm->square_left, m->square_left);
        box_audio_native_add(&nm->square_right, m->square_right);
        if (m->loudness > 0) {
            box_audio_native_add(&nm->loudness, (double)m->loudness);
        }

        atomic_fetch_add(&nm->frames, m->frames);
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        if (n->effect_audio[i].reduction > 0) {
            box_audio_native_raise(&n->reduction[i], n->effect_audio[i].reduction);
        }
    }
}

/** Apply the commands, render frames and send the events: the work of the device callback,
 * and of the main thread while the device is lost. Does not allocate, lock, log or wait.
 *
 * @param n The transport
 * @param out Receives interleaved stereo frames, NULL to drop them
 * @param frames Number of frames, any count
 */
static void box_audio_native_process(
    box_audio_native_t *n,
    float *out,
    uint32_t frames)
{
    uint32_t done = 0, i, bits;
    ecs_time_t start;
    ecs_time_measure(&start);
    box_audio_native_receive(n);
    while (done < frames) {
        uint32_t block = frames - done;
        if (block > BOX_AUDIO_TRANSPORT_FRAMES) {
            block = BOX_AUDIO_TRANSPORT_FRAMES;
        }

        box_audio_core_render(n->core, n->left, n->right, (int32_t)block);
        if (out) {
            float *at = out + (size_t)done * 2;
            for (i = 0; i < block; i ++) {
                at[i * 2] = n->left[i];
                at[i * 2 + 1] = n->right[i];
            }
        }

        if (box_audio_capture_write(&n->capture, n->left, n->right, (int32_t)block)) {
            n->capture = (box_audio_capture_t){0};
            atomic_store(&n->captured, 1);
        }

        done += block;
    }

    box_audio_native_send_events(n);
    box_audio_native_publish(n);
    n->load_audio = box_audio_load_update(n->load_audio, ecs_time_measure(&start),
        (int32_t)frames, box_audio_core_sample_rate(n->core));
    memcpy(&bits, &n->load_audio, sizeof(bits));
    atomic_store(&n->load, bits);
    atomic_store(&n->voices, box_audio_core_voice_count(n->core));
    atomic_store(&n->clock, box_audio_core_clock(n->core));
    for (i = 0; i < BOX_AUDIO_STREAM_MAX; i ++) {
        box_audio_stream_status_t status;
        box_audio_native_stream_t *ns = &n->streams[i];
        box_audio_core_stream_status(n->core, i, &status);
        atomic_store(&ns->at, status.at);
        atomic_store(&ns->underruns, status.underruns);
        atomic_store(&ns->starved, status.starved);
        atomic_store(&ns->head, ((uint64_t)status.voice << 32) |
            ((uint64_t)(status.open ? 1 : 0) << 31) | ((uint64_t)(status.ended ? 1 : 0) << 30) |
            (uint64_t)(status.consumed & 0x3FFFFFFFu));
    }
}

/** The device callback, on the audio thread.
 *
 * @param device The device
 * @param output Receives interleaved float stereo frames
 * @param input Unused
 * @param frames Frames the device asks for, any count
 */
static void box_audio_native_data(
    ma_device *device,
    void *output,
    const void *input,
    ma_uint32 frames)
{
    (void)input;
    box_audio_native_process(device->pUserData, output, frames);
}

/** Notifications of the device, on a thread of miniaudio: a stop the main thread did not ask
 * for is a lost device, such as unplugged headphones. A rerouted device is handled by miniaudio.
 *
 * @param notification The notification
 */
static void box_audio_native_notification(
    const ma_device_notification *notification)
{
    box_audio_native_t *n = notification->pDevice->pUserData;
    if (notification->type == ma_device_notification_type_stopped &&
        !atomic_load(&n->stopping))
    {
        atomic_store(&n->lost, 1);
    }
}

/** Initialize and start the device.
 *
 * @param n The transport
 * @param sample_rate Frames per second to ask for, 0 for the rate of the device
 * @param why Receives the result of miniaudio when the open fails
 * @return 0 when the device plays, nonzero when it does not
 */
static int box_audio_native_open(
    box_audio_native_t *n,
    ma_uint32 sample_rate,
    ma_result *why)
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    ma_result r;
    int result = -1;
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = sample_rate;
    config.dataCallback = box_audio_native_data;
    config.notificationCallback = box_audio_native_notification;
    config.pUserData = n;
    config.noPreSilencedOutputBuffer = MA_TRUE;
    config.noClip = MA_TRUE;
    config.noFixedSizedCallback = MA_TRUE;
    config.performanceProfile = ma_performance_profile_low_latency;

    r = ma_device_init(&n->context, &config, &n->device);
    if (r != MA_SUCCESS) {
        *why = r;
        goto done;
    }

    n->device_open = true;
    if (!n->core) {
        n->core = box_audio_core_new(&(box_audio_core_desc_t){
            .sample_rate = (float)n->device.sampleRate });
        if (!n->core) {
            *why = MA_OUT_OF_MEMORY;
            goto done;
        }
    }

    atomic_store(&n->lost, 0);
    atomic_store(&n->stopping, 0);
    r = ma_device_start(&n->device);
    if (r != MA_SUCCESS) {
        *why = r;
        goto done;
    }

    ecs_trace("opened audio device '%s' (%u Hz)", n->device.playback.name,
        n->device.sampleRate);
    result = 0;
done:
    if (result && n->device_open) {
        atomic_store(&n->stopping, 1);
        ma_device_uninit(&n->device);
        n->device_open = false;
    }

    return result;
}

/** Stop and close the device; after this the main thread is the only consumer of the rings.
 *
 * @param n The transport
 */
static void box_audio_native_close(
    box_audio_native_t *n)
{
    if (n->device_open) {
        atomic_store(&n->stopping, 1);
        ma_device_uninit(&n->device);
        n->device_open = false;
    }
}

/** Append a message to the messages that wait on the main thread.
 *
 * @param n The transport
 * @param kind A box_audio_msg_kind_t
 * @param data The bytes after the header
 * @param size Number of bytes, at most BOX_AUDIO_TRANSPORT_CHUNK
 * @return Whether the message was kept
 */
static bool box_audio_native_queue(
    box_audio_native_t *n,
    uint32_t kind,
    const void *data,
    int32_t size)
{
    box_audio_msg_t msg = { kind, (uint32_t)size };
    int32_t need = n->pending_size + (int32_t)sizeof(msg) + size;
    bool result = false;
    if (need > n->pending_capacity) {
        int32_t capacity = n->pending_capacity ? n->pending_capacity * 2 : 4096;
        uint8_t *bytes;
        while (capacity < need) {
            capacity *= 2;
        }

        bytes = ecs_os_realloc(n->pending, capacity);
        if (!bytes) {
            goto done;
        }

        n->pending = bytes;
        n->pending_capacity = capacity;
    }

    memcpy(n->pending + n->pending_size, &msg, sizeof(msg));
    memcpy(n->pending + n->pending_size + sizeof(msg), data, (size_t)size);
    n->pending_size = need;
    result = true;
done:
    return result;
}

/** Write the waiting messages into the command ring in order, until it is full. What does not
 * fit waits for the next frame; neither side blocks.
 *
 * @param n The transport
 */
static void box_audio_native_flush(
    box_audio_native_t *n)
{
    int32_t at = 0;
    while (at < n->pending_size) {
        box_audio_msg_t msg;
        memcpy(&msg, n->pending + at, sizeof(msg));
        if (!box_audio_ring_write(&n->commands, &msg, (int32_t)sizeof(msg),
            n->pending + at + sizeof(msg), (int32_t)msg.size))
        {
            break;
        }

        at += (int32_t)sizeof(msg) + (int32_t)msg.size;
    }

    if (at) {
        memmove(n->pending, n->pending + at, (size_t)(n->pending_size - at));
        n->pending_size -= at;
    }
}

/** Take the events of the event ring.
 *
 * @param n The transport
 */
static void box_audio_native_collect(
    box_audio_native_t *n)
{
    box_audio_event_t event;
    while (box_audio_ring_read(&n->events, &event, (int32_t)sizeof(event))) {
        box_audio_host_event(&n->host, &event);
    }
}

/** Queue a batch, split at command boundaries into chunks the audio thread copies at once,
 * and write what fits into the command ring.
 *
 * @param t The transport
 * @param data The bytes
 * @param size Number of bytes
 * @return Whether the batch was kept
 */
static bool box_audio_native_commands(
    box_audio_transport_t *t,
    const void *data,
    int32_t size)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    const uint8_t *bytes = data;
    int32_t at = 0;
    bool result = data && size > 0;

    while (result && at < size) {
        int32_t end = at;
        while (end < size) {
            box_audio_cmd_header_t header;
            int32_t length = size - end;
            if (length >= (int32_t)sizeof(header)) {
                memcpy(&header, bytes + end, sizeof(header));
                length = header.size >= sizeof(header) && header.size <= size - end ?
                    (int32_t)header.size : size - end;
            }

            if (end > at && end + length - at > BOX_AUDIO_TRANSPORT_CHUNK) {
                break;
            }

            end += length;
        }

        if (end - at > BOX_AUDIO_TRANSPORT_CHUNK) {
            end = at + BOX_AUDIO_TRANSPORT_CHUNK;
        }

        result = box_audio_native_queue(n, BOX_AUDIO_MSG_COMMANDS, bytes + at, end - at);
        at = end;
    }

    box_audio_native_flush(n);

    return result;
}

/** Copy a sound and hand the copy to the audio thread; it stays with the main thread.
 *
 * @param t The transport
 * @param id Id of the sound
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound
 * @return Whether the sound was kept
 */
static bool box_audio_native_sound(
    box_audio_transport_t *t,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    float *copy = box_audio_host_sound(&n->host, id, data, frames, channels);
    bool result = false;
    if (!copy) {
        goto done;
    }

    box_audio_msg_sound_t sound = { copy, id, frames, channels, sample_rate };
    result = box_audio_native_queue(n, BOX_AUDIO_MSG_SOUND, &sound, (int32_t)sizeof(sound));
    if (!result) {
        box_audio_host_sound_free(&n->host, id);
        goto done;
    }

    box_audio_native_flush(n);
done:
    return result;
}

/** Copy a chunk of a stream and hand the copy to the audio thread; it stays with the main
 * thread until the status of the stream says the core is done with it.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param voice Voice that reads the stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param last Whether the stream ends with it
 * @return Whether the chunk was kept
 */
static bool box_audio_native_stream_chunk(
    box_audio_transport_t *t,
    uint32_t stream,
    uint32_t voice,
    const float *data,
    int32_t frames,
    int32_t channels,
    bool last)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    float *copy = box_audio_host_chunk(&n->host, stream, voice, data, frames * channels);
    bool result = false;
    if (!copy) {
        goto done;
    }

    box_audio_msg_chunk_t chunk = { copy, stream, frames, last ? 1 : 0 };
    result = box_audio_native_queue(n, BOX_AUDIO_MSG_CHUNK, &chunk, (int32_t)sizeof(chunk));
    if (!result) {
        box_audio_host_chunk_drop(&n->host, stream);
        goto done;
    }

    box_audio_native_flush(n);
done:
    return result;
}

/** Allocate the memory of an effect slot and hand it to the audio thread, as a message of
 * the command ring; it stays with the main thread, which frees it after the core.
 *
 * @param t The transport
 * @param slot Slot of the effect
 * @param floats Floats it needs
 * @return Whether the memory went out
 */
static bool box_audio_native_effect_memory(
    box_audio_transport_t *t,
    uint32_t slot,
    int32_t floats)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_msg_effect_t effect = { NULL, slot, floats };
    bool result = slot < BOX_AUDIO_EFFECT_MAX && n->host.effect_floats[slot] >= floats;
    if (result || slot >= BOX_AUDIO_EFFECT_MAX) {
        goto done;
    }

    effect.data = box_audio_host_effect(&n->host, slot, floats);
    result = effect.data && box_audio_native_queue(n, BOX_AUDIO_MSG_EFFECT, &effect,
        (int32_t)sizeof(effect));
    if (!result && effect.data) {
        box_audio_host_effect_drop(&n->host, slot);
        goto done;
    }

    box_audio_native_flush(n);
done:
    return result;
}

/** Read the status of a stream the audio thread published and free the chunks it is done
 * with.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param status Receives the status
 */
static void box_audio_native_stream_status(
    box_audio_transport_t *t,
    uint32_t stream,
    box_audio_stream_status_t *status)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_native_stream_t *ns;
    uint64_t head;
    *status = (box_audio_stream_status_t){0};
    if (stream >= BOX_AUDIO_STREAM_MAX) {
        goto done;
    }

    ns = &n->streams[stream];
    head = atomic_load(&ns->head);
    status->voice = (uint32_t)(head >> 32);
    status->open = (int32_t)((head >> 31) & 1);
    status->ended = (int32_t)((head >> 30) & 1);
    status->consumed = (uint32_t)(head & 0x3FFFFFFFu);
    status->at = atomic_load(&ns->at);
    status->underruns = atomic_load(&ns->underruns);
    status->starved = atomic_load(&ns->starved);
    box_audio_host_stream_sync(&n->host, stream, status);
done:
    return;
}

/** Take the events of the audio thread.
 *
 * @param t The transport
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
static int32_t box_audio_native_read_events(
    box_audio_transport_t *t,
    box_audio_event_t *events,
    int32_t max)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_native_collect(n);

    return box_audio_host_read(&n->host, events, max);
}

/** Take the meters the audio thread published since the last call, a swap of each word
 * with 0.
 *
 * @param t The transport
 * @param buses Receives BOX_AUDIO_BUS_MAX meters
 * @param effects Receives BOX_AUDIO_EFFECT_MAX meters
 */
static void box_audio_native_meters(
    box_audio_transport_t *t,
    box_audio_meter_t *buses,
    box_audio_effect_meter_t *effects)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        box_audio_native_meter_t *nm = &n->meters[i];
        box_audio_meter_t *m = &buses[i];
        unsigned int l = atomic_exchange(&nm->peak_left, 0u);
        unsigned int r = atomic_exchange(&nm->peak_right, 0u);
        uint_least64_t sl = atomic_exchange(&nm->square_left, (uint_least64_t)0);
        uint_least64_t sr = atomic_exchange(&nm->square_right, (uint_least64_t)0);
        uint_least64_t kw = atomic_exchange(&nm->loudness, (uint_least64_t)0);
        double loudness;
        *m = (box_audio_meter_t){0};
        memcpy(&loudness, &kw, sizeof(kw));
        m->loudness = (float)loudness;
        memcpy(&m->peak_left, &l, sizeof(l));
        memcpy(&m->peak_right, &r, sizeof(r));
        memcpy(&m->square_left, &sl, sizeof(sl));
        memcpy(&m->square_right, &sr, sizeof(sr));
        m->frames = atomic_exchange(&nm->frames, 0u);
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        unsigned int bits = atomic_exchange(&n->reduction[i], 0u);
        memcpy(&effects[i].reduction, &bits, sizeof(bits));
    }
}

/** Report RUNNING, or LOST from the moment the device stopped.
 *
 * @param t The transport
 * @return The state
 */
static box_audio_state_t box_audio_native_state(
    box_audio_transport_t *t)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_state_t result = n->state;
    if (result == BOX_AUDIO_STATE_RUNNING && atomic_load(&n->lost)) {
        result = BOX_AUDIO_STATE_LOST;
    }

    return result;
}

/** Flush the commands and collect the events, once per frame.
 *
 * A lost device is closed, which joins its thread, so from then on the main thread is the only
 * consumer of the command ring: it renders the frames of the delta, so voices end and sounds are
 * freed as with a device, and tries to reopen the default device every few seconds.
 *
 * @param t The transport
 * @param delta Seconds of the frame
 */
static void box_audio_native_update(
    box_audio_transport_t *t,
    float delta)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    ma_result why = MA_SUCCESS;
    int32_t frames;

    if (n->state == BOX_AUDIO_STATE_RUNNING && atomic_load(&n->lost)) {
        box_audio_native_close(n);
        n->state = BOX_AUDIO_STATE_LOST;
        n->retry = BOX_AUDIO_NATIVE_RETRY;
        n->host.clock = 0;
        n->host.rendered = 0;
        if (!n->warned) {
            ecs_warn("box.audio: the audio device stopped, the app runs without sound until "
                "the default device reopens");
            n->warned = true;
        }
    }

    box_audio_native_flush(n);
    if (n->state == BOX_AUDIO_STATE_LOST) {
        frames = box_audio_host_frames(&n->host, delta, t->sample_rate);
        box_audio_native_process(n, NULL, (uint32_t)frames);
        n->retry -= delta > 0 ? delta : 0;
        if (n->retry <= 0) {
            n->retry = BOX_AUDIO_NATIVE_RETRY;
            if (!box_audio_native_open(n, (ma_uint32)t->sample_rate, &why)) {
                n->state = BOX_AUDIO_STATE_RUNNING;
            }
        }

        box_audio_native_flush(n);
    }

    box_audio_native_collect(n);
}

/** Report the voices and the load the audio thread measured last.
 *
 * @param t The transport
 * @param stats Receives the stats
 */
static void box_audio_native_stats(
    box_audio_transport_t *t,
    box_audio_transport_stats_t *stats)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    uint32_t bits = atomic_load(&n->load);
    memcpy(&stats->load, &bits, sizeof(bits));
    stats->voices = atomic_load(&n->voices);
    stats->chunks = n->host.chunks;
}

/** Report the clock of the core the audio thread published after its last render.
 *
 * @param t The transport
 * @return Frames of the core clock
 */
static uint64_t box_audio_native_clock(
    box_audio_transport_t *t)
{
    return (uint64_t)atomic_load(&((box_audio_native_t*)t)->clock);
}

/** Hand the audio thread a buffer to record its next frames into; it stays with the main
 * thread, which takes it back once the audio thread reports it complete.
 *
 * @param t The transport
 * @param frames Frames to record
 * @return Whether the capture started
 */
static bool box_audio_native_capture(
    box_audio_transport_t *t,
    int32_t frames)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_capture_t capture = { NULL, frames, 0 };
    bool result = false;
    if (n->capture_data || frames <= 0) {
        goto done;
    }

    capture.data = ecs_os_malloc_n(float, frames * 2);
    if (!capture.data) {
        goto done;
    }

    atomic_store(&n->captured, 0);
    if (!box_audio_native_queue(n, BOX_AUDIO_MSG_CAPTURE, &capture, (int32_t)sizeof(capture))) {
        ecs_os_free(capture.data);
        goto done;
    }

    n->capture_data = capture.data;
    n->capture_frames = frames;
    box_audio_native_flush(n);
    result = true;
done:
    return result;
}

/** Take the frames of a capture the audio thread completed.
 *
 * @param t The transport
 * @param frames Receives the number of frames
 * @return The frames, owned by the caller, NULL while the capture records
 */
static float* box_audio_native_capture_take(
    box_audio_transport_t *t,
    int32_t *frames)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    float *result = NULL;
    if (n->capture_data && atomic_load(&n->captured)) {
        result = n->capture_data;
        *frames = n->capture_frames;
        n->capture_data = NULL;
        n->capture_frames = 0;
        atomic_store(&n->captured, 0);
    }

    return result;
}

/** Close the device and free the transport.
 *
 * @param t The transport
 */
static void box_audio_native_free(
    box_audio_transport_t *t)
{
    box_audio_native_t *n = (box_audio_native_t*)t;
    box_audio_native_close(n);
    ecs_os_free(n->capture_data);
    ma_context_uninit(&n->context);
    box_audio_core_free(n->core);
    box_audio_host_fini(&n->host);
    box_audio_ring_fini(&n->commands);
    box_audio_ring_fini(&n->events);
    ecs_os_free(n->pending);
    ecs_os_free(n);
}

box_audio_transport_t* box_audio_transport_native_new(
    const box_audio_native_desc_t *desc)
{
    box_audio_native_t *n = ecs_os_calloc_t(box_audio_native_t);
    box_audio_transport_t *result = NULL;
    ma_context_config config = ma_context_config_init();
    ma_backend unavailable[] = {
#ifdef __APPLE__
        ma_backend_alsa
#else
        ma_backend_coreaudio
#endif
    };
    bool context = false;
    ma_result why = MA_OUT_OF_MEMORY;
    if (!n) {
        goto done;
    }

    atomic_init(&n->lost, 0);
    atomic_init(&n->stopping, 0);
    atomic_init(&n->captured, 0);
    atomic_init(&n->voices, 0);
    atomic_init(&n->load, 0);
    atomic_init(&n->clock, 0);
    for (int32_t i = 0; i < BOX_AUDIO_STREAM_MAX; i ++) {
        atomic_init(&n->streams[i].head, 0);
        atomic_init(&n->streams[i].at, 0);
        atomic_init(&n->streams[i].underruns, 0);
        atomic_init(&n->streams[i].starved, 0);
    }

    for (int32_t i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        atomic_init(&n->meters[i].peak_left, 0u);
        atomic_init(&n->meters[i].peak_right, 0u);
        atomic_init(&n->meters[i].square_left, (uint_least64_t)0);
        atomic_init(&n->meters[i].square_right, (uint_least64_t)0);
        atomic_init(&n->meters[i].frames, 0u);
        atomic_init(&n->meters[i].loudness, (uint_least64_t)0);
    }

    for (int32_t i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        atomic_init(&n->reduction[i], 0u);
    }

    if (box_audio_host_init(&n->host) ||
        box_audio_ring_init(&n->commands, BOX_AUDIO_TRANSPORT_COMMAND_RING) ||
        box_audio_ring_init(&n->events,
            BOX_AUDIO_TRANSPORT_EVENT_RING * (int32_t)sizeof(box_audio_event_t)))
    {
        goto done;
    }

    config.threadPriority = ma_thread_priority_realtime;
    why = desc && desc->backend == BOX_AUDIO_BACKEND_UNAVAILABLE ?
        ma_context_init(unavailable, 1, &config, &n->context) :
        ma_context_init(NULL, 0, &config, &n->context);
    if (why != MA_SUCCESS) {
        goto done;
    }

    context = true;
    if (box_audio_native_open(n, 0, &why)) {
        goto done;
    }

    n->state = BOX_AUDIO_STATE_RUNNING;
    n->base = (box_audio_transport_t){
        .kind = BOX_AUDIO_TRANSPORT_NATIVE,
        .sample_rate = box_audio_core_sample_rate(n->core),
        .commands = box_audio_native_commands,
        .sound = box_audio_native_sound,
        .stream_chunk = box_audio_native_stream_chunk,
        .stream_status = box_audio_native_stream_status,
        .effect_memory = box_audio_native_effect_memory,
        .read_events = box_audio_native_read_events,
        .meters = box_audio_native_meters,
        .state = box_audio_native_state,
        .update = box_audio_native_update,
        .free = box_audio_native_free,
        .stats = box_audio_native_stats,
        .clock = box_audio_native_clock,
        .capture = box_audio_native_capture,
        .capture_take = box_audio_native_capture_take
    };
    result = &n->base;
done:
    if (!result) {
        ecs_warn("box.audio: no audio device (%s), the app runs without sound",
            ma_result_description(why));
        if (n) {
            if (context) {
                ma_context_uninit(&n->context);
            }

            box_audio_core_free(n->core);
            box_audio_host_fini(&n->host);
            box_audio_ring_fini(&n->commands);
            box_audio_ring_fini(&n->events);
            ecs_os_free(n);
        }
    }

    return result;
}

#else

box_audio_transport_t* box_audio_transport_native_new(
    const box_audio_native_desc_t *desc)
{
    (void)desc;

    return NULL;
}

#endif
