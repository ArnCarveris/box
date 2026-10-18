/**
 * @file render/ring.c
 * @brief Packet ring: the main thread fills packets in order, the consumer renders them in order.
 *
 * Packets are handed out by sequence number, so a packet goes from acquired to submitted to
 * taken to released. The consumer is the render thread, or the main thread inside submit.
 */

#include "render.h"
#include <math.h>
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#include <sys/qos.h>
#endif

/** Refreshes of a cadence phase below a whole refresh that still count as reaching it */
#define BOX_RENDER_CADENCE_EPSILON (1e-6)

/** Seconds a wait for a GPU fence may take while the GPU already finished the frame, for a
 * backend that cannot tell whether a fence is done without waiting */
#define BOX_RENDER_GPU_LATE (0.0002)

/** A frame on the simulated display that is not shown yet */
typedef struct box_render_shown_t {
    bool active;        /**< A frame waits to be shown */
    uint64_t fence;     /**< Fence of the GPU work of the frame, 0 for none */
    double ready;       /**< Seconds when the CPU submitted the frame */
    float delta;        /**< Delta of the frame */
    int32_t presents;   /**< Refreshes the frame is shown for */
} box_render_shown_t;

/** State of the packet ring and its consumer */
struct box_render_ring_t {
    box_render_packet_t packets[BOX_RENDER_IN_FLIGHT_MAX]; /**< Packet storage */
    int32_t count;                  /**< Packets in use, at most BOX_RENDER_IN_FLIGHT_MAX */
    bool main_thread;               /**< The main thread renders in submit */
    RenderSettings settings;        /**< Settings to apply before the next acquire */
    bool dirty;                     /**< The settings changed since they were applied */
    box_render_backend_t backend;   /**< The backend */
    bool backend_set;               /**< The backend was replaced since the ring was created */
    bool backend_ready;             /**< The backend ran init and not yet fini */
    bool backend_failed;            /**< The init of the backend failed */
    bool running;                   /**< The render thread exists */
    ecs_os_thread_t thread;         /**< The render thread */
    ecs_os_mutex_t lock;            /**< Protects the sequence numbers and the flags below */
    ecs_os_cond_t can_write;        /**< Signalled when a packet is released */
    ecs_os_cond_t can_read;         /**< Signalled when a packet is submitted or on stop */
    int64_t base;                   /**< Sequence number of the first slot of the ring */
    int64_t acquired;               /**< Packets handed to the main thread */
    int64_t submitted;              /**< Packets handed to the consumer */
    int64_t taken;                  /**< Packets the consumer started on */
    int64_t released;               /**< Packets the consumer finished */
    int32_t stalls;                 /**< Acquires that waited for a free packet */
    int32_t waits;                  /**< Frame starts that waited for a free packet */
    bool stop;                      /**< The render thread exits once it took every packet */
    bool paused;                    /**< The render thread takes no packets */
    bool flushing;                  /**< A drain ignores the pause */
    int64_t flushes;                /**< Drains that asked the backend to flush */
    int64_t flushed;                /**< Flushes the render thread completed */
    float display_rate;             /**< Refresh rate of the simulated display, applied */
    float display_actual;           /**< Rate the simulated display runs at, applied */
    bool vsync;                     /**< The last submitted packet presents with vsync */
    float refresh;                  /**< Refresh rate of the display of the last packet */
    int32_t swap_interval;          /**< Refreshes the last submitted packet is shown for */
    int32_t cadence;                /**< Frames after which the swap intervals repeat */
    double phase;                   /**< Refreshes owed by the cadence of the submits */
    float cadence_refresh;          /**< Refresh rate the phase is counted at */
    float cadence_fps;              /**< Frame rate cap the phase is counted for */
    double origin;                  /**< Time of a refresh of the simulated display, 0 unset */
    double shown;                   /**< Refresh the last present is shown at, 0 for none */
    double frame_shown;             /**< Time the last packet was first shown, 0 for none */
    double planned;                 /**< Seconds the last shown packet is planned to show */
    int32_t presented;              /**< Packets presented */
    double present_ms;              /**< Interval between the last two presented packets */
    int32_t presents_late;          /**< Present intervals over 1.1 frame intervals */
    int32_t presents_early;         /**< Present intervals under 0.9 frame intervals */
    int32_t delta_mismatches;       /**< Shown intervals that differ from the packet delta */
    int32_t device_losses;          /**< Backend restarts after the device was lost */
    int32_t generation;             /**< Generation of the backend state, which the acquire
                                     *   publishes in backend.generation for the extract */
    box_render_shown_t pending;     /**< Frame on the simulated display not yet shown */
};

