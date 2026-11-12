/**
 * @file app/app.h
 * @brief Process measurements and the temporary folder shared by the box.app sources.
 */

#ifndef BOX_APP_INTERNAL_H
#define BOX_APP_INTERNAL_H

#include <box.h>
#include <sys/types.h>

int box_app_views(ecs_world_t *world);

/** Read the CPU time the process has used on all of its threads.
 *
 * @return User plus system seconds, 0 in the browser, which has no process CPU time
 */
double box_app_cpu_time(void);

/** Read the wall clock.
 *
 * @return Seconds since an arbitrary epoch
 */
double box_app_wall_time(void);

/** Decide whether a machine is oversubscribed for a process with a CPU budget.
 *
 * @param load Load average over the last minute
 * @param cores Number of online cores
 * @param budget Percentage of one core the process may use, 0 for no limit, which counts as a
 *        floor of one core: an unlimited run may use more, but counting every core would refuse it
 *        at almost any load
 * @return Whether the load plus the cores the budget claims exceeds the core count
 */
bool box_app_oversubscribed(
    double load,
    int32_t cores,
    float budget);

/** Refuse a headless start when the machine is oversubscribed for the CPU budget.
 *
 * @param budget Percentage of one core the process may use, 0 for no limit
 * @return 0 when the process may start, nonzero when the machine is oversubscribed
 */
int box_app_guard(
    float budget);

/** Compute the sleep that brings a process back within its CPU budget.
 *
 * @param cpu_used CPU seconds the process used during the interval
 * @param wall_used Wall seconds since the interval started, which is the deadline of the
 *        previous sleep, so the time a late wake up overslept counts toward this one
 * @param budget Percentage of one core the process may use
 * @return Seconds to sleep, 0 when the process is within its budget
 */
double box_app_pace(
    double cpu_used,
    double wall_used,
    double budget);

#ifndef _WIN32
/** Decide whether the root of the temporary folders keeps the folder of a process safe from
 * other users: it must be a real folder, owned by the user or by root, and sticky when others
 * can write it.
 *
 * @param mode The mode of the root, as lstat reports it
 * @param owner The user that owns the root
 * @param user The user of the process
 * @return NULL when the root is safe, otherwise why it is not
 */
const char* box_app_tmp_root_problem(
    mode_t mode,
    uid_t owner,
    uid_t user);
#endif

/** Create the private temporary folder of the process, or share the one it created, and
 * remove it when the last world that uses it is deleted.
 *
 * A folder of the current user that an earlier process with the same pid left at the path is
 * removed first; anything else at the path is refused and left alone.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_open(
    ecs_world_t *world);

/** Open the temporary folder of a headless process and stop the world when that fails.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_create(
    ecs_world_t *world);

/** Remove a folder of the current user and everything in it without following symbolic
 * links.
 *
 * @param path The folder
 */
void box_app_rmtree(
    const char *path);

#endif
