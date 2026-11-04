/**
 * @file audio/transport.c
 * @brief The main thread side shared by the transports, the null transport and the web one.
 *
 * The native transport is in native.c, the only file that includes miniaudio.
 */

#include "host.h"
#include "platform.h"
#include <flecs.h>
#include <math.h>
#include <string.h>

int box_audio_host_init(
    box_audio_host_t *host)
{
    host->events = ecs_os_malloc_n(box_audio_event_t, BOX_AUDIO_TRANSPORT_EVENTS);

    return host->events ? 0 : -1;
}

/** Free the oldest chunks of a stream.
 *
 * @param host The host
 * @param stream Index of the stream
 * @param count Number of chunks, at most the chunks held
 */
static void box_audio_host_chunks_free(
    box_audio_host_t *host,
    uint32_t stream,
    int32_t count)
{
    box_audio_host_stream_t *s = &host->streams[stream];
    int32_t i;
    for (i = 0; i < count && s->count; i ++) {
        ecs_os_free(s->chunks[s->first]);
        s->chunks[s->first] = NULL;
        s->first = (s->first + 1) % BOX_AUDIO_HOST_CHUNKS;
        s->count --;
        host->chunks --;
    }
}

float* box_audio_host_chunk(
    box_audio_host_t *host,
    uint32_t stream,
    uint32_t voice,
    const float *data,
    int32_t count)
{
    box_audio_host_stream_t *s;
    float *result = NULL;
    if (stream >= BOX_AUDIO_STREAM_MAX || !data || count <= 0 ||
        count > BOX_AUDIO_STREAM_CHUNK * 2)
    {
        goto done;
    }

    s = &host->streams[stream];
    if (s->voice != voice) {
        box_audio_host_chunks_free(host, stream, s->count);
        s->voice = voice;
        s->freed = 0;
    }

    if (s->count >= BOX_AUDIO_HOST_CHUNKS) {
        goto done;
    }

    result = ecs_os_malloc_n(float, count);
    if (!result) {
        goto done;
    }

    memcpy(result, data, (size_t)count * sizeof(float));
    s->chunks[(s->first + s->count) % BOX_AUDIO_HOST_CHUNKS] = result;
    s->count ++;
    host->chunks ++;
done:
    return result;
}

void box_audio_host_chunk_drop(
    box_audio_host_t *host,
    uint32_t stream)
{
    box_audio_host_stream_t *s;
    int32_t at;
    if (stream >= BOX_AUDIO_STREAM_MAX || !host->streams[stream].count) {
        goto done;
    }

    s = &host->streams[stream];
    at = (s->first + s->count - 1) % BOX_AUDIO_HOST_CHUNKS;
    ecs_os_free(s->chunks[at]);
    s->chunks[at] = NULL;
    s->count --;
    host->chunks --;
done:
    return;
}

void box_audio_host_stream_sync(
    box_audio_host_t *host,
    uint32_t stream,
    const box_audio_stream_status_t *status)
{
    box_audio_host_stream_t *s;
    if (stream >= BOX_AUDIO_STREAM_MAX) {
        goto done;
    }

    s = &host->streams[stream];
    if (!s->count || status->voice != s->voice) {
        goto done;
    }

    if (!status->open) {
        box_audio_host_chunks_free(host, stream, s->count);
        s->freed = 0;
    } else if (status->consumed > s->freed) {
        int32_t count = (int32_t)(status->consumed - s->freed);
        count = count < s->count ? count : s->count;
        box_audio_host_chunks_free(host, stream, count);
        s->freed += (uint32_t)count;
    }

done:
    return;
}

void box_audio_host_fini(
    box_audio_host_t *host)
{
    uint32_t i;
    for (i = 0; i < BOX_AUDIO_SOUND_MAX; i ++) {
        box_audio_host_sound_free(host, i);
    }

    for (i = 0; i < BOX_AUDIO_STREAM_MAX; i ++) {
        box_audio_host_chunks_free(host, i, host->streams[i].count);
    }

    for (i = 0; i < BOX_AUDIO_REVERB_MAX; i ++) {
        ecs_os_free(host->reverbs[i]);
        host->reverbs[i] = NULL;
    }

    ecs_os_free(host->events);
    host->events = NULL;
    host->event_count = 0;
}

