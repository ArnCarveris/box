/**
 * @file monitor/monitor_internal.h
 * @brief Private sampling state and registration of the box.monitor module.
 */

#ifndef BOX_MONITOR_INTERNAL_H
#define BOX_MONITOR_INTERNAL_H

#include <box.h>
#include "../core/core_internal.h"
#include "../renderer/renderer.h"

/** Statistics components the counts of the renderer can be published on */
#define BOX_MONITOR_COUNTED_MAX (8)

/** Copy the counts of a frame into a statistics component.
 *
 * @param ptr The component
 * @param c The counts
 */
typedef void (*box_counted_write_t)(
    void *ptr,
    const box_counters_t *c);

/** Moving average state of the CPU monitor, owned by its sampling system */
typedef struct box_cpu_monitor_t {
    box_cpu_sample_t last; /**< Reading of the previous sample */
    bool sampled;          /**< A previous reading exists */
} box_cpu_monitor_t;

/** Collection state of the statistics mirror, owned by its collecting system */
typedef struct box_stats_monitor_t {
    double elapsed;                           /**< Seconds since the last collection */
    int32_t frames;                           /**< Frames since the last collection */
    bool imported;                            /**< The mirror imported the stats addon */
    double frame_ms[BOX_MONITOR_HISTORY_MAX]; /**< Frame times of the retained window */
    int32_t count;                            /**< Frame times in the window */
    int32_t next;                             /**< Slot the next frame time takes */
} box_stats_monitor_t;

/** Per system accounting of the system monitor, owned by its collecting system */
typedef struct box_system_monitor_t {
    ecs_query_t *systems; /**< Query over every system of the world */
    ecs_map_t last;       /**< Nanoseconds each system spent up to the last frame */
} box_system_monitor_t;

/** Frame times a ring keeps, a second of frames at 240 per second */
#define BOX_FPS_RING (240)

/** Milliseconds the window of the published numbers covers */
#define BOX_FPS_WINDOW_MS (1000.0f)

/** Frame times of the most recent frames, oldest overwritten first */
typedef struct box_fps_ring_t {
    float times_ms[BOX_FPS_RING]; /**< Frame times in the order they arrived */
    int32_t count;                /**< Frame times the ring holds, at most its size */
    int32_t next;                 /**< Slot the next frame time takes */
} box_fps_ring_t;

/** Frame rate and frame time distribution of the frames a window covers */
typedef struct box_fps_stats_t {
    int32_t frames; /**< Frames the window covered */
    float fps;      /**< Frames per second over those frames */
    float mean_ms;  /**< Mean frame time of the window */
    float worst_ms; /**< Worst frame time of the window */
} box_fps_stats_t;

/** Record one frame time, overwriting the oldest one when the ring is full.
 *
 * @param ring The ring
 * @param time_ms Milliseconds the frame took, negative values are ignored
 */
void box_fps_ring_push(
    box_fps_ring_t *ring,
    float time_ms);

/** Frame time of one of the frames the ring holds, counted back from the newest.
 *
 * @param ring The ring
 * @param back Frames back from the newest one, zero for the newest
 * @return The frame time, zero when the ring holds no such frame
 */
float box_fps_ring_get(
    const box_fps_ring_t *ring,
    int32_t back);

/** Frames of the ring whose times fit in a window, counted back from the newest.
 *
 * The frame that crosses the end of the window is part of it, so a ring with
 * frames always reports at least one.
 *
 * @param ring The ring
 * @param window_ms Milliseconds the window covers
 * @return The number of frames
 */
int32_t box_fps_ring_window(
    const box_fps_ring_t *ring,
    float window_ms);

/** Summarize the frames of the ring that a window covers.
 *
 * @param ring The ring
 * @param window_ms Milliseconds the window covers
 * @param out Receives the frame rate and the frame time distribution
 */
void box_fps_ring_stats(
    const box_fps_ring_t *ring,
    float window_ms,
    box_fps_stats_t *out);

/** Register the CpuMonitor sampling system.
 *
 * @param world The world
 */
void box_cpu_monitor_init(
    ecs_world_t *world);

/** Register the EcsStats collection system and its stats addon import.
 *
 * @param world The world
 */
void box_stats_monitor_init(
    ecs_world_t *world);

/** Register the SystemMonitor collection system and its measurement toggle.
 *
 * @param world The world
 */
void box_system_monitor_init(
    ecs_world_t *world);

/** Register the system that publishes the per pass GPU times of the renderer.
 *
 * @param world The world
 */