/** Read the clock the frame loop measures with.
 *
 * @return Seconds on the clock of ecs_os_get_time
 */
static double box_render_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Get the packet of a sequence number.
 *
 * @param ring The ring
 * @param seq The sequence number
 * @return The packet
 */
static box_render_packet_t* box_render_ring_slot(
    box_render_ring_t *ring,
    int64_t seq)
{
    return &ring->packets[(seq - ring->base) % ring->count];
}

int32_t box_render_swap_interval(
    float refresh,
    float fps)
{
    int32_t result = 0;
    double whole;
    if (refresh <= 0) {
        goto done;
    }

    if (fps <= 0 || (double)fps >= (double)refresh - BOX_RENDER_REFRESH_SLACK) {
        result = 1;
        goto done;
    }

    whole = floor((double)refresh / (double)fps + 0.5);
    if (fabs(whole * (double)fps - (double)refresh) <= BOX_RENDER_REFRESH_SLACK) {
        result = (int32_t)whole;
    }

done:
    return result;
}

int32_t box_render_cadence_next(
    double *phase,
    float refresh,
    float fps)
{
    int32_t result = box_render_swap_interval(refresh, fps);
    if (result || refresh <= 0) {
        *phase = 0;
        goto done;
    }

    *phase += (double)refresh / (double)fps;
    result = (int32_t)floor(*phase + BOX_RENDER_CADENCE_EPSILON);
    *phase -= (double)result;
done:
    return result;
}

int32_t box_render_cadence_cycle(
    float refresh,
    float fps)
{
    int32_t result = box_render_swap_interval(refresh, fps) ? 1 : 0;
    double ratio, owed;
    int32_t frames;
    if (result || refresh <= 0) {
        goto done;
    }

    ratio = (double)refresh / (double)fps;
    for (frames = 2; frames <= BOX_RENDER_CADENCE_MAX && !result; frames ++) {
        owed = ratio * (double)frames;
        if (fabs(owed - floor(owed + 0.5)) < BOX_RENDER_CADENCE_EPSILON * (double)frames) {
            result = frames;
        }
    }

done:
    return result;
}

/** Seconds between the refreshes of the simulated display: the actual rate when set, else the
 * reported one.
 *
 * @param ring The ring
 * @return The period
 */
static double box_render_display_period(
    box_render_ring_t *ring)
{
    float rate = ring->display_actual > 0 ? ring->display_actual : ring->display_rate;
    return 1.0 / (double)rate;
}

/** Find the refresh of the simulated display that first shows a frame that is ready at a time:
 * the first refresh after it, and not before the refresh after the last present.
 *
 * The refreshes start half a period before the first frame is ready, as a real display has a
 * phase of its own: a loop that paces a cap by its clock submits whole refreshes after the first
 * frame, so each frame would be ready right at a refresh and jitter would pick either side.
 *
 * @param ring The ring
 * @param ready Seconds when the frame is ready
 * @return Seconds of the refresh
 */
static double box_render_display_time(
    box_render_ring_t *ring,
    double ready)
{
    double period = box_render_display_period(ring);
    double result;
    if (ring->origin == 0) {
        ring->origin = ready - period * 0.5;
    }

    result = ring->origin + ceil((ready - ring->origin) / period - 1e-6) * period;
    if (ring->shown > 0 && result < ring->shown + period * 0.5) {
        result = ring->shown + period;
    }

    return result;
}

/** Count a presented packet and check its interval against the interval planned for the frame
 * before it and against its delta.
 *
 * @param ring The ring
 * @param delta Delta of the packet
 * @param planned Seconds the packet is planned to show, the expected interval to the next
 * @param shown Seconds when the packet is first shown
 */
static void box_render_count_present(
    box_render_ring_t *ring,
    float delta,
    double planned,
    double shown)
{
    double expected = ring->planned;
    ecs_os_mutex_lock(ring->lock);
    if (ring->frame_shown > 0) {
        double interval = shown - ring->frame_shown;
        ring->present_ms = interval * 1000.0;
        if (expected > 0 && interval > expected * (1.0 + BOX_RENDER_PRESENT_TOLERANCE)) {
            ring->presents_late ++;
        } else if (expected > 0 && interval < expected * (1.0 - BOX_RENDER_PRESENT_TOLERANCE)) {
            ring->presents_early ++;
        }

        ring->delta_mismatches += ring->display_rate > 0 &&
            fabs((double)delta - interval) * 1000.0 > BOX_FRAME_TOLERANCE_MS;
    }

    ring->presented ++;
    ring->frame_shown = shown;
    ring->planned = planned;
    ecs_os_mutex_unlock(ring->lock);
}