float* box_audio_host_sound(
    box_audio_host_t *host,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels)
{
    float *result = NULL;
    if (id >= BOX_AUDIO_SOUND_MAX || host->sounds[id] || !data || frames <= 0 ||
        (channels != 1 && channels != 2) || frames > INT32_MAX / 4 / channels)
    {
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)frames * channels * (ecs_size_t)sizeof(float));
    if (!result) {
        goto done;
    }

    memcpy(result, data, (size_t)frames * (size_t)channels * sizeof(float));
    host->sounds[id] = result;
done:
    return result;
}

float* box_audio_host_reverb(
    box_audio_host_t *host,
    uint32_t slot,
    float sample_rate,
    int32_t *floats)
{
    float *result = NULL;
    if (slot >= BOX_AUDIO_REVERB_MAX || host->reverbs[slot]) {
        goto done;
    }

    *floats = box_audio_reverb_floats(sample_rate);
    result = ecs_os_calloc_n(float, *floats);
    host->reverbs[slot] = result;
done:
    return result;
}

void box_audio_host_sound_free(
    box_audio_host_t *host,
    uint32_t id)
{
    if (id < BOX_AUDIO_SOUND_MAX && host->sounds[id]) {
        ecs_os_free(host->sounds[id]);
        host->sounds[id] = NULL;
    }
}

void box_audio_host_event(
    box_audio_host_t *host,
    const box_audio_event_t *event)
{
    int32_t at;
    if (event->type == BOX_AUDIO_EVENT_SOUND_FREE || event->type == BOX_AUDIO_EVENT_SOUND_REFUSED) {
        box_audio_host_sound_free(host, event->id);
    }

    if (event->type == BOX_AUDIO_EVENT_SOUND_REFUSED || !host->events) {
        goto done;
    }

    if (host->event_count == BOX_AUDIO_TRANSPORT_EVENTS) {
        host->event_first = (host->event_first + 1) % BOX_AUDIO_TRANSPORT_EVENTS;
        host->event_count --;
    }

    at = (host->event_first + host->event_count) % BOX_AUDIO_TRANSPORT_EVENTS;
    host->events[at] = *event;
    host->event_count ++;
done:
    return;
}

int32_t box_audio_host_read(
    box_audio_host_t *host,
    box_audio_event_t *events,
    int32_t max)
{
    int32_t result = 0;
    while (result < max && host->event_count) {
        events[result ++] = host->events[host->event_first];
        host->event_first = (host->event_first + 1) % BOX_AUDIO_TRANSPORT_EVENTS;
        host->event_count --;
    }

    return result;
}

int32_t box_audio_host_frames(
    box_audio_host_t *host,
    float delta,
    float sample_rate)
{
    int64_t target, result = 0;
    if (!(delta > 0) || !(sample_rate > 0)) {
        goto done;
    }

    host->clock += (double)delta;
    target = (int64_t)floor(host->clock * (double)sample_rate);
    result = target - host->rendered;
    if (result > (int64_t)sample_rate) {
        result = (int64_t)sample_rate;
    }

    host->rendered = target;
    if (result < 0) {
        result = 0;
    }

done:
    return (int32_t)result;
}

bool box_audio_capture_write(
    box_audio_capture_t *capture,
    const float *left,
    const float *right,
    int32_t frames)
{
    int32_t i, count;
    bool result = false;
    if (!capture->data) {
        goto done;
    }

    count = capture->frames - capture->at;
    count = frames < count ? frames : count;
    for (i = 0; i < count; i ++) {
        capture->data[(capture->at + i) * 2] = left[i];
        capture->data[(capture->at + i) * 2 + 1] = right[i];
    }

    capture->at += count;
    result = capture->at >= capture->frames;
done:
    return result;
}

