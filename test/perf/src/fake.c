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

void fake_work(
    ecs_iter_t *it)
{
    AppStatus *app = ecs_singleton_get_mut(it->world, AppStatus);
    fake_time += fake_work_ms * 0.001;
    if (app) {
        app->slept += fake_slept_ms * 0.001;
    }
}
