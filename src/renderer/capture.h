/**
 * @file renderer/capture.h
 * @brief Capture slots, the encoder worker and the capture functions of the renderer.
 */

#ifndef BOX_RENDERER_CAPTURE_H
#define BOX_RENDERER_CAPTURE_H

#include "gpu.h"
#include "packet.h"
#ifndef BOX_PLATFORM_WEB
#include <pthread.h>
#endif

typedef struct box_renderer_t box_renderer_t;

/** Captures that can wait for their buffer map at the same time */
#define BOX_CAPTURE_PENDING_MAX (3)

/** Frames a capture waits for its map before the renderer blocks on it */
#define BOX_CAPTURE_MAP_DELAY (3)

/** A capture slot whose buffer receives a copy of the frame target */
typedef struct box_capture_t {
    WGPUBuffer buffer;               /**< Mappable copy of the frame target, kept for reuse */
    uint64_t size;                   /**< Bytes the buffer holds */
    uint32_t width;                  /**< Image width in pixels */
    uint32_t height;                 /**< Image height in pixels */
    uint32_t stride;                 /**< Bytes per row in the buffer */
    bool bgra;                       /**< Rows need a BGRA to RGBA swap */
    uint32_t wait;                   /**< Frames the capture has waited for its map */
    bool busy;                       /**< Slot holds a capture in flight */
    char path[BOX_CAPTURE_PATH_MAX]; /**< PNG the capture writes */
} box_capture_t;

#ifndef BOX_PLATFORM_WEB
/** Encodes that can wait for the worker before a new capture waits for them */
#define BOX_CAPTURE_JOBS_MAX (4)

/** Frames the worker may hold jobs before the renderer waits for it to finish them */
#define BOX_CAPTURE_ENCODE_DELAY (8)

/** Pixels of a mapped capture that the worker encodes */
typedef struct box_capture_job_t {
    struct box_capture_job_t *next;  /**< Next job in the queue */
    uint8_t *pixels;                 /**< Rows copied out of the mapped buffer */
    uint32_t width;                  /**< Image width in pixels */
    uint32_t height;                 /**< Image height in pixels */
    uint32_t stride;                 /**< Bytes per row */
    bool bgra;                       /**< Rows need a BGRA to RGBA swap */
    char path[BOX_CAPTURE_PATH_MAX]; /**< PNG the worker writes */
} box_capture_job_t;

/** Thread that encodes and writes captures away from the render thread */
typedef struct box_capture_worker_t {
    pthread_t thread;                /**< The worker thread */
    pthread_mutex_t lock;            /**< Guards the queue, the count and the stop flag */
    pthread_cond_t cond;             /**< Signals a queued job, a finished job or a stop */
    box_capture_job_t *head;         /**< Oldest queued job */
    box_capture_job_t *tail;         /**< Newest queued job */
    int32_t pending;                 /**< Jobs queued or being encoded */
    uint32_t wait;                   /**< Frames the worker has held jobs, render thread only */
    bool started;                    /**< Thread was created */
    bool stop;                       /**< Thread exits once the queue is empty */
} box_capture_worker_t;
#endif

/** Queue a readback of a texture that is written as PNG once the GPU finished the copy.
 *
 * Native waits for a slot when every slot is in flight, the web target drops the capture.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param path Output file
 * @return 0 when the capture was queued, nonzero when it was dropped
 */
int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path);

/** Deliver the maps of captures the GPU finished without waiting for the others.
 *
 * @param r The renderer
 */
void box_capture_poll(
    box_renderer_t *r);

/** Finish every capture in flight, stop the worker and release the buffers.
 *
 * @param r The renderer
 */
void box_capture_fini(
    box_renderer_t *r);

#endif
