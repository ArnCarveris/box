/**
 * @file Gpu.c
 * @brief Tests for the pass collector of the GPU timings and the component it feeds.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/renderer/timing.h"

void Gpu_empty_collector_reports_nothing(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    test_int(t.count, 0);
    test_flt(box_timing_get(&t, "opaque"), 0.0);
}

void Gpu_passes_take_two_timestamps_each(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    test_bool(box_timing_begin(&t), true);
    test_int(box_timing_add(&t, "shadow"), 0);
    test_int(box_timing_add(&t, "opaque"), 2);
    test_int(box_timing_add(&t, "sorted"), 4);
    test_int(t.recorded_count, 3);
    test_str(t.recorded[1].name, "opaque");
}

void Gpu_a_pass_outside_a_frame_is_not_timed(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    test_int(box_timing_add(&t, "opaque"), -1);
    test_int(t.recorded_count, 0);
}

void Gpu_a_full_frame_drops_the_passes_past_it(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    int32_t i;
    for (i = 0; i < BOX_TIMING_PASS_MAX; i ++) {
        test_int(box_timing_add(&t, "opaque"), i * 2);
    }

    test_int(box_timing_add(&t, "opaque"), -1);
    test_int(t.dropped, 1);
    test_int(t.recorded_count, BOX_TIMING_PASS_MAX);
}

void Gpu_a_long_pass_name_is_truncated(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "a_pass_name_that_does_not_fit_in_the_field");
    test_int(ecs_os_strlen(t.recorded[0].name), BOX_TIMING_NAME_MAX - 1);
}

void Gpu_resolve_publishes_the_milliseconds(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_add(&t, "sorted");
    box_timing_end(&t);
    uint64_t stamps[4] = { 1000000, 3500000, 10000000, 10250000 };
    box_timing_resolve(&t, stamps, 4);
    test_int(t.count, 2);
    test_str(t.passes[0].name, "opaque");
    test_flt(t.passes[0].time_ms, 2.5);
    test_flt(box_timing_get(&t, "sorted"), 0.25);
    test_flt(t.frame_ms, 2.75);
}

void Gpu_passes_with_one_name_are_summed(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "shadow");
    box_timing_add(&t, "shadow");
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    uint64_t stamps[6] = { 0, 1000000, 0, 2000000, 0, 4000000 };
    box_timing_resolve(&t, stamps, 6);
    test_int(t.count, 2);
    test_flt(box_timing_get(&t, "shadow"), 3.0);
    test_flt(box_timing_get(&t, "opaque"), 4.0);
}

void Gpu_a_backwards_timestamp_is_no_time(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    uint64_t stamps[2] = { 5000000, 1000000 };
    box_timing_resolve(&t, stamps, 2);
    test_flt(box_timing_get(&t, "opaque"), 0.0);
}

void Gpu_resolve_names_the_frame_it_measured(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    uint64_t stamps[2] = { 0, 1000000 };
    box_timing_resolve(&t, stamps, 2);
    test_int(t.resolved, 0);
    test_bool(box_timing_begin(&t), true);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    box_timing_resolve(&t, stamps, 2);
    test_int(t.resolved, 1);
}

void Gpu_a_pending_frame_holds_back_the_next_one(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    test_bool(box_timing_begin(&t), true);
    box_timing_add(&t, "opaque");
    test_bool(box_timing_end(&t), true);
    test_bool(box_timing_begin(&t), false);
    test_int(box_timing_add(&t, "opaque"), -1);
    uint64_t stamps[2] = { 0, 2000000 };
    box_timing_resolve(&t, stamps, 2);
    test_bool(box_timing_begin(&t), true);
}

void Gpu_a_dropped_frame_releases_the_collector(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    test_bool(t.pending, true);
    box_timing_drop(&t);
    test_int(t.count, 0);
    test_bool(box_timing_begin(&t), true);
}

void Gpu_a_frame_without_passes_is_not_resolved(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    test_bool(box_timing_end(&t), false);
    test_bool(t.pending, false);
    test_bool(box_timing_begin(&t), true);
}

void Gpu_an_end_without_a_frame_changes_nothing(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    test_bool(box_timing_end(&t), false);
    test_bool(t.pending, true);
}

void Gpu_resolve_ignores_the_timestamps_it_did_not_record(void)
{
    // monitoring/gpu_timings
    box_timing_t t;
    ecs_os_memset_t(&t, 0, box_timing_t);
    box_timing_begin(&t);
    box_timing_add(&t, "opaque");
    box_timing_end(&t);
    uint64_t stamps[4] = { 0, 1000000, 0, 8000000 };
    box_timing_resolve(&t, stamps, 4);
    test_int(t.count, 1);
    test_flt(t.frame_ms, 1.0);
}

void Gpu_absent_component_is_not_published(void)
{
    // monitoring/gpu_timings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0.016f);
    test_assert(ecs_singleton_get(world, GpuTimings) == NULL);
    ecs_fini(world);
}

void Gpu_a_world_without_a_renderer_reports_no_passes(void)
{
    // monitoring/gpu_timings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, GpuTimings);
    ecs_progress(world, 0.016f);
    const GpuTimings *g = ecs_singleton_get(world, GpuTimings);
    test_assert(g != NULL);
    test_bool(g->supported, false);
    test_int(g->pass_count, 0);
    test_flt(g->frame_ms, 0.0);
    test_assert(g->passes[0].name == NULL);
    ecs_fini(world);
}
