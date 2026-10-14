/**
 * @file app/cpu.c
 * @brief CPU time of the process, the oversubscription guard and the budget sleep.
 */

#include "app.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

double box_app_cpu_time(void)
{
    double result = 0;
#ifndef __EMSCRIPTEN__
    struct rusage usage;
    if (!getrusage(RUSAGE_SELF, &usage)) {
        result = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6 +
            (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
    }
#endif

    return result;
}

double box_app_wall_time(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

bool box_app_oversubscribed(
    double load,
    int32_t cores)
{
    return load > (double)cores;
}

int box_app_guard(void)
{
    int result = 0;
#ifndef __EMSCRIPTEN__
    double load = 0;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) {
        cores = 1;
    }

    if (getloadavg(&load, 1) == 1 && box_app_oversubscribed(load, (int32_t)cores)) {
        ecs_err("cpu oversubscribed: load %.1f on %d cores", load, (int)cores);
        result = -1;
    }
#endif

    return result;
}

double box_app_pace(
    double cpu_used,
    double wall_used,
    double budget)
{
    double result = 0;
    if (budget > 0) {
        double wall_needed = cpu_used * 100.0 / budget;
        if (wall_needed > wall_used) {
            result = wall_needed - wall_used;
        }
    }

    return result;
}
