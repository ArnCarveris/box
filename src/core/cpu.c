/**
 * @file core/cpu.c
 * @brief CPU oversubscription check and budget pacing.
 */

#include "core_internal.h"
#include <unistd.h>
#include <sys/resource.h>
#include <stdlib.h>

/** Process CPU time in seconds.
 *
 * @return User plus system time
 */
static double box_cpu_time(void)
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    return (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6 +
        (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
}

/** Wall clock time in seconds.
 *
 * @return Monotonic seconds
 */
static double box_wall_time(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

bool box_cpu_oversubscribed(void)
{
    bool result = false;
#ifndef BOX_PLATFORM_WEB
    double load = 0;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) {
        cores = 1;
    }

    if (getloadavg(&load, 1) == 1 && load > (double)cores) {
        ecs_err("cpu oversubscribed: load %.1f on %ld cores", load, cores);
        result = true;
    }
#endif

    return result;
}

void box_cpu_pace(
    box_cpu_t *cpu,
    double budget)
{
    double cpu_now = box_cpu_time();
    double wall_now = box_wall_time();
    if (cpu->wall_time != 0) {
        double cpu_used = cpu_now - cpu->cpu_time;
        double wall_used = wall_now - cpu->wall_time;
        double wall_needed = cpu_used / budget;
        if (wall_needed > wall_used) {
            double sleep = wall_needed - wall_used;
            ecs_os_sleep((int32_t)sleep, (int32_t)((sleep - (int32_t)sleep) * 1e9));
            wall_now = box_wall_time();
        }
    }

    cpu->cpu_time = cpu_now;
    cpu->wall_time = wall_now;
}
