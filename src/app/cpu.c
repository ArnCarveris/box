/**
 * @file app/cpu.c
 * @brief CPU time of the process, the oversubscription guard, the budget sleep and the wait.
 */

#include "app.h"
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>

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

static ecs_os_api_sleep_t box_os_sleep_next;

static void box_os_sleep(
    int32_t sec,
    int32_t nanosec)
{
    ecs_time_t start;
    double duration = (double)sec + (double)nanosec * 1e-9, left = duration;
    if (duration <= BOX_WAIT_STEP) {
        box_os_sleep_next(sec, nanosec);
        goto done;
    }

    ecs_os_get_time(&start);
    while (left > BOX_WAIT_STEP) {
        double step = left * BOX_WAIT_FRACTION;
        int32_t whole = (int32_t)step;
        ecs_time_t now = start;
        box_os_sleep_next(whole, (int32_t)((step - (double)whole) * 1e9));
        left = duration - ecs_time_measure(&now);
    }

    while (left > BOX_WAIT_EPSILON) {
        ecs_time_t now = start;
        sched_yield();
        left = duration - ecs_time_measure(&now);
    }

done:
    return;
}

void box_app_precise_sleep(void)
{
#ifndef __EMSCRIPTEN__
    if (ecs_os_api.sleep_ && ecs_os_api.sleep_ != box_os_sleep) {
        box_os_sleep_next = ecs_os_api.sleep_;
        ecs_os_api.sleep_ = box_os_sleep;
    }
#endif
}

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

    if (getloadavg(&load, 1) == 1 && box_app_oversubscribed(load, (int32_t)cores, budget)) {
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
