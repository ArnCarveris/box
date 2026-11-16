/**
 * @file LoadPanel.c
 * @brief The load panel: the load command and the LoadPanel singleton open and close it below
 * the fps panel, its rows show LoadSamples and LoadSummary, and the engine samples the time the
 * threads worked every frame.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include "../../../../src/jobs/jobs.h"

/** Read the clock of ecs_os_get_time.
 *
 * @return Seconds
 */
static double load_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** System that works 4 ms, then waits 15 ms with box_wait_until, which does not count as work.
 *
 * @param it Iterator of the system
 */
static void LoadWorkAndWait(
    ecs_iter_t *it)
{
    double start = load_now();
    (void)it;
    while (load_now() - start < 0.004) { }
    box_wait_until(load_now() + 0.015);
}

/** Job work that runs 5 ms and then marks its int32_t context done.
 *
 * @param ctx The done flag
 */
static void load_job_work(
    void *ctx)
{
    double start = load_now();
    while (load_now() - start < 0.005) { }
    *(int32_t*)ctx = 1;
}

void LoadPanel_closed_by_default(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);

    test_bool(ecs_singleton_get(world, LoadPanel)->open, false);
    test_assert(ecs_lookup(world, BOX_LOAD_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_UI) == 0);

    ecs_fini(world);
}

void LoadPanel_command_toggles(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;

    char *out = box_command_run(world, s, "load", &error);
    test_bool(error, false);
    test_str(out, "true");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, LoadPanel)->open, true);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t panel = ecs_lookup(world, BOX_LOAD_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has_id(world, panel, ecs_lookup(world, BOX_LOAD_TEMPLATE)));
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) != 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) == 0);
    const Text *heading = ecs_get(world, ecs_lookup(world, BOX_LOAD_HEADING), Text);
    test_assert(heading != NULL);
    test_str(heading->value, "Thread load");
    test_assert(ecs_get(world, ecs_lookup(world, BOX_FPS_SCRIPT), EcsScript)->error == NULL);

    out = box_command_run(world, s, "load", &error);
    test_bool(error, false);
    test_str(out, "false");
    ecs_os_free(out);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_LOAD_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    out = box_command_run(world, s, "help load", NULL);
    test_assert(out != NULL);
    test_assert(strstr(out, "load panel") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void LoadPanel_rows(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const char *rows[BOX_LOAD_ROWS] = {
        BOX_LOAD_MAIN, BOX_LOAD_RENDER, BOX_LOAD_GPU, BOX_LOAD_AUDIO, BOX_LOAD_JOBS
    };
    const char *names[BOX_LOAD_ROWS] = {
        "Main thread", "Render CPU", "Render GPU wait", "Audio", "Jobs, busiest of"
    };
    char path[256];
    for (int32_t i = 0; i < BOX_LOAD_ROWS; i ++) {
        snprintf(path, sizeof(path), "%s.head.name", rows[i]);
        const Text *name = ecs_get(world, ecs_lookup(world, path), Text);
        test_assert(name != NULL);
        test_assert(strncmp(name->value, names[i], strlen(names[i])) == 0);

        snprintf(path, sizeof(path), "%s.chart", rows[i]);
        ecs_entity_t chart = ecs_lookup(world, path);
        test_assert(chart != 0);
        test_assert(ecs_has_id(world, chart, ecs_lookup(world, BOX_FPS_CHART_TEMPLATE)));
        test_assert(ecs_lookup_child(world, chart, "line_60") != 0);
        test_assert(ecs_lookup_child(world, chart, "line_30") != 0);

        snprintf(path, sizeof(path), "%s.chart.bars", rows[i]);
        ecs_entity_t bars = ecs_lookup(world, path);
        test_assert(bars != 0);
        int32_t count = 0;
        ecs_iter_t it = ecs_children(world, bars);
        while (ecs_children_next(&it)) {
            count += it.count;
        }

        test_int(count, BOX_FPS_BARS);
    }

    ecs_fini(world);
}

void LoadPanel_below_fps_panel(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_singleton_set(world, FpsPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t fps = ecs_lookup(world, BOX_FPS_PANEL);
    ecs_entity_t load = ecs_lookup(world, BOX_LOAD_PANEL);
    test_assert(fps != 0);
    test_assert(load != 0);
    const LayoutBounds *fb = ecs_get(world, fps, LayoutBounds);
    const LayoutBounds *lb = ecs_get(world, load, LayoutBounds);
    const Position *fp = ecs_get(world, fps, Position);
    const Position *lp = ecs_get(world, load, Position);
    test_assert(fb != NULL && lb != NULL && fp != NULL && lp != NULL);
    test_flt(fb->width, lb->width);
    test_flt(fp->x + fb->width * 0.5f, 1280 * 0.5f - BOX_FPS_MARGIN);
    test_flt(lp->x + lb->width * 0.5f, 1280 * 0.5f - BOX_FPS_MARGIN);
    test_flt(fp->y + fb->height * 0.5f, 720 * 0.5f - BOX_FPS_MARGIN);
    test_flt(lp->y + lb->height * 0.5f, fp->y - fb->height * 0.5f - BOX_FPS_MARGIN);

    ecs_singleton_set(world, FpsPanel, { .open = false });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) == 0);
    test_uint(ecs_lookup(world, BOX_LOAD_PANEL), load);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) != 0);
    lp = ecs_get(world, load, Position);
    test_flt(lp->y + lb->height * 0.5f, 720 * 0.5f - BOX_FPS_MARGIN);

    ecs_fini(world);
}