float box_audio_load_update(
    float load,
    double seconds,
    int32_t frames,
    float sample_rate)
{
    float result = load;
    if (frames > 0 && sample_rate > 0 && seconds >= 0) {
        float now = (float)(seconds * (double)sample_rate / (double)frames);
        result = load + (now - load) * BOX_AUDIO_LOAD_SMOOTHING;
    }

    return result;
}

void box_audio_transport_free(
    box_audio_transport_t *t)
{
    if (t) {
        t->free(t);
    }
}

/* -- The null transport ----------------------------------------------------------------- */

/** The core on the main thread, rendered for the frame deltas */
typedef struct box_audio_null_t {
    box_audio_transport_t base;   /**< The interface, first */
    box_audio_host_t host;        /**< Sounds and events */
    box_audio_core_t *core;       /**< The core */
    box_audio_output_t output;    /**< Receives the rendered frames */
    void *ctx;                    /**< Context of output */
    box_audio_capture_t capture;  /**< The capture that records, if any */
    bool captured;                /**< The capture is complete */
    float load;                   /**< Smoothed load of the renders */
    float left[BOX_AUDIO_TRANSPORT_FRAMES];   /**< Rendered left channel */
    float right[BOX_AUDIO_TRANSPORT_FRAMES];  /**< Rendered right channel */
} box_audio_null_t;

/** Apply a batch to the core on the main thread; it takes effect at the next render.
 *
 * @param t The transport
 * @param data The bytes
 * @param size Number of bytes
 * @return Whether the batch was applied
 */
static bool box_audio_null_commands(
    box_audio_transport_t *t,
    const void *data,
    int32_t size)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    bool result = data && size > 0;
    if (result) {
        box_audio_core_apply(n->core, data, size);
    }

    return result;
}

/** Copy a sound and give it to the core.
 *
 * @param t The transport
 * @param id Id of the sound
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound
 * @return Whether the core took the sound
 */
static bool box_audio_null_sound(
    box_audio_transport_t *t,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    float *copy = box_audio_host_sound(&n->host, id, data, frames, channels);
    bool result = false;
    if (!copy) {
        goto done;
    }

    result = box_audio_core_sound(n->core, id, copy, frames, channels, sample_rate);
    if (!result) {
        box_audio_host_sound_free(&n->host, id);
    }

done:
    return result;
}

/** Copy a chunk of a stream and give it to the core.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param voice Voice that reads the stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param last Whether the stream ends with it
 * @return Whether the core took the chunk
 */
static bool box_audio_null_stream_chunk(
    box_audio_transport_t *t,
    uint32_t stream,
    uint32_t voice,
    const float *data,
    int32_t frames,
    int32_t channels,
    bool last)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    float *copy = box_audio_host_chunk(&n->host, stream, voice, data, frames * channels);
    bool result = false;
    if (!copy) {
        goto done;
    }

    result = box_audio_core_stream_chunk(n->core, stream, copy, frames, last);
    if (!result) {
        box_audio_host_chunk_drop(&n->host, stream);
    }

done:
    return result;
}

/** Allocate the delay memory of a reverb slot and give it to the core.
 *
 * @param t The transport
 * @param slot Slot of the reverb
 * @return Whether the core has the memory
 */
static bool box_audio_null_reverb(
    box_audio_transport_t *t,
    uint32_t slot)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    int32_t floats = 0;
    float *memory;
    bool result = slot < BOX_AUDIO_REVERB_MAX && n->host.reverbs[slot];
    if (result || slot >= BOX_AUDIO_REVERB_MAX) {
        goto done;
    }

    memory = box_audio_host_reverb(&n->host, slot, t->sample_rate, &floats);
    result = memory && box_audio_core_reverb_memory(n->core, slot, memory, floats);
done:
    return result;
}

/** Read the status of a stream of the core and free the chunks it is done with.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param status Receives the status
 */
static void box_audio_null_stream_status(
    box_audio_transport_t *t,
    uint32_t stream,
    box_audio_stream_status_t *status)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    box_audio_core_stream_status(n->core, stream, status);
    box_audio_host_stream_sync(&n->host, stream, status);
}