/** Show the pending frame of the simulated display at the first free refresh after its submit,
 * once the display reaches it. A GPU that finishes the frame later repeats the frame before
 * until the refresh after it finished.
 *
 * The frame then stays for its refreshes: in a window the clock of the frame loop keeps the next
 * frame that far away, here the display holds it, so a loop without box_app_run is paced alike.
 *
 * The wait frees an image of the three image swap chain for the next frame.
 *
 * @param ring The ring
 */
static void box_render_show_pending(
    box_render_ring_t *ring)
{
    box_render_shown_t *frame = &ring->pending;
    double period, shown, before, after;
    bool late;
    if (!frame->active) {
        goto done;
    }

    period = box_render_display_period(ring);
    shown = box_render_display_time(ring, frame->ready);
    box_wait_until(shown);
    if (frame->fence && ring->backend_ready && ring->backend.wait) {
        before = box_render_now();
        late = ring->backend.done && !ring->backend.done(ring->backend.ctx, frame->fence);
        ring->backend.wait(ring->backend.ctx, frame->fence);
        after = box_render_now();
        late = ring->backend.done ? late : after - before > BOX_RENDER_GPU_LATE;
        if (late) {
            shown = box_render_display_time(ring, after);
        }
    }

    box_render_count_present(ring, frame->delta, (double)frame->presents * period, shown);
    ring->shown = shown + (double)(frame->presents - 1) * period;
    frame->active = false;
done:
    return;
}

/** Hand a packet to the backend and present it. On a simulated display the frame becomes
 * pending and the frame before it is shown, so the CPU renders a frame while the one before
 * waits for its refresh and its GPU work, like a FIFO swap chain of three images.
 *
 * @param ring The ring
 * @param p The packet
 */
static void box_render_consume(
    box_render_ring_t *ring,
    const box_render_packet_t *p)
{
    box_render_shown_t frame = {0};
    if (ring->backend_ready && ring->backend.render) {
        ring->backend.render(ring->backend.ctx, p);
    }

    if (ring->display_rate <= 0) {
        box_render_count_present(ring, p->delta, p->fps > 0 ? 1.0 / (double)p->fps : 0,
            box_render_now());
        goto done;
    }

    frame.active = true;
    frame.fence = ring->backend_ready && ring->backend.fence
        ? ring->backend.fence(ring->backend.ctx)
        : 0;
    frame.ready = box_render_now();
    frame.delta = p->delta;
    frame.presents = p->presents > 0 ? p->presents : 1;
    box_render_show_pending(ring);
    ring->pending = frame;
done:
    return;
}

/** Run the init of the backend on the calling thread.
 *
 * @param ring The ring
 */
static void box_render_backend_start(
    box_render_ring_t *ring)
{
    if (ring->backend.init && ring->backend.init(ring->backend.ctx)) {
        ecs_err("render backend failed to initialize, packets are dropped");
        ring->backend_failed = true;
        goto done;
    }

    ring->backend_ready = true;
done:
    return;
}

/** Show the pending frame of a simulated display and run the fini of the backend on the
 * calling thread when its init ran, so no fence outlives the device it belongs to.
 *
 * A stop after an init, whether it succeeded or failed, bumps the generation of the backend:
 * the next acquire publishes it, so its extract sends everything again, and packets built for
 * the previous generation are dropped instead of rendered.
 *
 * @param ring The ring
 */
static void box_render_backend_stop(
    box_render_ring_t *ring)
{
    box_render_show_pending(ring);
    ring->pending.active = false;
    if (ring->backend_ready && ring->backend.flush) {
        ring->backend.flush(ring->backend.ctx);
    }

    if (ring->backend_ready && ring->backend.fini) {
        ring->backend.fini(ring->backend.ctx);
    }

    if (ring->backend_ready || ring->backend_failed) {
        ecs_os_mutex_lock(ring->lock);
        ring->generation ++;
        ecs_os_mutex_unlock(ring->lock);
    }

    ring->backend_ready = false;
    ring->backend_failed = false;
}

