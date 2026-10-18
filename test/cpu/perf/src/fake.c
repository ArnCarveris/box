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

static ecs_os_api_now_t fake_saved_now = NULL;
static ecs_os_api_sleep_t fake_saved_sleep = NULL;

void fake_os_begin(void)
{
    fake_saved_now = ecs_os_api.now_;
    fake_saved_sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = fake_os_now;
    ecs_os_api.sleep_ = fake_os_sleep;
}

void fake_os_end(void)
{
    ecs_os_api.now_ = fake_saved_now;
    ecs_os_api.sleep_ = fake_saved_sleep;
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