/** Take the events of the core.
 *
 * @param t The transport
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
static int32_t box_audio_null_read_events(
    box_audio_transport_t *t,
    box_audio_event_t *events,
    int32_t max)
{
    return box_audio_host_read(&((box_audio_null_t*)t)->host, events, max);
}

/** Report that there is no device.
 *
 * @param t The transport
 * @return BOX_AUDIO_STATE_NO_DEVICE
 */
static box_audio_state_t box_audio_null_state(
    box_audio_transport_t *t)
{
    (void)t;

    return BOX_AUDIO_STATE_NO_DEVICE;
}

/** Render the frames of a delta and collect the events of the core.
 *
 * @param t The transport
 * @param delta Seconds of the frame
 */
static void box_audio_null_update(
    box_audio_transport_t *t,
    float delta)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    box_audio_event_t events[64];
    int32_t frames = box_audio_host_frames(&n->host, delta, t->sample_rate);
    int32_t total = frames, count, i;
    ecs_time_t start;
    ecs_time_measure(&start);
    while (frames > 0) {
        int32_t block = frames < BOX_AUDIO_TRANSPORT_FRAMES ? frames : BOX_AUDIO_TRANSPORT_FRAMES;
        box_audio_core_render(n->core, n->left, n->right, block);
        if (n->output) {
            n->output(n->left, n->right, block, n->ctx);
        }

        if (box_audio_capture_write(&n->capture, n->left, n->right, block)) {
            n->captured = true;
        }

        frames -= block;
    }

    n->load = box_audio_load_update(n->load, ecs_time_measure(&start), total, t->sample_rate);

    do {
        count = box_audio_core_read_events(n->core, events, 64);
        for (i = 0; i < count; i ++) {
            box_audio_host_event(&n->host, &events[i]);
        }
    } while (count == 64);
}

/** Report the voices of the core and the load of the renders.
 *
 * @param t The transport
 * @param stats Receives the stats
 */
static void box_audio_null_stats(
    box_audio_transport_t *t,
    box_audio_transport_stats_t *stats)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    stats->voices = box_audio_core_voice_count(n->core);
    stats->load = n->load;
    stats->chunks = n->host.chunks;
}

/** Report the frames the core rendered so far.
 *
 * @param t The transport
 * @return Frames of the core clock
 */
static uint64_t box_audio_null_clock(
    box_audio_transport_t *t)
{
    return box_audio_core_clock(((box_audio_null_t*)t)->core);
}

/** Start to record the frames of the next renders.
 *
 * @param t The transport
 * @param frames Frames to record
 * @return Whether the capture started
 */
static bool box_audio_null_capture(
    box_audio_transport_t *t,
    int32_t frames)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    bool result = false;
    if (n->capture.data || frames <= 0) {
        goto done;
    }

    n->capture.data = ecs_os_malloc_n(float, frames * 2);
    n->capture.frames = frames;
    n->capture.at = 0;
    n->captured = false;
    result = n->capture.data != NULL;
done:
    return result;
}

/** Take the frames of a complete capture.
 *
 * @param t The transport
 * @param frames Receives the number of frames
 * @return The frames, owned by the caller, NULL while the capture records
 */
static float* box_audio_null_capture_take(
    box_audio_transport_t *t,
    int32_t *frames)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    float *result = NULL;
    if (n->captured) {
        result = n->capture.data;
        *frames = n->capture.frames;
        n->capture = (box_audio_capture_t){0};
        n->captured = false;
    }

    return result;
}

/** Free the core, then the sounds.
 *
 * @param t The transport
 */
static void box_audio_null_free(
    box_audio_transport_t *t)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
    ecs_os_free(n->capture.data);
    box_audio_core_free(n->core);
    box_audio_host_fini(&n->host);
    ecs_os_free(n);
}

