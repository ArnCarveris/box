/**
 * @file Fps.c
 * @brief Tests for the frame time ring, the Fps singleton and the panel script.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/monitor/monitor_internal.h"

void Fps_empty_ring_reports_nothing(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_stats_t stats;
    box_fps_ring_stats(&ring, BOX_FPS_WINDOW_MS, &stats);
    test_int(stats.frames, 0);
    test_flt(stats.fps, 0.0f);
    test_flt(stats.mean_ms, 0.0f);
    test_flt(stats.worst_ms, 0.0f);
}

void Fps_ring_keeps_the_newest_frame_first(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_ring_push(&ring, 10.0f);
    box_fps_ring_push(&ring, 20.0f);
    box_fps_ring_push(&ring, 30.0f);
    test_int(ring.count, 3);
    test_flt(box_fps_ring_get(&ring, 0), 30.0f);
    test_flt(box_fps_ring_get(&ring, 1), 20.0f);
    test_flt(box_fps_ring_get(&ring, 2), 10.0f);
    test_flt(box_fps_ring_get(&ring, 3), 0.0f);
}

void Fps_ring_wraps_past_its_size(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    int32_t i;
    for (i = 0; i < BOX_FPS_RING + 17; i ++) {
        box_fps_ring_push(&ring, (float)i);
    }

    test_int(ring.count, BOX_FPS_RING);
    test_flt(box_fps_ring_get(&ring, 0), (float)(BOX_FPS_RING + 16));
    test_flt(box_fps_ring_get(&ring, BOX_FPS_RING - 1), 17.0f);
}

void Fps_ring_ignores_a_negative_frame(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_ring_push(&ring, 12.0f);
    box_fps_ring_push(&ring, -3.0f);
    test_int(ring.count, 1);
    test_flt(box_fps_ring_get(&ring, 0), 12.0f);
}

void Fps_window_covers_one_second_of_frames(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    int32_t i;
    for (i = 0; i < BOX_FPS_RING; i ++) {
        box_fps_ring_push(&ring, 10.0f);
    }

    test_int(box_fps_ring_window(&ring, BOX_FPS_WINDOW_MS), 100);
    test_int(box_fps_ring_window(&ring, 55.0f), 6);
    test_int(box_fps_ring_window(&ring, 0.0f), 0);
}

void Fps_stats_summarize_the_window(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    int32_t i;
    for (i = 0; i < 40; i ++) {
        box_fps_ring_push(&ring, 20.0f);
    }

    box_fps_ring_push(&ring, 40.0f);
    box_fps_stats_t stats;
    box_fps_ring_stats(&ring, BOX_FPS_WINDOW_MS, &stats);
    test_int(stats.frames, 41);
    test_flt(stats.worst_ms, 40.0f);
    test_assert(stats.mean_ms > 20.0f && stats.mean_ms < 21.0f);
    test_assert(stats.fps > 47.0f && stats.fps < 50.0f);
}

static int box_test_fps_sets = 0;

static void BoxTestFpsOnSet(
    ecs_iter_t *it)
{
    box_test_fps_sets += it->count;
}

void Fps_singleton_exists_at_import(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const Fps *fps = ecs_singleton_get(world, Fps);
    test_assert(fps != NULL);
    test_flt(fps->fps, 0.0);
    test_int(fps->count, 0);
    test_flt(fps->history[BOX_MONITOR_FPS_HISTORY - 1], 0.0f);
    ecs_fini(world);
}

void Fps_window_is_published_on_the_singleton(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.02f);
    }

    const Fps *fps = ecs_singleton_get(world, Fps);
    test_assert(fps->fps > 49.0 && fps->fps < 51.0);
    test_assert(fps->mean_ms > 19.0 && fps->mean_ms < 21.0);
    test_assert(fps->worst_ms > 19.0 && fps->worst_ms < 21.0);
    test_assert(fps->frame_ms > 19.0 && fps->frame_ms < 21.0);
    ecs_fini(world);
}

void Fps_history_keeps_the_newest_frame_last(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.02f);
    ecs_progress(world, 0.04f);

    const Fps *fps = ecs_singleton_get(world, Fps);
    test_int(fps->count, 3);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 1] > 39.0f);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 1] < 41.0f);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 2] > 19.0f);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 2] < 21.0f);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 3] > 9.0f);
    test_assert(fps->history[BOX_MONITOR_FPS_HISTORY - 3] < 11.0f);
    test_flt(fps->history[BOX_MONITOR_FPS_HISTORY - 4], 0.0f);
    ecs_fini(world);
}

void Fps_history_count_stops_at_its_size(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < BOX_MONITOR_FPS_HISTORY + 10; i ++) {
        ecs_progress(world, 0.01f);
    }

    const Fps *fps = ecs_singleton_get(world, Fps);
    test_int(fps->count, BOX_MONITOR_FPS_HISTORY);
    test_assert(fps->history[0] > 9.0f && fps->history[0] < 11.0f);
    ecs_fini(world);
}

void Fps_publishing_emits_on_set(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_test_fps_sets = 0;
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Fps) }},
        .events = { EcsOnSet },
        .callback = BoxTestFpsOnSet
    });
    box_test_fps_sets = 0;
    ecs_progress(world, 0.02f);
    ecs_progress(world, 0.02f);
    test_int(box_test_fps_sets, 2);
    ecs_fini(world);
}

void Fps_panel_is_hidden_by_default(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t visible = ecs_lookup(world, "box.monitor.fps_panel.visible");
    test_assert(visible != 0);
    test_bool(ecs_mut_var_get_t(world, "box.monitor.fps_panel.visible", ecs_bool_t), false);
    ecs_progress(world, 0.02f);
    test_assert(ecs_lookup(world, "box.monitor.fps_panel.panel") == 0);
    ecs_fini(world);
}

void Fps_panel_shows_while_visible(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {true}), 0);
    ecs_entity_t panel = ecs_lookup(world, "box.monitor.fps_panel.panel");
    test_assert(panel != 0);
    test_assert(ecs_has(world, panel, Ui));

    test_int(ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {false}), 0);
    test_assert(!ecs_is_alive(world, panel));
    test_assert(ecs_lookup(world, "box.monitor.fps_panel.panel") == 0);
    ecs_fini(world);
}

void Fps_panel_label_shows_the_frame_rate(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {true});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.02f);
    }

    ecs_entity_t label = ecs_lookup(world,
        "box.monitor.fps_panel.panel.screen.frame.label");
    test_assert(label != 0);
    const Text *text = ecs_get(world, label, Text);
    test_assert(text != NULL);
    test_str(text->value, "50");
    const Rgb *rgb = ecs_get(world, label, Rgb);
    test_assert(rgb->r > rgb->g);

    for (i = 0; i < 110; i ++) {
        ecs_progress(world, 0.01f);
    }

    text = ecs_get(world, label, Text);
    test_str(text->value, "100");
    rgb = ecs_get(world, label, Rgb);
    test_assert(rgb->g > rgb->r && rgb->g > rgb->b);
    ecs_fini(world);
}

void Fps_panel_draws_one_bar_per_recorded_frame(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {true});
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    ecs_entity_t frame = ecs_lookup(world, "box.monitor.fps_panel.panel.screen.frame");
    test_assert(frame != 0);
    test_assert(ecs_lookup_child(world, frame, "bar_114") == 0);
    for (i = 115; i < 120; i ++) {
        char name[16];
        ecs_os_snprintf(name, sizeof(name), "bar_%d", i);
        test_assert(ecs_lookup_child(world, frame, name) != 0);
    }

    ecs_entity_t bar = ecs_lookup_child(world, frame, "bar_119");
    const Rect *rect = ecs_get(world, bar, Rect);
    test_flt(rect->width, 3.0f);
    test_flt(rect->height, 12.0f);
    const Rgb *rgb = ecs_get(world, bar, Rgb);
    test_assert(rgb->g > rgb->r && rgb->g > rgb->b);
    ecs_fini(world);
}

void Fps_slow_frame_turns_its_bar_red(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {true});
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.025f);
    ecs_progress(world, 0.08f);

    ecs_entity_t frame = ecs_lookup(world, "box.monitor.fps_panel.panel.screen.frame");
    const Rgb *green = ecs_get(world, ecs_lookup_child(world, frame, "bar_117"), Rgb);
    const Rgb *yellow = ecs_get(world, ecs_lookup_child(world, frame, "bar_118"), Rgb);
    const Rgb *red = ecs_get(world, ecs_lookup_child(world, frame, "bar_119"), Rgb);
    test_assert(green->g > green->r);
    test_assert(yellow->r > yellow->b && yellow->g > yellow->b);
    test_assert(red->r > red->g && red->r > red->b);
    const Rect *rect = ecs_get(world, ecs_lookup_child(world, frame, "bar_119"), Rect);
    test_flt(rect->height, 60.0f);
    ecs_fini(world);
}

void Fps_panel_follows_the_surface(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    Surface *surface = ecs_singleton_ensure(world, Surface);
    surface->width = 960;
    surface->height = 540;
    ecs_mut_var_set_t(world, "box.monitor.fps_panel.visible", ecs_bool_t, {true});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0.02f);
    }

    ecs_entity_t back = ecs_lookup(world, "box.monitor.fps_panel.panel.screen.frame.back");
    const Bounds *bounds = ecs_get(world, back, Bounds);
    test_assert(bounds != NULL);
    test_flt(bounds->width, 492.0f);
    test_flt(bounds->height, 98.0f);
    test_flt(bounds->x, 960.0f / 2.0f - 10.0f - 492.0f / 2.0f);
    test_flt(bounds->y, 540.0f / 2.0f - 10.0f - 98.0f / 2.0f);
    ecs_fini(world);
}
