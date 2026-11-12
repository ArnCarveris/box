/**
 * @file app/cpu.c
 * @brief CPU time of the process, the oversubscription guard, the budget sleep and the wait.
 */

#include "app.h"
#include <stdlib.h>
#include "../posix.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif

/** Seconds left below which a wait sleeps the rest in one go */
#define BOX_WAIT_STEP (0.0001)

/** Fraction of the time left that one sleep of a longer wait asks for.
 *
 * A process without a window gets up to 50% timer leeway on macOS, so a sleep of the whole
 * time left could overshoot by half of it; shorter sleeps converge on the deadline.
 */
#define BOX_WAIT_FRACTION (0.6)

/** Seconds before a deadline at which a wait counts it as reached, below the sleep resolution */
#define BOX_WAIT_EPSILON (1e-6)

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

double box_wait_until(
    double deadline)
{
    double now = box_app_wall_time();
    while (deadline - now > BOX_WAIT_EPSILON) {
        double left = deadline - now;
        ecs_sleepf(left > BOX_WAIT_STEP ? left * BOX_WAIT_FRACTION : left);
        now = box_app_wall_time();
    }

    return now;
}

bool box_app_oversubscribed(
    double load,
    int32_t cores,
    float budget)
{
    double claim = budget > 0 ? (double)budget / 100.0 : 1.0;

    return load + claim > (double)cores;
}

int box_app_guard(
    float budget)
{
    int result = 0;
#ifndef __EMSCRIPTEN__
    double load = 0;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) {
        cores = 1;
    }

    if (getloadavg(&load, 1) != 1) {
        load = 0;
    }

    if (box_app_oversubscribed(load, (int32_t)cores, budget)) {
        ecs_err("cpu oversubscribed: load %.1f plus a claim of %.2f cores on %d cores", load,
            budget > 0 ? (double)budget / 100.0 : 1.0, (int)cores);
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