void LoadPanel_bars_follow_samples(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    LoadSamples *samples = ecs_singleton_get_mut(world, LoadSamples);
    samples->main.hold = samples->render.hold = samples->gpu.hold = true;
    samples->audio.hold = samples->jobs.hold = true;
    ecs_singleton_modified(world, LoadSamples);
    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    samples = ecs_singleton_get_mut(world, LoadSamples);
    box_frame_samples_add(&samples->render, 10);
    box_frame_samples_add(&samples->render, 40);
    ecs_singleton_modified(world, LoadSamples);
    ecs_progress(world, 0);

    ecs_entity_t newest = ecs_lookup(world, BOX_LOAD_RENDER ".chart.bars.bar_119");
    ecs_entity_t before = ecs_lookup(world, BOX_LOAD_RENDER ".chart.bars.bar_118");
    ecs_entity_t empty = ecs_lookup(world, BOX_LOAD_RENDER ".chart.bars.bar_117");
    test_assert(newest != 0 && before != 0 && empty != 0);
    test_flt(ecs_get(world, newest, Rect)->height, 48);
    test_flt(ecs_get(world, before, Rect)->height, 12);
    test_flt(ecs_get(world, empty, Rect)->height, 0);
    const Rgb *red = ecs_get(world, newest, Rgb);
    test_int(red->r, BOX_FPS_RED.r);
    test_int(red->g, BOX_FPS_RED.g);
    const Rgb *green = ecs_get(world, before, Rgb);
    test_int(green->r, BOX_FPS_GREEN.r);
    test_int(green->g, BOX_FPS_GREEN.g);

    ecs_entity_t other = ecs_lookup(world, BOX_LOAD_MAIN ".chart.bars.bar_119");
    test_flt(ecs_get(world, other, Rect)->height, 0);

    ecs_fini(world);
}

void LoadPanel_summary_text(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    FrameSamples *frames = ecs_singleton_get_mut(world, FrameSamples);
    frames->hold = true;
    frames->count = 0;
    frames->next = 0;
    for (int32_t i = 0; i < 60; i ++) {
        box_frame_samples_add(frames, 20);
    }

    ecs_singleton_modified(world, FrameSamples);
    LoadSamples *samples = ecs_singleton_get_mut(world, LoadSamples);
    samples->main.hold = samples->render.hold = samples->gpu.hold = true;
    samples->audio.hold = samples->jobs.hold = true;
    for (int32_t i = 0; i < 49; i ++) {
        box_frame_samples_add(&samples->main, 5);
    }

    box_frame_samples_add(&samples->main, 11);
    for (int32_t i = 0; i < 50; i ++) {
        box_frame_samples_add(&samples->gpu, 18);
    }

    ecs_singleton_modified(world, LoadSamples);
    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const LoadSummary *sum = ecs_singleton_get(world, LoadSummary);
    test_int(sum->frames, 50);
    test_flt(sum->main.mean_ms, 5.12);
    test_flt(sum->main.worst_ms, 11);
    test_flt(sum->main.share, 25.6);
    test_flt(sum->gpu.share, 90);
    test_flt(sum->render.mean_ms, 0);

    const Text *times = ecs_get(world, ecs_lookup(world, BOX_LOAD_MAIN ".head.times"), Text);
    const Text *percent = ecs_get(world, ecs_lookup(world, BOX_LOAD_MAIN ".head.percent"), Text);
    test_str(times->value, "mean 5.1 ms, worst 11.0 ms");
    test_str(percent->value, "26%");
    const Rgb *color = ecs_get(world, ecs_lookup(world, BOX_LOAD_GPU ".head.percent"), Rgb);
    test_int(color->r, BOX_FPS_YELLOW.r);
    test_int(color->g, BOX_FPS_YELLOW.g);

    ecs_fini(world);
}

