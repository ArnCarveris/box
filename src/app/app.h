/**
 * @file app/app.h
 * @brief Process measurements and the temporary folder shared by the box.app sources.
 */

#ifndef BOX_APP_INTERNAL_H
#define BOX_APP_INTERNAL_H

#include <box.h>

/** Read the CPU time the process has used on all of its threads.
 *
 * @return User plus system seconds
 */
double box_app_cpu_time(void);

/** Read the wall clock.
 *
 * @return Seconds since an arbitrary epoch
 */
double box_app_wall_time(void);

/** Decide whether a machine is oversubscribed.
 *
 * @param load Load average over the last minute
 * @param cores Number of online cores
 * @return Whether the load exceeds the core count
 */
bool box_app_oversubscribed(
    double load,
    int32_t cores);

/** Refuse a headless start when the machine is oversubscribed.
 *
 * @return 0 when the process may start, nonzero when the machine is oversubscribed
 */
int box_app_guard(void);

/** Compute the sleep that brings a process back within its CPU budget.
 *
 * @param cpu_used CPU seconds the process used during the interval
 * @param wall_used Wall seconds the interval took
 * @param budget Percentage of one core the process may use
 * @return Seconds to sleep, 0 when the process is within its budget
 */
double box_app_pace(
    double cpu_used,
    double wall_used,
    double budget);

/** Create the temporary folder of the process and remove it when the world is deleted.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_create(
    ecs_world_t *world);

#endif
