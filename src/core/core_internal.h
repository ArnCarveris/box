/**
 * @file core/core_internal.h
 * @brief Private helpers of the box.core module.
 */

#ifndef BOX_CORE_INTERNAL_H
#define BOX_CORE_INTERNAL_H

#include <box.h>

/** CPU accounting for budget pacing */
typedef struct box_cpu_t {
    double cpu_time;    /**< Process CPU seconds at the last sample */
    double wall_time;   /**< Wall clock seconds at the last sample */
} box_cpu_t;

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

/** Sleep as needed to keep CPU usage within the budget since the last call.
 *
 * @param cpu Accounting state
 * @param budget Fraction of one core allowed
 */
void box_cpu_pace(
    box_cpu_t *cpu,
    double budget);

#endif