void LoadPanel_summarize(void) {
    // fps/load
    FrameSamples frames = {0};
    LoadSamples samples = {0};
    LoadSummary sum;
    for (int32_t i = 0; i < 200; i ++) {
        box_frame_samples_add(&frames, 10);
        box_frame_samples_add(&samples.render, 2);
    }

    for (int32_t i = 0; i < 50; i ++) {
        box_frame_samples_add(&samples.audio, 1);
    }

    box_frame_samples_add(&samples.jobs, 30);
    box_load_summarize(&frames, &samples, 3, &sum);
    test_int(sum.frames, 100);
    test_int(sum.workers, 3);
    test_flt(sum.render.mean_ms, 2);
    test_flt(sum.render.worst_ms, 2);
    test_flt(sum.render.share, 20);
    test_flt(sum.audio.mean_ms, 1);
    test_flt(sum.audio.share, 10);
    test_flt(sum.jobs.mean_ms, 30);
    test_flt(sum.jobs.share, 300);
    test_flt(sum.main.mean_ms, 0);
    test_flt(sum.main.share, 0);

    box_load_summarize(NULL, &samples, 0, &sum);
    test_int(sum.frames, 0);
    test_flt(sum.render.mean_ms, 0);
    test_flt(sum.render.share, 0);
}

void LoadPanel_engine_records_samples(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const LoadSamples *samples = ecs_singleton_get(world, LoadSamples);
    test_int(samples->main.count, 3);
    test_int(samples->render.count, 3);
    test_int(samples->gpu.count, 3);
    test_int(samples->audio.count, 3);
    test_int(samples->jobs.count, 3);
    test_assert(box_frame_sample(&samples->main, 0) > 0);

    LoadSamples *held = ecs_singleton_get_mut(world, LoadSamples);
    held->main.hold = true;
    ecs_singleton_modified(world, LoadSamples);
    ecs_progress(world, 0);
    samples = ecs_singleton_get(world, LoadSamples);
    test_int(samples->main.count, 3);
    test_int(samples->render.count, 4);

    ecs_fini(world);
}

void LoadPanel_main_excludes_waits(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "WorkAndWait" }),
        .phase = EcsOnUpdate,
        .run = LoadWorkAndWait
    });

    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const LoadSamples *samples = ecs_singleton_get(world, LoadSamples);
    const FrameSamples *frames = ecs_singleton_get(world, FrameSamples);
    double main = box_frame_sample(&samples->main, 0);
    test_assert(main >= 4);
    test_assert(main < 15);
    test_assert(box_frame_sample(frames, 0) >= 19);

    ecs_fini(world);
}

void LoadPanel_jobs_busiest_worker(void) {
    // fps/load
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t done = 0;
    ecs_progress(world, 0);
    test_int(box_job_submit(world, &(box_job_desc_t){
        .work = load_job_work,
        .ctx = &done
    }), 0);

    for (int32_t i = 0; i < 1000 && box_jobs_pending(world); i ++) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    test_int(done, 1);
    const LoadSamples *samples = ecs_singleton_get(world, LoadSamples);
    double sum = 0;
    for (int32_t age = 0; age < samples->jobs.count; age ++) {
        sum += box_frame_sample(&samples->jobs, age);
    }

    test_assert(sum >= 5);

    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, LoadSummary)->workers > 0);

    ecs_fini(world);
}
