/**
 * @file Spikes.c
 * @brief Tests for the frame time collector and its spike budget.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/monitor/monitor_internal.h"

void Spikes_empty_run_reports_nothing(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 8, BOX_SPIKE_FACTOR);
    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_flt(report.mean_ms, 0.0);
    test_flt(report.max_ms, 0.0);
    test_flt(report.budget_ms, 0.0);
    test_int(report.count, 0);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_run_reports_mean_and_percentiles(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 16, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        box_spikes_add(&spikes, 10.0 - i, 0, 0);
    }

    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(spikes.count, 10);
    test_flt(report.mean_ms, 5.5);
    test_flt(report.p50_ms, 6.0);
    test_flt(report.p99_ms, 10.0);
    test_flt(report.max_ms, 10.0);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_flat_run_has_no_spike(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 64, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 64; i ++) {
        box_spikes_add(&spikes, 2.0 + (i % 4) * 0.25, 0, 0);
    }

    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 0);
    test_flt(report.budget_ms, report.mean_ms * BOX_SPIKE_FACTOR);
    test_bool(report.max_ms < report.budget_ms, true);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_frame_over_the_budget_is_a_spike(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 100, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 100; i ++) {
        box_spikes_add(&spikes, i == 42 ? 40.0 : 1.0, 0, 0);
    }

    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 1);
    test_int(report.worst[0].frame, 42);
    test_flt(report.worst[0].time_ms, 40.0);
    test_flt(report.max_ms, 40.0);
    test_bool(report.worst[0].time_ms > report.budget_ms, true);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_budget_follows_the_factor(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 32, 2.0);
    int32_t i;
    for (i = 0; i < 31; i ++) {
        box_spikes_add(&spikes, 1.0, 0, 0);
    }

    box_spikes_add(&spikes, 3.0, 0, 0);
    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_flt(report.mean_ms, 34.0 / 32.0);
    test_flt(report.budget_ms, 2.0 * 34.0 / 32.0);
    test_int(report.count, 1);
    test_int(report.worst[0].frame, 31);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_zero_factor_disables_the_budget(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 8, 0);
    box_spikes_add(&spikes, 1.0, 0, 0);
    box_spikes_add(&spikes, 1000.0, 0, 0);
    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_flt(report.max_ms, 1000.0);
    test_flt(report.budget_ms, 0.0);
    test_int(report.count, 0);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_spike_keeps_the_slowest_system(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 16, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 15; i ++) {
        box_spikes_add(&spikes, 1.0, 700, 0.4);
    }

    box_spikes_add(&spikes, 20.0, 800, 17.5);
    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 1);
    test_uint(report.worst[0].system, 800);
    test_flt(report.worst[0].system_ms, 17.5);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_spikes_are_listed_worst_first(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 50, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 50; i ++) {
        double ms = 1.0;
        if (i == 10) {
            ms = 12.0;
        } else if (i == 20) {
            ms = 30.0;
        } else if (i == 30) {
            ms = 20.0;
        }

        box_spikes_add(&spikes, ms, (ecs_entity_t)(500 + i), ms * 0.5);
    }

    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 3);
    test_int(report.worst[0].frame, 20);
    test_int(report.worst[1].frame, 30);
    test_int(report.worst[2].frame, 10);
    test_uint(report.worst[0].system, 520);
    test_flt(report.worst[0].system_ms, 15.0);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_report_keeps_the_worst_of_many_spikes(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 200, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 180; i ++) {
        box_spikes_add(&spikes, 1.0, 0, 0);
    }

    for (i = 0; i < 20; i ++) {
        box_spikes_add(&spikes, 30.0 + i, 0, 0);
    }

    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 20);
    test_flt(report.worst[0].time_ms, 49.0);
    test_int(report.worst[0].frame, 199);
    test_flt(report.worst[BOX_SPIKE_MAX - 1].time_ms, 42.0);
    box_spikes_fini(&spikes);
    ecs_os_fini();
}

void Spikes_collector_grows_past_its_capacity(void)
{
    // responsiveness/frame_spikes
    ecs_os_init();
    box_spikes_t spikes;
    box_spikes_init(&spikes, 2, BOX_SPIKE_FACTOR);
    int32_t i;
    for (i = 0; i < 300; i ++) {
        box_spikes_add(&spikes, 1.0 + (i == 299 ? 50.0 : 0.0), 0, 0);
    }

    test_int(spikes.count, 300);
    test_bool(spikes.capacity >= 300, true);
    test_int(spikes.frames[299].frame, 299);
    box_spike_report_t report;
    box_spikes_eval(&spikes, &report);
    test_int(report.count, 1);
    test_int(report.worst[0].frame, 299);
    box_spikes_fini(&spikes);
    test_assert(spikes.frames == NULL);
    ecs_os_fini();
}
