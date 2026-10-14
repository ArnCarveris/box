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

/** Relative difference of the refresh rate from a whole multiple of the cap that still counts */
#define BOX_RENDER_SWAP_SLACK (0.01f)

/** Frames whose GPU work may be in flight on a simulated display, like a three image swap chain */
#define BOX_RENDER_GPU_FRAMES (2)

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
    bool stop;                      /**< The render thread exits once it took every packet */
    bool paused;                    /**< The render thread takes no packets */
    bool flushing;                  /**< A drain ignores the pause */
    float display_rate;             /**< Refresh rate of the simulated display, applied */
    bool vsync;                     /**< The last submitted packet presents with vsync */
    float refresh;                  /**< Refresh rate of the display of the last packet */
    int32_t swap_interval;          /**< Swap interval of the last submitted packet */
    double origin;                  /**< Time of a refresh of the simulated display, 0 unset */
    double shown;                   /**< Refresh the last present is shown at, 0 for none */
    double frame_shown;             /**< Time the last packet was first shown, 0 for none */
    int32_t presented;              /**< Packets presented */
    double present_ms;              /**< Interval between the last two presented packets */
    int32_t presents_late;          /**< Present intervals over 1.1 frame intervals */
    int32_t presents_early;         /**< Present intervals under 0.9 frame intervals */
    int32_t delta_mismatches;       /**< Shown intervals that differ from the packet delta */
    uint64_t fences[BOX_RENDER_GPU_FRAMES]; /**< GPU fences of the last frames, oldest first */
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

/** Run the fini of the backend on the calling thread when its init ran.
 *
 * @param ring The ring
 */
static void box_render_backend_stop(
    box_render_ring_t *ring)
{
    if (ring->backend_ready && ring->backend.fini) {
        ring->backend.fini(ring->backend.ctx);
    }

    ring->backend_ready = false;
    ring->backend_failed = false;
}

int32_t box_render_swap_interval(
    float refresh,
    float fps)
{
    int32_t result = 0;
    double ratio, whole;
    if (refresh <= 0) {
        goto done;
    }

    if (fps <= 0 || fps >= refresh * (1.0f - BOX_RENDER_SWAP_SLACK)) {
        result = 1;
        goto done;
    }

    ratio = (double)refresh / (double)fps;
    whole = floor(ratio + 0.5);
    if (fabs(ratio - whole) <= ratio * BOX_RENDER_SWAP_SLACK) {
        result = (int32_t)whole;
    }

done:
    return result;
}

/** Find the refresh of the simulated display that first shows a frame that is ready now: the
 * first refresh after it, and not before the refresh after the last present.
 *
 * @param ring The ring
 * @param now Seconds when the frame is ready
 * @return Seconds of the refresh
 */
static double box_render_display_time(
    box_render_ring_t *ring,
    double now)
{
    double period = 1.0 / (double)ring->display_rate;
    double result;
    if (ring->origin == 0) {
        ring->origin = now;
    }

    result = ring->origin + ceil((now - ring->origin) / period - 1e-6) * period;
    if (ring->shown > 0 && result < ring->shown + period * 0.5) {
        result = ring->shown + period;
    }

    return result;
}

/** Count a presented packet and check its interval against its frame interval and its delta.
 *
 * @param ring The ring
 * @param p The packet
 * @param shown Seconds when the packet is first shown
 */
static void box_render_count_present(
    box_render_ring_t *ring,
    const box_render_packet_t *p,
    double shown)
{
    double expected = p->fps > 0 ? 1.0 / (double)p->fps : 0;
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
            fabs((double)p->delta - interval) * 1000.0 > BOX_FRAME_TOLERANCE_MS;
    }

    ring->presented ++;
    ring->frame_shown = shown;
    ecs_os_mutex_unlock(ring->lock);
}

/** Wait until the GPU finished the oldest frame that may be in flight on a simulated display,
 * so the GPU falls behind the presents by at most BOX_RENDER_GPU_FRAMES frames.
 *
 * @param ring The ring
 */
static void box_render_gpu_backpressure(
    box_render_ring_t *ring)
{
    uint64_t oldest = ring->fences[0];
    if (oldest && ring->backend_ready && ring->backend.wait) {
        ring->backend.wait(ring->backend.ctx, oldest);
    }
}