/** Restart a backend that lost its device on the thread that renders, before its next packet,
 * as wgpu and Dawn applications do from the device lost callback: the stop bumps the
 * generation, so the extract sends the whole scene to the new device.
 *
 * A failed init drops the packets that follow, as a failed first init does.
 *
 * @param ring The ring
 */
static void box_render_backend_recover(
    box_render_ring_t *ring)
{
    if (!ring->backend_ready || !ring->backend.lost || !ring->backend.lost(ring->backend.ctx)) {
        goto done;
    }

    box_render_backend_stop(ring);
    box_render_backend_start(ring);
    ecs_os_mutex_lock(ring->lock);
    ring->device_losses += ring->backend_ready;
    ecs_os_mutex_unlock(ring->lock);
done:
    return;
}

/** Recover a lost backend and render a packet unless it was built for an older generation of
 * the backend, whose resources the backend no longer has.
 *
 * @param ring The ring
 * @param p The packet
 */
static void box_render_take(
    box_render_ring_t *ring,
    const box_render_packet_t *p)
{
    bool current;
    box_render_backend_recover(ring);
    ecs_os_mutex_lock(ring->lock);
    current = p->generation == ring->generation;
    ecs_os_mutex_unlock(ring->lock);
    if (current) {
        box_render_consume(ring, p);
    }
}

/** Check whether the render thread may take a packet, called with the lock held.
 *
 * @param ring The ring
 * @return Whether a packet is submitted and the ring is not held
 */
static bool box_render_ring_readable(
    const box_render_ring_t *ring)
{
    bool held = ring->paused && !ring->stop && !ring->flushing;

    return ring->taken < ring->submitted && !held;
}

/** Render packets until the ring stops and every submitted packet is rendered, and flush the
 * backend when a drain waits for it after the last packet. On macOS the thread runs at the user
 * interactive QoS, as Apple recommends for threads that render frames.
 *
 * @param arg The ring
 * @return NULL
 */
static void* box_render_thread_main(
    void *arg)
{
    box_render_ring_t *ring = arg;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    box_render_backend_start(ring);
    ecs_os_mutex_lock(ring->lock);
    while (true) {
        while (!box_render_ring_readable(ring) && !(ring->taken == ring->submitted &&
            (ring->stop || ring->flushed < ring->flushes)))
        {
            ecs_os_cond_wait(ring->can_read, ring->lock);
        }

        if (ring->taken == ring->submitted && ring->flushed < ring->flushes) {
            int64_t flush = ring->flushes;
            ecs_os_mutex_unlock(ring->lock);
            if (ring->backend_ready && ring->backend.flush) {
                ring->backend.flush(ring->backend.ctx);
            }

            ecs_os_mutex_lock(ring->lock);
            ring->flushed = flush;
            ecs_os_cond_broadcast(ring->can_write);
            continue;
        }

        if (ring->taken == ring->submitted) {
            break;
        }

        box_render_packet_t *p = box_render_ring_slot(ring, ring->taken ++);
        ecs_os_mutex_unlock(ring->lock);
        box_render_take(ring, p);
        ecs_os_mutex_lock(ring->lock);
        ring->released ++;
        ecs_os_cond_broadcast(ring->can_write);
    }

    ecs_os_mutex_unlock(ring->lock);
    box_render_backend_stop(ring);

    return NULL;
}

/** Start the render thread when the packets go to one and it is not running.
 *
 * @param ring The ring
 */
static void box_render_ring_start(
    box_render_ring_t *ring)
{
    if (ring->main_thread || ring->running) {
        goto done;
    }

    ring->stop = false;
    ring->running = true;
    ring->thread = ecs_os_thread_new(box_render_thread_main, ring);
done:
    return;
}

/** Render every submitted packet, join the render thread and run the fini of the backend.
 *
 * @param ring The ring
 */
static void box_render_ring_stop(
    box_render_ring_t *ring)
{
    if (!ring->running) {
        box_render_backend_stop(ring);
        goto done;
    }

    ecs_os_mutex_lock(ring->lock);
    ring->stop = true;
    ecs_os_cond_broadcast(ring->can_read);
    ecs_os_mutex_unlock(ring->lock);
    ecs_os_thread_join(ring->thread);
    ring->running = false;
    ring->stop = false;
done:
    return;
}

/** Wait until every submitted packet is rendered and the backend flushed, also while the
 * ring is held.
 *
 * @param ring The ring
 */