box_audio_transport_t* box_audio_transport_null_new(
    const box_audio_null_desc_t *desc)
{
    box_audio_null_t *n = ecs_os_calloc_t(box_audio_null_t);
    box_audio_transport_t *result = NULL;
    float rate = desc && desc->sample_rate > 0 ? desc->sample_rate : 48000.0f;
    if (!n) {
        goto done;
    }

    n->core = box_audio_core_new(&(box_audio_core_desc_t){ .sample_rate = rate });
    if (!n->core || box_audio_host_init(&n->host)) {
        box_audio_core_free(n->core);
        ecs_os_free(n->host.events);
        ecs_os_free(n);
        goto done;
    }

    n->output = desc ? desc->output : NULL;
    n->ctx = desc ? desc->ctx : NULL;
    n->base = (box_audio_transport_t){
        .kind = BOX_AUDIO_TRANSPORT_NULL,
        .sample_rate = box_audio_core_sample_rate(n->core),
        .commands = box_audio_null_commands,
        .sound = box_audio_null_sound,
        .stream_chunk = box_audio_null_stream_chunk,
        .stream_status = box_audio_null_stream_status,
        .reverb = box_audio_null_reverb,
        .read_events = box_audio_null_read_events,
        .state = box_audio_null_state,
        .update = box_audio_null_update,
        .free = box_audio_null_free,
        .stats = box_audio_null_stats,
        .clock = box_audio_null_clock,
        .capture = box_audio_null_capture,
        .capture_take = box_audio_null_capture_take
    };
    result = &n->base;
done:
    return result;
}

/* -- The web transport ------------------------------------------------------------------ */

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>

/** Read the state of the audio of the page.
 *
 * @return A box_audio_state_t
 */
EM_JS(int, box_audio_web_state, (void), {
    var audio = Module.boxAudio;
    if (!audio) {
        return 2;
    }
    if (audio.failed) {
        return 5;
    }
    if (audio.hidden && audio.wasRunning) {
        return 7;
    }
    if (audio.ctx.state !== 'running') {
        return 6;
    }
    return audio.node ? 3 : 2;
});

/** Read the sample rate of the AudioContext of the page.
 *
 * @return Frames per second, 0 when the page has no audio
 */
EM_JS(float, box_audio_web_rate, (void), {
    var audio = Module.boxAudio;
    return audio && !audio.failed ? audio.ctx.sampleRate : 0;
});

/** Read the voices the worklet reported last.
 *
 * @return Number of voices
 */
EM_JS(int, box_audio_web_voices, (void), {
    var audio = Module.boxAudio;
    return audio && audio.voices ? audio.voices : 0;
});

/** Read the clock of the core of the worklet, extrapolated from the last report.
 *
 * @return Frames of the core clock, 0 when the page has no audio
 */
EM_JS(double, box_audio_web_clock, (void), {
    var audio = Module.boxAudio;
    return audio && audio.clock ? audio.clock() : 0;
});

/** Ask the worklet to record the frames of its next quanta.
 *
 * @param frames Frames to record
 * @return Whether the request was posted
 */
EM_JS(int, box_audio_web_capture_start, (int frames), {
    var audio = Module.boxAudio;
    if (!audio || audio.failed || audio.capturing) {
        return 0;
    }
    audio.capturing = true;
    audio.captured = null;
    audio.post({ type: 'capture', frames: frames });
    return 1;
});

/** Copy a capture the worklet completed into the heap of the main module.
 *
 * @param dst Receives frames * 2 floats
 * @param frames Frames of the capture
 * @return Whether a complete capture was copied
 */
EM_JS(int, box_audio_web_capture_take, (float *dst, int frames), {
    var audio = Module.boxAudio;
    var data = audio && audio.captured;
    if (!data || data.length !== frames * 2) {
        return 0;
    }
    HEAPF32.set(data, dst >> 2);
    audio.captured = null;
    audio.capturing = false;
    return 1;
});

/** Read the status of a stream the worklet reported last.
 *
 * @param stream Index of the stream
 * @param out Receives voice, open, ended, consumed, at, underruns and starved
 */
