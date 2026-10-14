/**
 * @file render/ring.c
 * @brief Packet ring: the main thread fills packets in order, the consumer renders them in order.
 *
 * Packets are handed out by sequence number, so a packet goes from acquired to submitted to
 * taken to released. The consumer is the render thread, or the main thread inside submit.
 */

#include "render.h"

/** State of the packet ring and its consumer */
struct box_render_ring_t {
    box_render_packet_t packets[BOX_RENDER_IN_FLIGHT_MAX]; /**< Packet storage */
    int32_t count;                  /**< Packets in use, at most BOX_RENDER_IN_FLIGHT_MAX */
    bool main_thread;               /**< The main thread renders in submit */
    RenderSettings settings;        /**< Settings to apply before the next acquire */
    bool dirty;                     /**< The settings changed since they were applied */
    box_render_backend_t backend;   /**< The backend */
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
};

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

/** Hand a packet to the backend.
 *
 * @param ring The ring
 * @param p The packet
 */
static void box_render_consume(
    box_render_ring_t *ring,
    const box_render_packet_t *p)
{
    if (ring->backend_ready && ring->backend.render) {
        ring->backend.render(ring->backend.ctx, p);
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

/** Render packets until the ring stops and every submitted packet is rendered.
 *
 * @param arg The ring
 * @return NULL
 */
static void* box_render_thread_main(
    void *arg)
{
    box_render_ring_t *ring = arg;
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
    if (count <= 0) {
        count = BOX_RENDER_IN_FLIGHT;
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
    ecs_os_mutex_unlock(ring->lock);
}
