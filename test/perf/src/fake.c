/**
 * @file fake.c
 * @brief Deterministic clock and frame cost callbacks shared by the perf tests.
 */

#include "fake.h"

double fake_time = 100;
double fake_work_ms = 0;
double fake_slept_ms = 0;

double fake_now(void)
{
    fake_time += 1e-6;

    return fake_time;
}

void fake_sleep(
    double seconds)
{
    fake_time += seconds;
}

void fake_sleep_late(
    double seconds)
{
    fake_time += seconds * 1.5 + 0.00002;
}

uint64_t fake_os_now(void)
{
    return (uint64_t)(fake_time * 1e9);
}

void fake_os_sleep(
    int32_t sec,
    int32_t nanosec)
{
    fake_time += (double)sec + (double)nanosec * 1e-9;
}

void fake_work(
    ecs_iter_t *it)
{
    AppStatus *app = ecs_singleton_get_mut(it->world, AppStatus);
    fake_time += fake_work_ms * 0.001;
    if (app) {
        app->slept += fake_slept_ms * 0.001;
    }
}
