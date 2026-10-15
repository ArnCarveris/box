/**
 * @file Readiness.c
 * @brief Tests for the warning a headless frame logs when it stops waiting for loads in flight.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "jobs.h"

static int32_t hold_started = 0;
static int32_t hold_release = 0;
static double fake_now = 0;
static char warnings[4096];
static ecs_os_api_get_time_t real_get_time = NULL;
static ecs_os_api_log_t real_log = NULL;

static void WorkHold(
    void *ctx)
{
    ecs_os_ainc(&hold_started);
    while (!*(volatile int32_t*)&hold_release) {
        ecs_sleepf(0.001);
    }
}

static void FakeTime(
    ecs_time_t *time)
{
    fake_now += 1.0;
    time->sec = (uint32_t)fake_now;
    time->nanosec = 0;
}

static void CaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    if (level == -2 && strlen(warnings) + strlen(msg) + 2 < sizeof(warnings)) {
        strcat(warnings, msg);
        strcat(warnings, "\n");
    }
}

void Readiness_setup(void)
{
    hold_started = 0;
    hold_release = 0;
    fake_now = 0;
    warnings[0] = '\0';
}

void Readiness_texture_wait_timeout_warns(void)
{
    // test_fixes/readiness_wait_warn
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 1);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 100, .unguarded = true, .unpaced = true });

    test_int(box_job_submit(world, &(box_job_desc_t){ .work = WorkHold }), 0);
    int32_t i;
    for (i = 0; i < 5000 && !*(volatile int32_t*)&hold_started; i ++) {
        ecs_sleepf(0.001);
    }

    test_int(hold_started, 1);
    ecs_entity_t tex = ecs_entity(world, { .name = "stuck_texture" });
    ecs_set(world, tex, TexturePath, { .path = "test/integration/visual/assets/checker.png" });
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureLoading);

    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    real_get_time = ecs_os_api.get_time_;
    real_log = ecs_os_api.log_;
    ecs_os_api.get_time_ = FakeTime;
    ecs_os_api.log_ = CaptureLog;
    ecs_progress(world, 0);
    ecs_os_api.get_time_ = real_get_time;
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_assert(strstr(warnings, "headless texture wait timed out") != NULL);
    test_assert(strstr(warnings, "1 texture(s) loading (stuck_texture "
        "test/integration/visual/assets/checker.png)") != NULL);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureLoading);

    ecs_os_ainc(&hold_release);
    ecs_fini(world);
}

void Readiness_text_wait_timeout_warns(void)
{
    // test_fixes/readiness_wait_warn
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 1);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 100, .unguarded = true, .unpaced = true });

    test_int(box_job_submit(world, &(box_job_desc_t){ .work = WorkHold }), 0);
    int32_t i;
    for (i = 0; i < 5000 && !*(volatile int32_t*)&hold_started; i ++) {
        ecs_sleepf(0.001);
    }

    test_int(hold_started, 1);
    ecs_entity_t font = ecs_entity(world, { .name = "stuck_font" });
    ecs_set(world, font, Font, { .path = "etc/fonts/B612-Regular.ttf" });
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Text, { .value = "ready?", .size = 16, .font = font });
    test_int(ecs_get(world, font, FontStatus)->state, FontLoading);

    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    real_get_time = ecs_os_api.get_time_;
    real_log = ecs_os_api.log_;
    ecs_os_api.get_time_ = FakeTime;
    ecs_os_api.log_ = CaptureLog;
    ecs_progress(world, 0);
    ecs_os_api.get_time_ = real_get_time;
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_assert(strstr(warnings, "headless text wait timed out") != NULL);
    test_assert(strstr(warnings, "1 font(s) loading (stuck_font etc/fonts/B612-Regular.ttf)")
        != NULL);
    test_int(ecs_get(world, font, FontStatus)->state, FontLoading);

    ecs_os_ainc(&hold_release);
    ecs_fini(world);
}

void Readiness_ready_frame_does_not_warn(void)
{
    // test_fixes/readiness_wait_warn
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 100, .unguarded = true, .unpaced = true });

    ecs_entity_t tex = ecs_entity(world, { .name = "ready_texture" });
    ecs_set(world, tex, TexturePath, { .path = "test/integration/visual/assets/checker.png" });

    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    real_log = ecs_os_api.log_;
    ecs_os_api.log_ = CaptureLog;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_assert(strstr(warnings, "wait timed out") == NULL);
    test_assert(ecs_get(world, tex, TextureStatus)->state != TextureLoading);

    ecs_fini(world);
}
