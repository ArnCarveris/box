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

void box_audio_host_fini(
    box_audio_host_t *host)
{
    uint32_t i;
    for (i = 0; i < BOX_AUDIO_SOUND_MAX; i ++) {
        box_audio_host_sound_free(host, i);
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
    int32_t count, i;
    while (frames > 0) {
        int32_t block = frames < BOX_AUDIO_TRANSPORT_FRAMES ? frames : BOX_AUDIO_TRANSPORT_FRAMES;
        box_audio_core_render(n->core, n->left, n->right, block);
        if (n->output) {
            n->output(n->left, n->right, block, n->ctx);
        }

        frames -= block;
    }

    do {
        count = box_audio_core_read_events(n->core, events, 64);
        for (i = 0; i < count; i ++) {
            box_audio_host_event(&n->host, &events[i]);
        }
    } while (count == 64);
}

/** Free the core, then the sounds.
 *
 * @param t The transport
 */
static void box_audio_null_free(
    box_audio_transport_t *t)
{
    box_audio_null_t *n = (box_audio_null_t*)t;
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
        .read_events = box_audio_null_read_events,
        .state = box_audio_null_state,
        .update = box_audio_null_update,
        .free = box_audio_null_free
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
    if (!audio || audio.failed) {
        return audio ? 1 : 2;
    }
    return audio.node && audio.ctx.state === 'running' ? 3 : 2;
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

box_audio_transport_t* box_audio_transport_web_new(void)
{
    box_audio_transport_t *result = ecs_os_calloc_t(box_audio_transport_t);
    if (result) {
        *result = (box_audio_transport_t){
            .kind = BOX_AUDIO_TRANSPORT_WEB,
            .commands = box_audio_webt_commands,
            .sound = box_audio_webt_sound,
            .read_events = box_audio_webt_read_events,
            .state = box_audio_webt_state,
            .update = box_audio_webt_update,
            .free = box_audio_webt_free
        };
    }

    return result;
}

#else

box_audio_transport_t* box_audio_transport_web_new(void)
{
    return NULL;
}

#endif
