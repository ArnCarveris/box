/**
 * @file StatsPanels.c
 * @brief Tests for the statistics panels, the singletons they read and their collection.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/surface/surface_internal.h"
#include "../../../src/renderer/counters.h"

void StatsPanels_panels_are_hidden_by_default(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_lookup(world, "box.monitor.stats.renderer") != 0);
    test_assert(ecs_lookup(world, "box.monitor.stats.gpu") != 0);
    test_bool(ecs_mut_var_get_t(world, "box.monitor.stats.renderer", ecs_bool_t), false);
    test_bool(ecs_mut_var_get_t(world, "box.monitor.stats.gpu", ecs_bool_t), false);
    ecs_progress(world, 0.02f);
    test_assert(ecs_lookup(world, "box.monitor.stats.panel") == 0);
    test_assert(ecs_singleton_get(world, RendererStats) == NULL);
    test_assert(ecs_singleton_get(world, GpuTimings) == NULL);
    ecs_fini(world);
}

void StatsPanels_templates_are_loaded(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_lookup(world, "box.monitor.panels.StatsPanel") != 0);
    test_assert(ecs_lookup(world, "box.monitor.panels.StatRow") != 0);
    test_assert(ecs_lookup(world, "box.monitor.panels.RendererPanel") != 0);
    test_assert(ecs_lookup(world, "box.monitor.panels.GpuPanel") != 0);
    ecs_fini(world);
}

void StatsPanels_showing_a_panel_adds_its_singleton(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t counting = ecs_lookup(world, "box.monitor.RendererCounting");
    test_assert(counting != 0);
    test_bool(ecs_has_id(world, counting, counting), false);

    ecs_mut_var_set_t(world, "box.monitor.stats.renderer", ecs_bool_t, {true});
    test_assert(ecs_singleton_get(world, RendererStats) != NULL);
    test_bool(ecs_has_id(world, counting, counting), true);
    test_assert(ecs_lookup(world,
        "box.monitor.stats.panel.screen.stack.renderer_stats") != 0);

    ecs_mut_var_set_t(world, "box.monitor.stats.renderer", ecs_bool_t, {false});
    test_assert(ecs_singleton_get(world, RendererStats) == NULL);
    test_bool(ecs_has_id(world, counting, counting), false);
    test_assert(ecs_lookup(world, "box.monitor.stats.panel") == 0);
    ecs_fini(world);
}

void StatsPanels_gpu_panel_adds_gpu_timings(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {true});
    test_assert(ecs_singleton_get(world, GpuTimings) != NULL);
    test_assert(ecs_singleton_get(world, RendererStats) == NULL);
    test_assert(ecs_lookup(world, "box.monitor.stats.panel.screen.stack.gpu_stats") != 0);

    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {false});
    test_assert(ecs_singleton_get(world, GpuTimings) == NULL);
    ecs_fini(world);
}

void StatsPanels_hiding_keeps_a_singleton_added_elsewhere(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, GpuTimings);
    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {true});
    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {false});
    test_assert(ecs_singleton_get(world, GpuTimings) != NULL);
    ecs_fini(world);
}

void StatsPanels_removing_the_singleton_stops_counting(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t counting = ecs_lookup(world, "box.monitor.RendererCounting");
    ecs_singleton_add(world, RendererStats);
    test_bool(ecs_has_id(world, counting, counting), true);
    ecs_singleton_remove(world, RendererStats);
    test_bool(ecs_has_id(world, counting, counting), false);
    ecs_fini(world);
}

void StatsPanels_renderer_panel_shows_the_counts(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.renderer", ecs_bool_t, {true});
    ecs_singleton_set(world, RendererStats, {
        .frame = 12, .draw_calls = 42, .triangles = 1234567,
        .gpu_memory = 3 * 1024 * 1024 / 2
    });
    ecs_progress(world, 0.02f);

    ecs_entity_t panel = ecs_lookup(world, "box.monitor.stats.panel.screen.stack.renderer_stats");
    test_assert(panel != 0);
    const Text *heading = ecs_get(world, ecs_lookup_child(world, panel, "heading"), Text);
    test_str(heading->value, "Renderer");
    ecs_entity_t draws = ecs_lookup_child(world, panel, "draw_calls");
    const Text *label = ecs_get(world, ecs_lookup_child(world, draws, "name"), Text);
    const Text *value = ecs_get(world, ecs_lookup_child(world, draws, "amount"), Text);
    test_str(label->value, "Draw calls");
    test_str(value->value, "42");
    ecs_entity_t tris = ecs_lookup_child(world, panel, "triangles");
    test_str(ecs_get(world, ecs_lookup_child(world, tris, "amount"), Text)->value, "1234567");
    ecs_entity_t mem = ecs_lookup_child(world, panel, "gpu_memory");
    test_str(ecs_get(world, ecs_lookup_child(world, mem, "amount"), Text)->value, "1.5 MB");

    ecs_singleton_set(world, RendererStats, { .frame = 13, .draw_calls = 7 });
    test_str(ecs_get(world, ecs_lookup_child(world, draws, "amount"), Text)->value, "7");
    ecs_fini(world);
}

void StatsPanels_gpu_panel_lists_the_passes(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {true});
    ecs_entity_t panel = ecs_lookup(world, "box.monitor.stats.panel.screen.stack.gpu_stats");
    test_assert(ecs_lookup_child(world, panel, "unsupported") != 0);

    GpuTimings *g = ecs_singleton_ensure(world, GpuTimings);
    g->supported = true;
    g->pass_count = 2;
    g->frame_ms = 1.5;
    g->passes[0].name = ecs_os_strdup("shadow");
    g->passes[0].time_ms = 0.25;
    g->passes[1].name = ecs_os_strdup("opaque");
    g->passes[1].time_ms = 1.25;
    ecs_singleton_modified(world, GpuTimings);

    test_assert(ecs_lookup_child(world, panel, "unsupported") == 0);
    ecs_entity_t total = ecs_lookup_child(world, panel, "total");
    test_str(ecs_get(world, ecs_lookup_child(world, total, "amount"), Text)->value, "1.50 ms");
    ecs_entity_t second = ecs_lookup_child(world, panel, "pass_1");
    test_assert(second != 0);
    test_str(ecs_get(world, ecs_lookup_child(world, second, "name"), Text)->value, "opaque");
    test_str(ecs_get(world, ecs_lookup_child(world, second, "amount"), Text)->value,
        "1.250 ms");
    test_assert(ecs_lookup_child(world, panel, "pass_2") == 0);
    ecs_fini(world);
}

void StatsPanels_panels_stack_in_one_column(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Surface, {.width = 960, .height = 540});
    ecs_mut_var_set_t(world, "box.monitor.stats.gpu", ecs_bool_t, {true});
    ecs_mut_var_set_t(world, "box.monitor.stats.renderer", ecs_bool_t, {true});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0.02f);
    }

    const Bounds *renderer = ecs_get(world, ecs_lookup(world,
        "box.monitor.stats.panel.screen.stack.renderer_stats"), Bounds);
    const Bounds *gpu = ecs_get(world, ecs_lookup(world,
        "box.monitor.stats.panel.screen.stack.gpu_stats"), Bounds);
    test_assert(renderer != NULL);
    test_assert(gpu != NULL);
    test_assert(renderer->height > 0);
    test_assert(gpu->height > 0);
    test_flt(renderer->x - renderer->width / 2.0f, gpu->x - gpu->width / 2.0f);
    float top_first = renderer->y + renderer->height / 2.0f;
    float top_second = gpu->y + gpu->height / 2.0f;
    float bottom_first = renderer->y - renderer->height / 2.0f;
    float bottom_second = gpu->y - gpu->height / 2.0f;
    bool stacked = bottom_first >= top_second || bottom_second >= top_first;
    test_bool(stacked, true);
    ecs_fini(world);
}

void StatsPanels_a_surface_world_publishes_the_counts(void)
{
    // monitoring/stats_command
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Surface, {.width = 128, .height = 128});
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 4, -12});
    ecs_set(world, cam, LookAt, {0, 0, 0});
    ecs_set(world, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0.5f, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_singleton_add(world, RendererStats);

    const RendererStats *s = NULL;
    int32_t i;
    for (i = 0; i < 200; i ++) {
        ecs_progress(world, 0.02f);
        s = ecs_singleton_get(world, RendererStats);
        if (s->draw_calls) {
            break;
        }
    }

    const SurfaceImpl *impl = ecs_get(world, ecs_id(Surface), SurfaceImpl);
    if (impl && impl->renderer) {
        test_assert(s->draw_calls >= 1);
        test_assert(s->render_passes >= 1);
        test_assert(s->instances >= 1);
        test_assert(s->triangles >= 12);
        test_assert(s->buffers > 0);
        test_assert(s->textures > 0);
        test_assert(s->gpu_memory >= s->buffer_memory + s->texture_memory);
        test_bool(s->indirect_resolved, true);
    }

    ecs_fini(world);
}

void StatsPanels_detail_panels_add_their_singletons(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const char *vars[] = { "pass", "pipeline", "bindgroup", "buffer", "compute" };
    ecs_entity_t comps[] = { ecs_id(PassStats), ecs_id(PipelineStats),
        ecs_id(BindGroupStats), ecs_id(BufferStats), ecs_id(ComputeStats) };
    const char *panels[] = { "pass_stats", "pipeline_stats", "bindgroup_stats",
        "buffer_stats", "compute_stats" };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        char var[64], path[128];
        ecs_os_snprintf(var, sizeof(var), "box.monitor.stats.%s", vars[i]);
        ecs_os_snprintf(path, sizeof(path), "box.monitor.stats.panel.screen.stack.%s",
            panels[i]);
        test_bool(ecs_has_id(world, comps[i], comps[i]), false);
        ecs_mut_var_set_t(world, var, ecs_bool_t, {true});
        test_bool(ecs_has_id(world, comps[i], comps[i]), true);
        test_assert(ecs_lookup(world, path) != 0);
        ecs_mut_var_set_t(world, var, ecs_bool_t, {false});
        test_bool(ecs_has_id(world, comps[i], comps[i]), false);
        test_assert(ecs_lookup(world, path) == 0);
    }

    ecs_fini(world);
}

void StatsPanels_counting_asks_for_what_the_singletons_need(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t counting = ecs_lookup(world, "box.monitor.RendererCounting");
    ecs_singleton_add(world, PipelineStats);
    const uint32_t *flags = ecs_get_id(world, counting, counting);
    test_assert(flags != NULL);
    test_int(*flags, BOX_COUNT_PIPELINES);
    ecs_singleton_add(world, RendererStats);
    ecs_singleton_add(world, BufferStats);
    flags = ecs_get_id(world, counting, counting);
    test_int(*flags, BOX_COUNT_PIPELINES | BOX_COUNT_FRAME | BOX_COUNT_BUFFERS);
    ecs_singleton_remove(world, PipelineStats);
    flags = ecs_get_id(world, counting, counting);
    test_int(*flags, BOX_COUNT_FRAME | BOX_COUNT_BUFFERS);
    ecs_singleton_remove(world, RendererStats);
    ecs_singleton_remove(world, BufferStats);
    test_bool(ecs_has_id(world, counting, counting), false);
    ecs_fini(world);
}

void StatsPanels_pass_panel_lists_the_passes(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.pass", ecs_bool_t, {true});
    PassStats *s = ecs_singleton_ensure(world, PassStats);
    s->total = 2;
    s->count = 2;
    s->passes[0].name = ecs_os_strdup("cull");
    s->passes[0].compute = true;
    s->passes[0].dispatches = 3;
    s->passes[1].name = ecs_os_strdup("opaque");
    s->passes[1].draw_calls = 5;
    s->passes[1].triangles = 600;
    s->passes[1].pipeline_switches = 2;
    s->passes[1].bind_group_changes = 4;
    ecs_singleton_modified(world, PassStats);

    ecs_entity_t panel = ecs_lookup(world, "box.monitor.stats.panel.screen.stack.pass_stats");
    ecs_entity_t header = ecs_lookup_child(world, panel, "header");
    test_str(ecs_get(world, ecs_lookup_child(world, header, "name_cell"), Text)->value,
        "pass (2/2)");
    ecs_entity_t rows = ecs_lookup_child(world, panel, "rows");
    ecs_entity_t cull = ecs_lookup_child(world, rows, "pass_0");
    test_str(ecs_get(world, ecs_lookup_child(world, cull, "a_cell"), Text)->value, "3");
    ecs_entity_t opaque = ecs_lookup_child(world, rows, "pass_1");
    test_str(ecs_get(world, ecs_lookup_child(world, opaque, "name_cell"), Text)->value,
        "opaque");
    test_str(ecs_get(world, ecs_lookup_child(world, opaque, "a_cell"), Text)->value, "5");
    test_str(ecs_get(world, ecs_lookup_child(world, opaque, "b_cell"), Text)->value, "600");
    test_str(ecs_get(world, ecs_lookup_child(world, opaque, "c_cell"), Text)->value, "2");
    test_str(ecs_get(world, ecs_lookup_child(world, opaque, "d_cell"), Text)->value, "4");
    const Rgb *compute = ecs_get(world, ecs_lookup_child(world, cull, "name_cell"), Rgb);
    const Rgb *render = ecs_get(world, ecs_lookup_child(world, opaque, "name_cell"), Rgb);
    test_assert(compute->r != render->r || compute->b != render->b);
    ecs_fini(world);
}

void StatsPanels_tables_end_with_their_total(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.pipeline", ecs_bool_t, {true});
    ecs_progress(world, 0.02f);
    PipelineStats *s = ecs_singleton_ensure(world, PipelineStats);
    s->total = 1;
    s->count = 1;
    s->sets = 9;
    s->switches = 4;
    s->pipelines[0].name = ecs_os_strdup("mesh");
    s->pipelines[0].sets = 9;
    s->pipelines[0].switches = 4;
    ecs_singleton_modified(world, PipelineStats);

    ecs_entity_t panel = ecs_lookup(world,
        "box.monitor.stats.panel.screen.stack.pipeline_stats");
    ecs_entity_t kids[8];
    int32_t count = 0;
    ecs_iter_t it = ecs_children(world, panel);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && count < 8; i ++) {
            kids[count ++] = it.entities[i];
        }
    }

    test_int(count, 4);
    test_str(ecs_get_name(world, kids[0]), "heading");
    test_str(ecs_get_name(world, kids[1]), "header");
    test_str(ecs_get_name(world, kids[2]), "rows");
    test_str(ecs_get_name(world, kids[3]), "total");
    ecs_entity_t row = ecs_lookup_child(world, kids[2], "pipeline_0");
    test_str(ecs_get(world, ecs_lookup_child(world, row, "name_cell"), Text)->value, "mesh");
    test_str(ecs_get(world, ecs_lookup_child(world, kids[3], "b_cell"), Text)->value, "4");
    ecs_fini(world);
}

void StatsPanels_buffer_panel_shows_the_used_classes(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_mut_var_set_t(world, "box.monitor.stats.buffer", ecs_bool_t, {true});
    BufferStats *s = ecs_singleton_ensure(world, BufferStats);
    s->buffers = 3;
    s->bytes = 2 * 1024 * 1024;
    s->usages[3].usage = ecs_os_strdup("uniform");
    s->usages[3].buffers = 3;
    s->usages[3].bytes = 2 * 1024 * 1024;
    s->usages[3].uploads = 2;
    s->usages[3].upload_bytes = 2048;
    ecs_singleton_modified(world, BufferStats);

    ecs_entity_t panel = ecs_lookup(world, "box.monitor.stats.panel.screen.stack.buffer_stats");
    ecs_entity_t rows = ecs_lookup_child(world, panel, "rows");
    test_assert(ecs_lookup_child(world, rows, "usage_0") == 0);
    ecs_entity_t uniform = ecs_lookup_child(world, rows, "usage_3");
    test_assert(uniform != 0);
    test_str(ecs_get(world, ecs_lookup_child(world, uniform, "name_cell"), Text)->value,
        "uniform");
    test_str(ecs_get(world, ecs_lookup_child(world, uniform, "b_cell"), Text)->value, "2.00");
    test_str(ecs_get(world, ecs_lookup_child(world, uniform, "d_cell"), Text)->value, "2.0");
    ecs_fini(world);
}

void StatsPanels_a_surface_world_publishes_the_details(void)
{
    // monitoring/stats_details
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Surface, {.width = 128, .height = 128});
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 4, -12});
    ecs_set(world, cam, LookAt, {0, 0, 0});
    ecs_set(world, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0.5f, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_singleton_add(world, RendererStats);
    ecs_singleton_add(world, PassStats);
    ecs_singleton_add(world, PipelineStats);
    ecs_singleton_add(world, BindGroupStats);
    ecs_singleton_add(world, BufferStats);
    ecs_singleton_add(world, ComputeStats);

    int32_t i;
    for (i = 0; i < 200; i ++) {
        ecs_progress(world, 0.02f);
        const RendererStats *r = ecs_singleton_get(world, RendererStats);
        const PipelineStats *p = ecs_singleton_get(world, PipelineStats);
        if (r->draw_calls && p->count && r->frame == p->frame) {
            break;
        }
    }

    const SurfaceImpl *impl = ecs_get(world, ecs_id(Surface), SurfaceImpl);
    if (impl && impl->renderer) {
        const RendererStats *r = ecs_singleton_get(world, RendererStats);
        const PassStats *passes = ecs_singleton_get(world, PassStats);
        const PipelineStats *pipelines = ecs_singleton_get(world, PipelineStats);
        const BindGroupStats *binds = ecs_singleton_get(world, BindGroupStats);
        const BufferStats *buffers = ecs_singleton_get(world, BufferStats);
        test_int(passes->frame, r->frame);
        test_int(pipelines->frame, r->frame);
        test_assert(passes->count > 0);
        int32_t draws = 0;
        for (i = 0; i < passes->count; i ++) {
            test_assert(passes->passes[i].name != NULL);
            draws += passes->passes[i].draw_calls;
        }

        test_int(draws, r->draw_calls);
        test_int(pipelines->switches, r->pipeline_switches);
        for (i = 1; i < pipelines->count; i ++) {
            test_assert(pipelines->pipelines[i - 1].switches >=
                pipelines->pipelines[i].switches);
        }

        test_int(binds->changes, r->bind_group_changes);
        test_int(buffers->uploads, r->buffer_uploads);
        test_int(buffers->buffers, r->buffers);
        test_str(buffers->usages[3].usage, "uniform");
    }

    ecs_fini(world);
}