void box_gpu_monitor_init(
    ecs_world_t *world);

/** Renderer of the first surface of a world.
 *
 * @param world The world
 * @return The renderer, or NULL when the world renders nothing
 */
box_renderer_t* box_monitor_renderer(
    ecs_world_t *world);

/** Keep a name in a reflected string field, reusing it while it is the same.
 *
 * @param slot The field
 * @param name The name it has now, NULL for an unused entry
 */
void box_monitor_name(
    char **slot,
    const char *name);

/** Register the system that publishes the counts of the renderer and RendererStats.
 *
 * @param world The world
 */
void box_counters_monitor_init(
    ecs_world_t *world);

/** Publish the counts of the renderer on a statistics singleton while it exists.
 *
 * @param world The world
 * @param component The singleton
 * @param flags BOX_COUNT_ flags of what it needs counted
 * @param write Fills it from the counts of a frame
 */
void box_monitor_counted(
    ecs_world_t *world,
    ecs_entity_t component,
    uint32_t flags,
    box_counted_write_t write);

/** Load the overlay script and tie each statistics panel to its singleton.
 *
 * @param world The world
 */
void box_stats_panels_init(
    ecs_world_t *world);

/** Register the system that fills the Fps singleton.
 *
 * @param world The world
 */
void box_fps_monitor_init(
    ecs_world_t *world);

/** Insert a system into a list that keeps the entries with the highest average.
 *
 * @param top The list, slowest first
 * @param count Entries of the list
 * @param entry The system and its times
 */
void box_monitor_top(
    SystemTime *top,
    int32_t count,
    const SystemTime *entry);

/** Whether the flecs stats addon is imported and its monitors run.
 *
 * @param world The world
 * @return true when the addon collects
 */
bool box_monitor_stats_active(
    const ecs_world_t *world);

/** Frames a spike report keeps, worst first */
#define BOX_SPIKE_MAX (8)

/** Default spike budget of a run as a multiple of its mean frame time */
#define BOX_SPIKE_FACTOR (4.0)

/** One measured frame and the system it spent the most time in */
typedef struct box_spike_frame_t {
    int32_t frame;       /**< Index of the frame in the measured run */
    double time_ms;      /**< Milliseconds the frame took */
    ecs_entity_t system; /**< Slowest system of the frame, 0 when not attributed */
    double system_ms;    /**< Milliseconds that system spent */
} box_spike_frame_t;

/** Frame times of a run and the budget their spikes are measured against */
typedef struct box_spikes_t {
    box_spike_frame_t *frames; /**< Measured frames, in frame order */
    int32_t count;             /**< Frames recorded */
    int32_t capacity;          /**< Frames the array holds */
    double factor;             /**< Budget as a multiple of the mean, zero disables */
} box_spikes_t;

/** Distribution of a run, its spike budget and the frames that exceeded it */
typedef struct box_spike_report_t {
    double mean_ms;                         /**< Mean frame time */
    double p50_ms;                          /**< Median frame time */
    double p99_ms;                          /**< 99th percentile frame time */
    double max_ms;                          /**< Worst frame time */
    double budget_ms;                       /**< Mean times the factor, zero when off */
    int32_t count;                          /**< Frames above the budget */
    box_spike_frame_t worst[BOX_SPIKE_MAX]; /**< The worst of those frames, worst first */
} box_spike_report_t;

/** Start a frame time collector with room for a run and its spike budget.
 *
 * @param spikes The collector
 * @param capacity Frames to make room for, at least one
 * @param factor Budget as a multiple of the mean, zero disables the check
 */
void box_spikes_init(
    box_spikes_t *spikes,
    int32_t capacity,
    double factor);

/** Free the frames of a collector.
 *
 * @param spikes The collector
 */
void box_spikes_fini(
    box_spikes_t *spikes);

/** Record one measured frame and the system it spent the most time in.
 *
 * @param spikes The collector
 * @param time_ms Milliseconds the frame took
 * @param system Slowest system of the frame, zero when it was not attributed
 * @param system_ms Milliseconds that system spent
 */
void box_spikes_add(
    box_spikes_t *spikes,
    double time_ms,
    ecs_entity_t system,
    double system_ms);

/** Summarize a run and collect the frames that exceeded its budget.
 *
 * @param spikes The collector
 * @param report The summary to fill
 */
void box_spikes_eval(
    const box_spikes_t *spikes,
    box_spike_report_t *report);

#endif