/** Remember the GPU fence of the frame the backend just rendered.
 *
 * @param ring The ring
 */
static void box_render_gpu_fence(
    box_render_ring_t *ring)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_GPU_FRAMES - 1; i ++) {
        ring->fences[i] = ring->fences[i + 1];
    }

    ring->fences[BOX_RENDER_GPU_FRAMES - 1] = ring->backend_ready && ring->backend.fence
        ? ring->backend.fence(ring->backend.ctx)
        : 0;
}

/** Hand a packet to the backend and present it. On a simulated display the frame is shown at
 * the first free refresh after its submit for swap interval refreshes, the GPU stays at most
 * two frames behind and the consumer blocks while a present is queued, like a FIFO.
 *
 * @param ring The ring
 * @param p The packet
 */
static void box_render_consume(
    box_render_ring_t *ring,
    const box_render_packet_t *p)
{
    int32_t swap = box_render_swap_interval(ring->display_rate, p->fps);
    double period, shown;
    if (ring->display_rate > 0) {
        box_render_gpu_backpressure(ring);
    }

    if (ring->backend_ready && ring->backend.render) {
        ring->backend.render(ring->backend.ctx, p);
    }

    if (ring->display_rate <= 0) {
        box_render_count_present(ring, p, box_render_now());
        goto done;
    }

    box_render_gpu_fence(ring);
    period = 1.0 / (double)ring->display_rate;
    shown = box_render_display_time(ring, box_render_now());
    box_render_count_present(ring, p, shown);
    ring->shown = shown + (double)((swap > 1 ? swap : 1) - 1) * period;
    box_wait_until(ring->shown - period);
done:
    return;
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

/** Render packets until the ring stops and every submitted packet is rendered. On macOS the
 * thread runs at the user interactive QoS, as Apple recommends for threads that render frames.
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
        while (!box_render_ring_readable(ring) && !(ring->stop && ring->taken == ring->submitted)) {
            ecs_os_cond_wait(ring->can_read, ring->lock);
        }

        if (ring->taken == ring->submitted) {
            break;
        }

        box_render_packet_t *p = box_render_ring_slot(ring, ring->taken ++);
        ecs_os_mutex_unlock(ring->lock);
        box_render_consume(ring, p);
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

/** Wait until every submitted packet is rendered, also while the ring is held.
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
    ecs_os_cond_broadcast(ring->can_read);
    while (ring->released < ring->submitted) {
        ecs_os_cond_wait(ring->can_write, ring->lock);
    }

    ring->flushing = false;
    ecs_os_mutex_unlock(ring->lock);
done:
    return;
}

/** Apply changed settings while no packet is acquired.
 *
 * A new packet count drains the ring and keeps the backend; moving the rendering to or from
 * the main thread also restarts the backend on its new thread.
 *
 * @param ring The ring
 */
static void box_render_ring_apply(
    box_render_ring_t *ring)
{
    int32_t count = ring->settings.in_flight;
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

    ecs_os_mutex_lock(ring->lock);
    ring->base = ring->released;
    ring->count = count;
    if (ring->display_rate != ring->settings.display_rate) {
        ring->display_rate = ring->settings.display_rate;
        ring->origin = 0;
        ring->shown = 0;
        ring->frame_shown = 0;
        ecs_os_memset_n(ring->fences, 0, uint64_t, BOX_RENDER_GPU_FRAMES);
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
    ecs_os_mutex_unlock(ring->lock);
    box_render_packet_reset(result);

    return result;
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
    ring->swap_interval = ring->vsync ? box_render_swap_interval(ring->refresh, p->fps) : 0;
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
    box_render_consume(ring, p);
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

    ring->backend = *backend;
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
    status->threaded = !ring->main_thread;
    status->vsync = ring->vsync;
    status->refresh_rate = ring->refresh;
    status->swap_interval = ring->swap_interval;
    status->presented = ring->presented;
    status->present_ms = ring->present_ms;
    status->presents_late = ring->presents_late;
    status->presents_early = ring->presents_early;
    status->delta_mismatches = ring->delta_mismatches;
    ecs_os_mutex_unlock(ring->lock);
}
