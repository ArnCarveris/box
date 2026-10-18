/**
 * @file Report.c
 * @brief Tests for the JSON report and the reflected status of a performance run.
 */

#include <box.h>
#include <bake_test.h>
#include "fake.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void Report_writes_json(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    char path[256];
    snprintf(path, sizeof(path), "/tmp/box_perf_report_%d.json", (int)getpid());
    remove(path);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 5, .report = path });
    for (int i = 0; i < 7; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);
    static char json[65536];
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    json[fread(json, 1, sizeof(json) - 1, f)] = '\0';
    fclose(f);
    test_assert(strstr(json, "\"passed\": true") != NULL);
    test_assert(strstr(json, "\"target_fps\": 120") != NULL);
    test_assert(strstr(json, "\"frames\": 5") != NULL);
    test_assert(strstr(json, "\"errors\": 0") != NULL);
    test_assert(strstr(json, "\"shapes\": 1") != NULL);
    test_assert(strstr(json, "\"mean_ms\": ") != NULL);
    test_assert(strstr(json, "\"p99_ms\": ") != NULL);
    const char *intervals = strstr(json, "\"intervals_ms\": [");
    test_assert(intervals != NULL);
    int32_t commas = 0;
    for (const char *ptr = intervals; *ptr && *ptr != ']'; ptr ++) {
        commas += *ptr == ',';
    }

    test_int(commas, 4);
    remove(path);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Report_failed_run(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    char path[256];
    snprintf(path, sizeof(path), "/tmp/box_perf_failed_%d.json", (int)getpid());
    remove(path);

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .stall = 12, .report = path });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    static char json[65536];
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    json[fread(json, 1, sizeof(json) - 1, f)] = '\0';
    fclose(f);
    test_assert(strstr(json, "\"passed\": false") != NULL);
    test_assert(strstr(json, "\"errors\": 3") != NULL);
    test_assert(strstr(json, "\"slow\": 3") != NULL);
    test_assert(strstr(json, "\"stall_ms\": 12.000") != NULL);
    remove(path);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Report_status_reflected(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 2 });
    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    char *json = ecs_ptr_to_json(world, ecs_id(PerfStatus), status);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"state\":\"PerfDone\"") != NULL);
    test_assert(strstr(json, "\"passed\":true") != NULL);
    test_assert(strstr(json, "\"errors\":0") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Report_script_run(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    test_assert(ecs_script_run(world, "perf",
        "using box.camera\n"
        "using box.perf\n"
        "cam { Camera: {fov: 50} }\n"
        "$ { PerfRun: {fps: 90, frames: 4, warmup: 3, camera: cam} }\n"
        "$ { PerfRun: {quit: true, stall: 1} }\n", NULL) == 0);

    const PerfRun *run = ecs_singleton_get(world, PerfRun);
    test_assert(run != NULL);
    test_int(run->fps, 90);
    test_int(run->frames, 4);
    test_int(run->warmup, 3);
    test_assert(run->camera == ecs_lookup(world, "cam"));
    test_bool(run->quit, true);
    test_int(ecs_singleton_get(world, App)->fps, 90);

    int32_t frames = 0;
    while (ecs_progress(world, 0) && frames < 100) {
        frames ++;
    }

    test_int(frames, 6);
    test_int(ecs_singleton_get(world, PerfStatus)->frame, 4);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Report_orbit_json(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    char path[256];
    snprintf(path, sizeof(path), "/tmp/box_perf_orbit_%d.json", (int)getpid());
    remove(path);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 3, .frames = 4, .period = 2, .camera = camera, .report = path });
    for (int i = 0; i < 7; i ++) {
        ecs_set(world, view, CullStatus, {
            .frame = i, .instances = 50, .visible = 40 + i, .frustum_culled = 10 - i });
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);
    static char json[65536];
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    json[fread(json, 1, sizeof(json) - 1, f)] = '\0';
    fclose(f);
    test_assert(strstr(json, "\"warmup\": 3") != NULL);
    test_assert(strstr(json, "\"period\": 2.000") != NULL);
    test_assert(strstr(json, "\"revolutions\": 0.01") != NULL);
    test_assert(strstr(json, "\"views\": 1") != NULL);
    test_assert(strstr(json, "\"samples\": 4") != NULL);
    test_assert(strstr(json, "\"changes\": 3") != NULL);
    test_assert(strstr(json, "\"visible\": {\"min\": 43, \"max\": 46") != NULL);
    test_assert(strstr(json, "\"frustum_culled\": {\"min\": 4, \"max\": 7") != NULL);
    test_assert(strstr(json, "\"occlusion_culled\": {\"min\": 0, \"max\": 0") != NULL);
    remove(path);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}
