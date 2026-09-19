/**
 * @file Fps.c
 * @brief Tests for the frame time ring, the panel it builds and the FpsOverlay component.
 */

#include <stdlib.h>
#include <box.h>
#include <bake_test.h>
#include "../../../src/renderer/overlay.h"

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
    for (i = 0; i < BOX_FPS_HISTORY + 17; i ++) {
        box_fps_ring_push(&ring, (float)i);
    }

    test_int(ring.count, BOX_FPS_HISTORY);
    test_flt(box_fps_ring_get(&ring, 0), (float)(BOX_FPS_HISTORY + 16));
    test_flt(box_fps_ring_get(&ring, BOX_FPS_HISTORY - 1), 17.0f);
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
    for (i = 0; i < BOX_FPS_HISTORY; i ++) {
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

void Fps_sixty_frames_a_second_is_green(void)
{
    // monitoring/fps_overlay
    test_int(box_fps_class(0.0f), BOX_FPS_GREEN);
    test_int(box_fps_class(8.0f), BOX_FPS_GREEN);
    test_int(box_fps_class(1000.0f / 60.0f), BOX_FPS_GREEN);
}

void Fps_above_thirty_frames_a_second_is_yellow(void)
{
    // monitoring/fps_overlay
    test_int(box_fps_class(20.0f), BOX_FPS_YELLOW);
    test_int(box_fps_class(1000.0f / 30.0f), BOX_FPS_YELLOW);
}

void Fps_below_thirty_frames_a_second_is_red(void)
{
    // monitoring/fps_overlay
    test_int(box_fps_class(40.0f), BOX_FPS_RED);
    test_int(box_fps_class(250.0f), BOX_FPS_RED);
}

void Fps_every_class_has_its_own_color(void)
{
    // monitoring/fps_overlay
    float green[3];
    float yellow[3];
    float red[3];
    box_fps_class_color(BOX_FPS_GREEN, green);
    box_fps_class_color(BOX_FPS_YELLOW, yellow);
    box_fps_class_color(BOX_FPS_RED, red);
    test_assert(green[1] > green[0] && green[1] > green[2]);
    test_assert(yellow[0] > yellow[2] && yellow[1] > yellow[2]);
    test_assert(red[0] > red[1] && red[0] > red[2]);
}

void Fps_panel_is_pinned_to_its_corner(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_ring_push(&ring, 16.0f);
    box_overlay_rect_t rects[BOX_OVERLAY_RECT_MAX];
    box_overlay_layout_t layout = {
        .width = 960.0f, .height = 540.0f, .scale = 1.0f, .opacity = 0.7f,
        .corner = BOX_OVERLAY_TOP_LEFT
    };
    test_assert(box_overlay_build(&ring, &layout, rects, BOX_OVERLAY_RECT_MAX) > 0);
    test_flt(rects[0].rect[0], BOX_OVERLAY_MARGIN);
    test_flt(rects[0].rect[1], BOX_OVERLAY_MARGIN);
    test_flt(rects[0].color[3], 0.7f);

    layout.corner = BOX_OVERLAY_BOTTOM_RIGHT;
    box_overlay_build(&ring, &layout, rects, BOX_OVERLAY_RECT_MAX);
    test_flt(rects[0].rect[0], 960.0f - BOX_OVERLAY_WIDTH - BOX_OVERLAY_MARGIN);
    test_flt(rects[0].rect[1], 540.0f - BOX_OVERLAY_HEIGHT - BOX_OVERLAY_MARGIN);
}

void Fps_panel_grows_with_its_scale(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_ring_push(&ring, 16.0f);
    box_overlay_rect_t rects[BOX_OVERLAY_RECT_MAX];
    box_overlay_layout_t layout = {
        .width = 960.0f, .height = 540.0f, .scale = 2.0f, .opacity = 1.0f,
        .corner = BOX_OVERLAY_TOP_LEFT
    };
    box_overlay_build(&ring, &layout, rects, BOX_OVERLAY_RECT_MAX);
    test_flt(rects[0].rect[2], BOX_OVERLAY_WIDTH * 2.0f);
    test_flt(rects[0].rect[3], BOX_OVERLAY_HEIGHT * 2.0f);
}

void Fps_panel_draws_one_bar_per_frame(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    int32_t i;
    for (i = 0; i < 20; i ++) {
        box_fps_ring_push(&ring, 16.0f);
    }

    box_overlay_rect_t rects[BOX_OVERLAY_RECT_MAX];
    box_overlay_layout_t layout = {
        .width = 960.0f, .height = 540.0f, .scale = 1.0f, .opacity = 1.0f,
        .corner = BOX_OVERLAY_TOP_RIGHT
    };
    int32_t count = box_overlay_build(&ring, &layout, rects, BOX_OVERLAY_RECT_MAX);
    test_int(count, 3 + 20);
    float green[3];
    box_fps_class_color(BOX_FPS_GREEN, green);
    test_flt(rects[3].color[0], green[0]);
    test_flt(rects[3].color[1], green[1]);
}

void Fps_a_slow_frame_turns_its_bar_red(void)
{
    // monitoring/fps_overlay
    box_fps_ring_t ring;
    ecs_os_memset_t(&ring, 0, box_fps_ring_t);
    box_fps_ring_push(&ring, 16.0f);
    box_fps_ring_push(&ring, 80.0f);
    box_overlay_rect_t rects[BOX_OVERLAY_RECT_MAX];
    box_overlay_layout_t layout = {
        .width = 960.0f, .height = 540.0f, .scale = 1.0f, .opacity = 1.0f,
        .corner = BOX_OVERLAY_TOP_LEFT
    };
    box_overlay_build(&ring, &layout, rects, BOX_OVERLAY_RECT_MAX);
    float red[3];
    box_fps_class_color(BOX_FPS_RED, red);
    test_flt(rects[3].color[0], red[0]);
    test_flt(rects[3].rect[3], BOX_OVERLAY_CHART_H);
}

void Fps_overlay_defaults_are_enabled_in_a_corner(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_entity_t e = ecs_new(world);
    const FpsOverlay *o = ecs_ensure(world, e, FpsOverlay);
    test_bool(o->enabled, true);
    test_int(o->corner, FpsTopRight);
    test_flt(o->scale, 1.0f);
    test_flt(o->opacity, 0.7f);
    test_flt(o->fps, 0.0);
    ecs_fini(world);
}

void Fps_window_is_published_on_the_component(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, FpsOverlay, {.enabled = true, .scale = 1.0f});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.02f);
    }

    const FpsOverlay *o = ecs_singleton_get(world, FpsOverlay);
    test_assert(o->fps > 45.0 && o->fps < 55.0);
    test_assert(o->mean_ms > 19.0 && o->mean_ms < 21.0);
    test_assert(o->worst_ms > 19.0 && o->worst_ms < 21.0);
    ecs_fini(world);
}