static void box_render_ring_drain(
    box_render_ring_t *ring)
{
    if (!ring->running) {
        goto done;
    }

    ecs_os_mutex_lock(ring->lock);
    ring->flushing = true;
    int64_t flush = ++ ring->flushes;
    ecs_os_cond_broadcast(ring->can_read);
    while (ring->released < ring->submitted || ring->flushed < flush) {
        ecs_os_cond_wait(ring->can_write, ring->lock);
    }

    ring->flushing = false;
    ecs_os_mutex_unlock(ring->lock);
done:
    return;
}

/** Apply changed settings while no packet is acquired.
 *
 * A new packet count drains the ring and keeps the backend, and releases the storage of the
 * slots a lower count no longer uses; moving the rendering to or from the main thread also
 * restarts the backend on its new thread.
 *
 * @param ring The ring
 */
static void box_render_ring_apply(
    box_render_ring_t *ring)
{
    int32_t i, count = ring->settings.in_flight;
    bool main_thread = ring->settings.main_thread || !ecs_os_has_threading();
    if (count < 1) {
        count = 1;
    }

    if (count > BOX_RENDER_IN_FLIGHT_MAX) {
        count = BOX_RENDER_IN_FLIGHT_MAX;
    }

    if (main_thread != ring->main_thread) {
        box_render_ring_stop(ring);
    } else {
        box_render_ring_drain(ring);
    }

    for (i = count; i < ring->count; i ++) {
        box_render_packet_fini(&ring->packets[i]);
        box_render_packet_init(&ring->packets[i]);
    }

    ecs_os_mutex_lock(ring->lock);
    ring->base = ring->released;
    ring->count = count;
    if (ring->display_rate != ring->settings.display_rate ||
        ring->display_actual != ring->settings.display_actual)
    {
        ring->display_rate = ring->settings.display_rate;
        ring->display_actual = ring->settings.display_actual;
        ring->origin = 0;
        ring->shown = 0;
        ring->frame_shown = 0;
        ring->planned = 0;
        ring->pending.active = false;
    }

    ecs_os_mutex_unlock(ring->lock);
    ring->main_thread = main_thread;
    ring->dirty = false;
}

box_render_ring_t* box_render_ring_new(void)
{
    box_render_ring_t *result = ecs_os_calloc_t(box_render_ring_t);
    int32_t i;
    for (i = 0; i < BOX_RENDER_IN_FLIGHT_MAX; i ++) {
        box_render_packet_init(&result->packets[i]);
    }

    result->count = BOX_RENDER_IN_FLIGHT;
    result->main_thread = !ecs_os_has_threading();
    result->lock = ecs_os_mutex_new();
    result->can_write = ecs_os_cond_new();
    result->can_read = ecs_os_cond_new();
    box_render_null_t *null = box_render_null_new();
    result->backend = box_render_null_backend(null);
    result->backend.free = box_render_null_free;

    return result;
}

void box_render_ring_free(
    box_render_ring_t *ring)
{
    int32_t i;
    box_render_ring_stop(ring);
    if (ring->backend.free) {
        ring->backend.free(ring->backend.ctx);
    }

    for (i = 0; i < BOX_RENDER_IN_FLIGHT_MAX; i ++) {
        box_render_packet_fini(&ring->packets[i]);
    }

    ecs_os_cond_free(ring->can_read);
    ecs_os_cond_free(ring->can_write);
    ecs_os_mutex_free(ring->lock);
    ecs_os_free(ring);
}

void box_render_ring_configure(
    box_render_ring_t *ring,
    const RenderSettings *settings)
{
    ring->settings = *settings;
    ring->dirty = true;
}

box_render_packet_t* box_render_ring_acquire(
    box_render_ring_t *ring)
{
    box_render_packet_t *result;
    if (ring->dirty) {
        box_render_ring_apply(ring);
    }

    box_render_ring_start(ring);
    ecs_os_mutex_lock(ring->lock);
    if (ring->acquired - ring->released >= ring->count) {
        ring->stalls ++;
    }

    while (ring->acquired - ring->released >= ring->count) {
        ecs_os_cond_wait(ring->can_write, ring->lock);
    }

    result = box_render_ring_slot(ring, ring->acquired ++);
    ring->backend.generation = ring->generation;
    ecs_os_mutex_unlock(ring->lock);
    box_render_packet_reset(result);
    result->generation = ring->backend.generation;

    return result;
}

