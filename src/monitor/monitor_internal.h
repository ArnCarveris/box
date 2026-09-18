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

#endif