EM_JS(void, box_audio_web_stream_status, (int stream, int32_t *out), {
    var audio = Module.boxAudio;
    var s = audio && audio.streams && audio.streams[stream];
    var fields = ['voice', 'open', 'ended', 'consumed', 'at', 'underruns', 'starved'];
    for (var i = 0; i < fields.length; i++) {
        HEAP32[(out >> 2) + i] = s ? (s[fields[i]] | 0) : 0;
    }
});

/** Take the events the worklet reported since the last call.
 *
 * @param events Receives type, id and value per event
 * @param max Room in events, in events
 * @return Number of events written
 */
EM_JS(int, box_audio_web_events, (int32_t *events, int max), {
    var audio = Module.boxAudio;
    var queue = audio && audio.queue;
    var n = 0;
    if (!queue) {
        return 0;
    }
    while (n < max && queue.length) {
        var e = queue.shift();
        HEAP32[(events >> 2) + n * 3] = e.type;
        HEAP32[(events >> 2) + n * 3 + 1] = e.id;
        HEAP32[(events >> 2) + n * 3 + 2] = e.value;
        n++;
    }
    return n;
});

/** The web transport: the worklet renders, the main module waits for its captures */
typedef struct box_audio_webt_t {
    box_audio_transport_t base;  /**< The interface, first */
    int32_t capture;             /**< Frames of the capture that records, 0 for none */
    uint32_t reverbs;            /**< Reverb slots whose memory was asked for, a bit each */
} box_audio_webt_t;

/** Post a batch to the worklet.
 *
 * @param t The transport
 * @param data The bytes
 * @param size Number of bytes
 * @return Whether the batch was posted
 */
static bool box_audio_webt_commands(
    box_audio_transport_t *t,
    const void *data,
    int32_t size)
{
    (void)t;
    box_audio_web_apply(data, size);

    return data && size > 0;
}

/** Post a sound to the worklet, which replies whether its core took it.
 *
 * @param t The transport
 * @param id Id of the sound
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound
 * @return Whether the sound was posted
 */
static bool box_audio_webt_sound(
    box_audio_transport_t *t,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    (void)t;
    box_audio_web_sound(id, data, frames, channels, sample_rate);

    return data && frames > 0 && (channels == 1 || channels == 2);
}

/** Post a chunk of a stream to the worklet, which copies it into the heap of its core and frees
 * it there once the core consumed it.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param voice Voice that reads the stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param last Whether the stream ends with it
 * @return Whether the chunk was posted
 */
static bool box_audio_webt_stream_chunk(
    box_audio_transport_t *t,
    uint32_t stream,
    uint32_t voice,
    const float *data,
    int32_t frames,
    int32_t channels,
    bool last)
{
    (void)t;
    (void)voice;
    bool result = data && frames > 0 && frames <= BOX_AUDIO_STREAM_CHUNK &&
        (channels == 1 || channels == 2) && stream < BOX_AUDIO_STREAM_MAX;
    if (result) {
        box_audio_web_chunk(stream, data, frames, channels, last);
    }

    return result;
}

/** Ask the worklet to allocate the delay memory of a reverb slot in the heap of its core.
 *
 * @param t The transport
 * @param slot Slot of the reverb
 * @return Whether the request was posted
 */
static bool box_audio_webt_reverb(
    box_audio_transport_t *t,
    uint32_t slot)
{
    box_audio_webt_t *w = (box_audio_webt_t*)t;
    bool result = slot < BOX_AUDIO_REVERB_MAX;
    if (result && !(w->reverbs & (1u << slot))) {
        box_audio_web_reverb(slot);
        w->reverbs |= 1u << slot;
    }

    return result;
}

/** Report the status of a stream the worklet posted last; the worklet frees its chunks.
 *
 * @param t The transport
 * @param stream Index of the stream
 * @param status Receives the status
 */
static void box_audio_webt_stream_status(
    box_audio_transport_t *t,
    uint32_t stream,
    box_audio_stream_status_t *status)
{
    (void)t;
    box_audio_web_stream_status((int)stream, (int32_t*)status);
}