void box_render_ring_wait(
    box_render_ring_t *ring)
{
    if (ring->dirty || ring->main_thread) {
        goto done;
    }

    ecs_os_mutex_lock(ring->lock);
    if (ring->acquired == ring->submitted && ring->acquired - ring->released >= ring->count) {
        ring->waits ++;
    }

    while (ring->acquired == ring->submitted && ring->acquired - ring->released >= ring->count) {
        ecs_os_cond_wait(ring->can_write, ring->lock);
    }

    ecs_os_mutex_unlock(ring->lock);
done:
    return;
}

box_render_packet_t* box_render_ring_current(
    box_render_ring_t *ring)
{
    box_render_packet_t *result = NULL;
    if (ring->acquired > ring->submitted) {
        result = box_render_ring_slot(ring, ring->submitted);
    }

    return result;
}

void box_render_ring_submit(
    box_render_ring_t *ring)
{
    box_render_packet_t *p = box_render_ring_current(ring);
    if (!p) {
        goto done;
    }

    ring->vsync = p->vsync || ring->display_rate > 0;
    ring->refresh = ring->display_rate > 0 ? ring->display_rate : p->refresh;
    if (ring->refresh != ring->cadence_refresh || p->fps != ring->cadence_fps) {
        ring->phase = 0;
        ring->cadence_refresh = ring->refresh;
        ring->cadence_fps = p->fps;
    }

    p->presents = ring->vsync ? box_render_cadence_next(&ring->phase, ring->refresh, p->fps) : 0;
    ring->swap_interval = p->presents;
    ring->cadence = ring->vsync ? box_render_cadence_cycle(ring->refresh, p->fps) : 0;
    if (!ring->main_thread) {
        box_render_ring_start(ring);
        ecs_os_mutex_lock(ring->lock);
        ring->submitted ++;
        ecs_os_cond_signal(ring->can_read);
        ecs_os_mutex_unlock(ring->lock);
        goto done;
    }

    if (!ring->backend_ready && !ring->backend_failed) {
        box_render_backend_start(ring);
    }

    ecs_os_mutex_lock(ring->lock);
    ring->submitted ++;
    ring->taken ++;
    ecs_os_mutex_unlock(ring->lock);
    box_render_take(ring, p);
    ecs_os_mutex_lock(ring->lock);
    ring->released ++;
    ecs_os_mutex_unlock(ring->lock);
done:
    return;
}

void box_render_ring_set_backend(
    box_render_ring_t *ring,
    const box_render_backend_t *backend)
{
    box_render_ring_stop(ring);
    if (ring->backend.free) {
        ring->backend.free(ring->backend.ctx);
    }

    ecs_os_mutex_lock(ring->lock);
    ring->generation ++;
    ring->backend = *backend;
    ring->backend.generation = ring->generation;
    ecs_os_mutex_unlock(ring->lock);
    ring->backend_set = true;
}

const box_render_backend_t* box_render_ring_backend(
    const box_render_ring_t *ring)
{
    return &ring->backend;
}

bool box_render_ring_is_default(
    const box_render_ring_t *ring)
{
    return !ring->backend_set;
}

void box_render_ring_pause(
    box_render_ring_t *ring,
    bool paused)
{
    ecs_os_mutex_lock(ring->lock);
    ring->paused = paused;
    ecs_os_cond_broadcast(ring->can_read);
    ecs_os_mutex_unlock(ring->lock);
}

void box_render_ring_sync(
    box_render_ring_t *ring)
{
    box_render_ring_drain(ring);
    if (ring->main_thread && ring->backend_ready && ring->backend.flush) {
        ring->backend.flush(ring->backend.ctx);
    }
}

void box_render_ring_status(
    box_render_ring_t *ring,
    RenderStatus *status)
{
    ecs_os_mutex_lock(ring->lock);
    status->submitted = (int32_t)ring->submitted;
    status->rendered = (int32_t)ring->released;
    status->in_flight = (int32_t)(ring->acquired - ring->released);
    status->stalls = ring->stalls;
    status->waits = ring->waits;
    status->threaded = !ring->main_thread;
    status->vsync = ring->vsync;
    status->refresh_rate = ring->refresh;
    status->swap_interval = ring->swap_interval;
    status->cadence = ring->cadence;
    status->presented = ring->presented;
    status->present_ms = ring->present_ms;
    status->presents_late = ring->presents_late;
    status->presents_early = ring->presents_early;
    status->delta_mismatches = ring->delta_mismatches;
    status->device_losses = ring->device_losses;
    ecs_os_mutex_unlock(ring->lock);
}
