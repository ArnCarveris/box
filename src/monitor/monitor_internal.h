/**
 * @file monitor/monitor_internal.h
 * @brief Private sampling state and registration of the box.monitor module.
 */

#ifndef BOX_MONITOR_INTERNAL_H
#define BOX_MONITOR_INTERNAL_H

#include <box.h>
#include "../core/core_internal.h"

/** Moving average state of the CPU monitor, owned by its sampling system */
typedef struct box_cpu_monitor_t {
    box_cpu_sample_t last; /**< Reading of the previous sample */
    bool sampled;          /**< A previous reading exists */
} box_cpu_monitor_t;

/** Collection state of the statistics mirror, owned by its collecting system */
typedef struct box_stats_monitor_t {
    double elapsed;                           /**< Seconds since the last collection */
    double frame_ms[BOX_MONITOR_HISTORY_MAX]; /**< Frame times of the retained window */
    int32_t count;                            /**< Frame times in the window */
    int32_t next;                             /**< Slot the next frame time takes */
} box_stats_monitor_t;

/** Per system accounting of the system monitor, owned by its collecting system */
typedef struct box_system_monitor_t {
    ecs_query_t *systems; /**< Query over every system of the world */
    ecs_map_t last;       /**< Nanoseconds each system spent up to the last frame */
} box_system_monitor_t;

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

/** Register the FpsOverlay defaults and the system that publishes its window.
 *
 * @param world The world
 */
void box_fps_overlay_init(
    ecs_world_t *world);

/** Insert a system and its time into a list that keeps the slowest entries.
 *
 * @param top The list, slowest first
 * @param count Entries of the list
 * @param system The system to insert
 * @param time_ms Milliseconds the system spent
 */
void box_monitor_top(
    SystemTime *top,
    int32_t count,
    ecs_entity_t system,
    double time_ms);

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