void Fps_overlay_publishes_its_text(void)
{
    // ore_engine/fps_text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, FpsOverlay, {.enabled = true, .scale = 1.0f,
        .corner = FpsBottomLeft});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.02f);
    }

    ecs_iter_t it = ecs_each(world, Text);
    test_bool(ecs_each_next(&it), true);
    test_int(it.count, 1);
    const Text *text = ecs_get(world, it.entities[0], Text);
    test_assert(text->value != NULL);
    test_assert(atoi(text->value) > 45 && atoi(text->value) < 55);

    const Rgb *rgb = ecs_get(world, it.entities[0], Rgb);
    test_assert(rgb->r > 0 || rgb->g > 0 || rgb->b > 0);

    const Bounds *bounds = ecs_get(world, it.entities[0], Bounds);
    float view_left = -960.0f / 2.0f;
    float view_bottom = -540.0f / 2.0f;
    test_assert(bounds->x > view_left && bounds->x < view_left + BOX_OVERLAY_WIDTH);
    test_assert(bounds->y > view_bottom && bounds->y < view_bottom + BOX_OVERLAY_HEIGHT);
    ecs_iter_fini(&it);
    ecs_fini(world);
}

void Fps_absent_overlay_publishes_nothing(void)
{
    // monitoring/fps_overlay
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.02f);
    }

    test_assert(ecs_singleton_get(world, FpsOverlay) == NULL);
    ecs_singleton_set(world, FpsOverlay, {.enabled = true, .scale = 1.0f});
    const FpsOverlay *o = ecs_singleton_get(world, FpsOverlay);
    test_flt(o->fps, 0.0);
    ecs_fini(world);
}
