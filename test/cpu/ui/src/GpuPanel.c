/**
 * @file GpuPanel.c
 * @brief The GPU stats panel: the gpu_stats command and the GpuPanel singleton open and close
 * it below the load panel, box.fps writes GpuStats every GpuPanel.interval while it is open and
 * its rows show them, formatted by box.fps.count and box.fps.bytes.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void GpuPanel_closed_by_default(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);

    const GpuPanel *panel = ecs_singleton_get(world, GpuPanel);
    test_assert(panel != NULL);
    test_bool(panel->open, false);
    test_flt(panel->interval, BOX_GPU_INTERVAL);
    test_assert(ecs_lookup(world, BOX_GPU_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 0);

    ecs_fini(world);
}

void GpuPanel_command_toggles(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;

    char *out = box_command_run(world, s, "gpu_stats", &error);
    test_bool(error, false);
    test_str(out, "true");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, GpuPanel)->open, true);
    test_flt(ecs_singleton_get(world, GpuPanel)->interval, BOX_GPU_INTERVAL);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t panel = ecs_lookup(world, BOX_GPU_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has_id(world, panel, ecs_lookup(world, BOX_GPU_TEMPLATE)));
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) != 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_LOAD_PANEL) == 0);
    const Text *heading = ecs_get(world, ecs_lookup(world, BOX_GPU_HEADING ".name"), Text);
    test_assert(heading != NULL);
    test_str(heading->value, "GPU stats");
    test_assert(ecs_get(world, ecs_lookup(world, BOX_FPS_SCRIPT), EcsScript)->error == NULL);
    test_assert(ecs_singleton_get(world, GpuStats)->frames > 0);

    out = box_command_run(world, s, "gpu_stats", &error);
    test_bool(error, false);
    test_str(out, "false");
    ecs_os_free(out);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_GPU_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    out = box_command_run(world, s, "help gpu_stats", NULL);
    test_assert(out != NULL);
    test_assert(strstr(out, "GPU stats panel") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GpuPanel_below_load_panel(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, GpuPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_singleton_set(world, LoadPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_singleton_set(world, FpsPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t fps = ecs_lookup(world, BOX_FPS_PANEL);
    ecs_entity_t load = ecs_lookup(world, BOX_LOAD_PANEL);
    ecs_entity_t gpu = ecs_lookup(world, BOX_GPU_PANEL);
    test_assert(fps != 0 && load != 0 && gpu != 0);
    const LayoutBounds *fb = ecs_get(world, fps, LayoutBounds);
    const LayoutBounds *lb = ecs_get(world, load, LayoutBounds);
    const LayoutBounds *gb = ecs_get(world, gpu, LayoutBounds);
    const Position *fp = ecs_get(world, fps, Position);
    const Position *lp = ecs_get(world, load, Position);
    const Position *gp = ecs_get(world, gpu, Position);
    test_assert(fb && lb && gb && fp && lp && gp);
    test_assert(gb->width > fb->width);
    test_flt(gp->x + gb->width * 0.5f, 1280 * 0.5f - BOX_FPS_MARGIN);
    test_flt(fp->y + fb->height * 0.5f, 720 * 0.5f - BOX_FPS_MARGIN);
    test_flt(lp->y + lb->height * 0.5f, fp->y - fb->height * 0.5f - BOX_FPS_MARGIN);
    test_flt(gp->y + gb->height * 0.5f, lp->y - lb->height * 0.5f - BOX_FPS_MARGIN);

    ecs_singleton_set(world, FpsPanel, { .open = false });
    ecs_singleton_set(world, LoadPanel, { .open = false });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) == 0);
    test_assert(ecs_lookup(world, BOX_LOAD_PANEL) == 0);
    test_uint(ecs_lookup(world, BOX_GPU_PANEL), gpu);
    gp = ecs_get(world, gpu, Position);
    test_flt(gp->y + gb->height * 0.5f, 720 * 0.5f - BOX_FPS_MARGIN);

    ecs_fini(world);
}

void GpuPanel_stats_without_gpu(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_add(world, e, DynamicTransform);
    }

    ecs_singleton_set(world, GpuPanel, { .open = true });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const GpuStats *s = ecs_singleton_get(world, GpuStats);
    test_bool(s->gpu, false);
    test_int(s->frames, 1);
    test_int(s->packets, 0);
    test_int(s->scene.dynamic, 3);
    test_assert(s->scene.instances >= 3);
    test_assert(s->scene.batches >= 1);
    test_flt(s->commands.draws, 0);
    test_flt(s->commands.pipelines, 0);
    test_flt(s->scene.triangles, 0);
    test_int(s->memory.buffers, 0);
    test_bool(s->time.measured, false);

    ecs_fini(world);
}

void GpuPanel_interval(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 1000 });
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 1);

    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 3);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 1);

    ecs_singleton_set(world, GpuPanel, { .open = false });
    ecs_progress(world, 0);
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 1000 });
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, GpuStats)->frames, 1);

    ecs_fini(world);
}

void GpuPanel_rows_show_stats(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_add(world, e, DynamicTransform);
    }

    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const Text *t = ecs_get(world, ecs_lookup(world,
        BOX_GPU_PANEL ".columns.left.scene.instances.amount"), Text);
    test_assert(t != NULL);
    char *expect = flecs_asprintf("%d, 3 dynamic",
        ecs_singleton_get(world, GpuStats)->scene.instances);
    test_str(t->value, expect);
    ecs_os_free(expect);
    t = ecs_get(world, ecs_lookup(world, BOX_GPU_PANEL ".columns.left.scene.batches.name"),
        Text);
    test_assert(t != NULL);
    test_str(t->value, "Batches");
    t = ecs_get(world, ecs_lookup(world, BOX_GPU_PANEL ".columns.middle.commands.draws.amount"),
        Text);
    test_assert(t != NULL);
    test_str(t->value, "0");
    t = ecs_get(world, ecs_lookup(world, BOX_GPU_PANEL ".columns.left.time.cull.amount"), Text);
    test_assert(t != NULL);
    test_str(t->value, "-");
    t = ecs_get(world, ecs_lookup(world, BOX_GPU_HEADING ".window"), Text);
    test_assert(t != NULL);
    test_str(t->value, "no GPU backend");

    ecs_fini(world);
}

void GpuPanel_format_count(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const char *exprs[] = {
        "box.fps.count(0)", "box.fps.count(12)", "box.fps.count(2.5)",
        "box.fps.count(999)", "box.fps.count(150.4)", "box.fps.count(1500)",
        "box.fps.count(1234567)", "box.fps.count(3000000000)"
    };
    const char *expect[] = { "0", "12", "2.5", "999", "150", "1.5k", "1.23M", "3.00G" };

    for (int32_t i = 0; i < 8; i ++) {
        char *v = NULL;
        ecs_value_t value = { .type = ecs_id(ecs_string_t), .ptr = &v };
        test_assert(ecs_expr_run(world, exprs[i], &value, NULL) != NULL);
        test_str(v, expect[i]);
        ecs_os_free(v);
    }

    ecs_fini(world);
}

void GpuPanel_format_bytes(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const char *exprs[] = {
        "box.fps.bytes(512)", "box.fps.bytes(2048)", "box.fps.bytes(3145728)",
        "box.fps.bytes(1610612736)"
    };
    const char *expect[] = { "512 B", "2.0 KB", "3.0 MB", "1.50 GB" };

    for (int32_t i = 0; i < 4; i ++) {
        char *v = NULL;
        ecs_value_t value = { .type = ecs_id(ecs_string_t), .ptr = &v };
        test_assert(ecs_expr_run(world, exprs[i], &value, NULL) != NULL);
        test_str(v, expect[i]);
        ecs_os_free(v);
    }

    ecs_fini(world);
}
