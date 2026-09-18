/**
 * @file core/core_internal.h
 * @brief Private helpers of the box.core module.
 */

#ifndef BOX_CORE_INTERNAL_H
#define BOX_CORE_INTERNAL_H

#include <box.h>

/** Cores one CPU sample reports usage for */
#define BOX_CPU_CORE_MAX (64)

/** CPU accounting for budget pacing */
typedef struct box_cpu_t {
    double cpu_time;    /**< Process CPU seconds at the last sample */
    double wall_time;   /**< Wall clock seconds at the last sample */
} box_cpu_t;

/** One reading of process, machine and per core CPU time.
 *
 * The machine and per core fields are cumulative ticks in a unit the platform
 * picks, so only the ratio of two readings is meaningful.
 */
typedef struct box_cpu_sample_t {
    double process_time;                  /**< Process user plus system seconds */
    double wall_time;                     /**< Monotonic seconds */
    double busy_time;                     /**< Busy time of every core */
    double total_time;                    /**< Busy plus idle time of every core */
    double core_busy[BOX_CPU_CORE_MAX];   /**< Busy time per core */
    double core_total[BOX_CPU_CORE_MAX];  /**< Busy plus idle time per core */
    int32_t cores;                        /**< Cores the reading covers */
} box_cpu_sample_t;

/** Remove a directory and everything in it.
 *
 * @param path The directory
 */
void box_rmtree(
    const char *path);

/** Create the per-process folder and register its cleanup.
 *
 * @param world The world
 */
void box_process_init(
    ecs_world_t *world);

/** Check whether the machine's load exceeds its core count.
 *
 * @return true when the CPU is oversubscribed
 */
bool box_cpu_oversubscribed(void);

/** Read the CPU time this process consumed.
 *
 * @return User plus system seconds
 */
double box_cpu_process_time(void);

/** Read the monotonic clock.
 *
 * @return Wall clock seconds
 */
double box_cpu_wall_time(void);

/** Count the cores of the machine.
 *
 * @return Online cores, at least one
 */
int32_t box_cpu_cores(void);

/** Read process, machine and per core CPU time in one go.
 *
 * @param sample Receives the reading
 */
void box_cpu_sample(
    box_cpu_sample_t *sample);

/** Report what the CPU budget cost the last frame.
 *
 * @return Milliseconds the last box_cpu_pace call slept
 */
double box_cpu_sleep_ms(void);

/** Sleep as needed to keep CPU usage within the budget since the last call.
 *
 * @param cpu Accounting state
 * @param budget Fraction of one core allowed
 */
void box_cpu_pace(
    box_cpu_t *cpu,
    double budget);

#endif