/** Take the events the worklet reported.
 *
 * @param t The transport
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
static int32_t box_audio_webt_read_events(
    box_audio_transport_t *t,
    box_audio_event_t *events,
    int32_t max)
{
    (void)t;

    return max > 0 ? box_audio_web_events((int32_t*)events, max) : 0;
}

/** Report the state of the audio of the page.
 *
 * @param t The transport
 * @return The state
 */
static box_audio_state_t box_audio_webt_state(
    box_audio_transport_t *t)
{
    (void)t;

    return (box_audio_state_t)box_audio_web_state();
}

/** Nothing per frame: the worklet renders on its own.
 *
 * @param t The transport
 * @param delta Seconds of the frame
 */
static void box_audio_webt_update(
    box_audio_transport_t *t,
    float delta)
{
    (void)t;
    (void)delta;
}

/** Free the transport; the audio of the page stays.
 *
 * @param t The transport
 */
static void box_audio_webt_free(
    box_audio_transport_t *t)
{
    ecs_os_free(t);
}

/** Report the voices the worklet reported last; the worklet does not measure its load.
 *
 * @param t The transport
 * @param stats Receives the stats
 */
static void box_audio_webt_stats(
    box_audio_transport_t *t,
    box_audio_transport_stats_t *stats)
{
    t->sample_rate = box_audio_web_rate();
    stats->voices = box_audio_web_voices();
    stats->load = 0;
}

/** Report the clock of the core of the worklet, as the page extrapolates it.
 *
 * @param t The transport
 * @return Frames of the core clock
 */
static uint64_t box_audio_webt_clock(
    box_audio_transport_t *t)
{
    double clock = box_audio_web_clock();
    (void)t;

    return clock > 0 ? (uint64_t)clock : 0;
}

/** Ask the worklet to record its next frames.
 *
 * @param t The transport
 * @param frames Frames to record
 * @return Whether the request was posted
 */
static bool box_audio_webt_capture(
    box_audio_transport_t *t,
    int32_t frames)
{
    box_audio_webt_t *w = (box_audio_webt_t*)t;
    bool result = false;
    if (!w->capture && frames > 0 && box_audio_web_capture_start(frames)) {
        w->capture = frames;
        result = true;
    }

    return result;
}

/** Take a capture the worklet completed.
 *
 * @param t The transport
 * @param frames Receives the number of frames
 * @return The frames, owned by the caller, NULL while the capture records
 */
static float* box_audio_webt_capture_take(
    box_audio_transport_t *t,
    int32_t *frames)
{
    box_audio_webt_t *w = (box_audio_webt_t*)t;
    float *result = NULL;
    if (!w->capture) {
        goto done;
    }

    result = ecs_os_malloc_n(float, w->capture * 2);
    if (result && box_audio_web_capture_take(result, w->capture)) {
        *frames = w->capture;
        w->capture = 0;
    } else {
        ecs_os_free(result);
        result = NULL;
    }

done:
    return result;
}

box_audio_transport_t* box_audio_transport_web_new(void)
{
    box_audio_webt_t *w = ecs_os_calloc_t(box_audio_webt_t);
    box_audio_transport_t *result = NULL;
    box_audio_web_start();
    if (w) {
        w->base = (box_audio_transport_t){
            .kind = BOX_AUDIO_TRANSPORT_WEB,
            .commands = box_audio_webt_commands,
            .sound = box_audio_webt_sound,
            .stream_chunk = box_audio_webt_stream_chunk,
            .stream_status = box_audio_webt_stream_status,
            .reverb = box_audio_webt_reverb,
            .read_events = box_audio_webt_read_events,
            .state = box_audio_webt_state,
            .update = box_audio_webt_update,
            .free = box_audio_webt_free,
            .stats = box_audio_webt_stats,
            .clock = box_audio_webt_clock,
            .capture = box_audio_webt_capture,
            .capture_take = box_audio_webt_capture_take
        };
        result = &w->base;
    }

    return result;
}

#else

box_audio_transport_t* box_audio_transport_web_new(void)
{
    return NULL;
}

#endif
