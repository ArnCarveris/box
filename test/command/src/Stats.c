/**
 * @file Stats.c
 * @brief Tests for the stats command: the panels it lists, switches and prints.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Stats_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Stats_teardown(void)
{
    ecs_fini(w);
}

void Stats_lists_the_panels(void)
{
    // monitoring/stats_command
    bool error = true;
    char *out = box_command_run(w, session, "stats", &error);
    test_bool(error, false);
    test_str(out,
        "renderer   off  draw calls, passes, state changes, uploads and memory per frame\n"
        "gpu        off  GPU time of every pass\n"
        "pass       off  draws, triangles and state changes per pass\n"
        "pipeline   off  sets, switches and draws per pipeline\n"
        "bindgroup  off  bind group sets and changes per pass and slot\n"
        "buffer     off  live buffers and uploads per usage\n"
        "compute    off  dispatches and workgroups per compute pass\n"
        "systems    off  min, average and max time of the ten slowest ECS systems over the"
        " last second\n");
    ecs_os_free(out);
}

void Stats_naming_a_panel_shows_it(void)
{
    // monitoring/stats_command
    bool error = true;
    char *out = box_command_run(w, session, "stats renderer", &error);
    test_bool(error, false);
    test_str(out, "renderer panel on\n");
    ecs_os_free(out);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.stats.renderer", ecs_bool_t), true);
    test_assert(ecs_singleton_get(w, RendererStats) != NULL);
    test_assert(ecs_lookup(w, "box.monitor.stats.panel.screen.stack.renderer_stats") != 0);

    out = box_command_run(w, session, "stats", &error);
    test_assert(strstr(out, "renderer   on ") != NULL);
    ecs_os_free(out);
}

void Stats_naming_a_shown_panel_prints_its_counts(void)
{
    // monitoring/stats_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats renderer", &error));
    ecs_singleton_set(w, RendererStats, {
        .frame = 9, .draw_calls = 42, .render_passes = 3, .triangles = 5000,
        .gpu_memory = 2 * 1024 * 1024
    });

    char *out = box_command_run(w, session, "stats renderer", &error);
    test_bool(error, false);
    test_assert(strstr(out, "frame                9\n") != NULL);
    test_assert(strstr(out, "draw calls           42\n") != NULL);
    test_assert(strstr(out, "render passes        3\n") != NULL);
    test_assert(strstr(out, "triangles rendered   5000\n") != NULL);
    test_assert(strstr(out, "gpu memory           2.0 MB\n") != NULL);
    ecs_os_free(out);
}

void Stats_gpu_prints_the_passes(void)
{
    // monitoring/stats_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats gpu", &error));
    char *out = box_command_run(w, session, "stats gpu", &error);
    test_str(out, "the device has no timestamp queries\n");
    ecs_os_free(out);

    GpuTimings *g = ecs_singleton_ensure(w, GpuTimings);
    g->supported = true;
    g->frame = 4;
    g->pass_count = 1;
    g->frame_ms = 0.5;
    g->passes[0].name = ecs_os_strdup("opaque");
    g->passes[0].time_ms = 0.5;
    ecs_singleton_modified(w, GpuTimings);

    out = box_command_run(w, session, "stats gpu", &error);
    test_bool(error, false);
    test_str(out,
        "frame                4\n"
        "gpu time             0.500 ms\n"
        "opaque               0.500 ms\n");
    ecs_os_free(out);
}

void Stats_off_hides_the_panel(void)
{
    // monitoring/stats_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats gpu on", &error));
    test_assert(ecs_singleton_get(w, GpuTimings) != NULL);
    char *out = box_command_run(w, session, "stats gpu off", &error);
    test_bool(error, false);
    test_str(out, "gpu panel off\n");
    ecs_os_free(out);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.stats.gpu", ecs_bool_t), false);
    test_assert(ecs_singleton_get(w, GpuTimings) == NULL);
    test_assert(ecs_lookup(w, "box.monitor.stats.panel") == 0);
}

void Stats_off_hides_every_panel(void)
{
    // monitoring/stats_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats gpu", &error));
    ecs_os_free(box_command_run(w, session, "stats renderer", &error));
    char *out = box_command_run(w, session, "stats off", &error);
    test_bool(error, false);
    test_str(out, "stats panels off\n");
    ecs_os_free(out);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.stats.gpu", ecs_bool_t), false);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.stats.renderer", ecs_bool_t), false);
    test_assert(ecs_singleton_get(w, GpuTimings) == NULL);
    test_assert(ecs_singleton_get(w, RendererStats) == NULL);
}

void Stats_unknown_panel_is_an_error(void)
{
    // monitoring/stats_command
    bool error = false;
    char *out = box_command_run(w, session, "stats nope", &error);
    test_bool(error, true);
    test_str(out, "no statistics panel 'nope'");
    ecs_os_free(out);
}

void Stats_unknown_state_is_an_error(void)
{
    // monitoring/stats_command
    bool error = false;
    char *out = box_command_run(w, session, "stats renderer maybe", &error);
    test_bool(error, true);
    test_str(out, "expected 'on' or 'off', got 'maybe'");
    ecs_os_free(out);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.stats.renderer", ecs_bool_t), false);
    test_assert(ecs_singleton_get(w, RendererStats) == NULL);
}

void Stats_help_lists_the_command(void)
{
    // monitoring/stats_command
    bool error = true;
    char *out = box_command_run(w, session, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "stats") != NULL);
    ecs_os_free(out);
}

void Stats_pass_prints_a_row_per_pass(void)
{
    // monitoring/stats_details
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats pass", &error));
    PassStats *s = ecs_singleton_ensure(w, PassStats);
    s->frame = 3;
    s->total = 1;
    s->count = 1;
    s->passes[0].name = ecs_os_strdup("opaque");
    s->passes[0].draw_calls = 5;
    s->passes[0].triangles = 60;
    s->passes[0].pipeline_switches = 2;
    s->passes[0].bind_group_changes = 4;
    ecs_singleton_modified(w, PassStats);

    char *out = box_command_run(w, session, "stats pass", &error);
    test_bool(error, false);
    test_str(out,
        "frame                3\n"
        "pass                          calls       tris      pipes      binds\n"
        "opaque                            5         60          2          4\n");
    ecs_os_free(out);
}

void Stats_bindgroup_prints_the_redundant_sets(void)
{
    // monitoring/stats_details
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats bindgroup", &error));
    BindGroupStats *s = ecs_singleton_ensure(w, BindGroupStats);
    s->frame = 2;
    s->total = 1;
    s->count = 1;
    s->sets = 4;
    s->changes = 3;
    s->slots[0].pass = ecs_os_strdup("opaque");
    s->slots[0].slot = 3;
    s->slots[0].sets = 4;
    s->slots[0].changes = 3;
    ecs_singleton_modified(w, BindGroupStats);

    char *out = box_command_run(w, session, "stats bindgroup", &error);
    test_bool(error, false);
    test_str(out,
        "frame                2\n"
        "pass slot                      sets    changes  redundant           \n"
        "opaque 3                          4          3          1           \n"
        "total                             4          3          1           \n");
    ecs_os_free(out);
}

void Stats_buffer_skips_the_unused_classes(void)
{
    // monitoring/stats_details
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats buffer", &error));
    BufferStats *s = ecs_singleton_ensure(w, BufferStats);
    s->buffers = 2;
    s->bytes = 512;
    s->uploads = 1;
    s->upload_bytes = 64;
    s->usages[3].usage = ecs_os_strdup("uniform");
    s->usages[3].buffers = 2;
    s->usages[3].bytes = 512;
    s->usages[3].uploads = 1;
    s->usages[3].upload_bytes = 64;
    ecs_singleton_modified(w, BufferStats);

    char *out = box_command_run(w, session, "stats buffer", &error);
    test_bool(error, false);
    test_str(out,
        "frame                0\n"
        "usage                       buffers      bytes    uploads   bytes up\n"
        "uniform                           2        512          1         64\n"
        "total                             2        512          1         64\n");
    ecs_os_free(out);
}

void Stats_off_hides_the_detail_panels(void)
{
    // monitoring/stats_details
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats pipeline", &error));
    ecs_os_free(box_command_run(w, session, "stats compute", &error));
    test_assert(ecs_singleton_get(w, PipelineStats) != NULL);
    test_assert(ecs_singleton_get(w, ComputeStats) != NULL);
    ecs_os_free(box_command_run(w, session, "stats off", &error));
    test_assert(ecs_singleton_get(w, PipelineStats) == NULL);
    test_assert(ecs_singleton_get(w, ComputeStats) == NULL);
    test_assert(ecs_lookup(w, "box.monitor.stats.panel") == 0);
}

void Stats_systems_prints_the_slowest_systems(void)
{
    // monitoring/stats_systems
    bool error = true;
    ecs_os_free(box_command_run(w, session, "stats systems", &error));
    test_assert(ecs_singleton_get(w, EcsStats) != NULL);
    test_assert(ecs_lookup(w, "flecs.stats") != 0);

    EcsStats *s = ecs_singleton_ensure(w, EcsStats);
    s->systems_ran = 2;
    s->fps = 50.0;
    s->frame_time_avg_ms = 20.0;
    s->system_time_ms = 1.5;
    s->slowest[0].system = ecs_lookup(w, "box.monitor.BoxCollectSystems");
    s->slowest[0].time_ms = 1.0;
    s->slowest[0].min_ms = 0.5;
    s->slowest[0].max_ms = 2.0;
    s->slowest[1].system = ecs_lookup(w, "box.monitor.BoxPublishFps");
    s->slowest[1].time_ms = 0.5;
    s->slowest[1].min_ms = 0.25;
    s->slowest[1].max_ms = 0.75;
    ecs_singleton_modified(w, EcsStats);

    char *out = box_command_run(w, session, "stats systems", &error);
    test_bool(error, false);
    test_str(out,
        "systems ran          2\n"
        "fps                  50\n"
        "frame                20.000 ms\n"
        "in systems           1.500 ms\n"
        "system                       min ms     avg ms     max ms           \n"
        "BoxCollectSystems             0.500      1.000      2.000           \n"
        "BoxPublishFps                 0.250      0.500      0.750           \n");
    ecs_os_free(out);

    ecs_os_free(box_command_run(w, session, "stats systems off", &error));
    test_assert(ecs_singleton_get(w, EcsStats) == NULL);
    test_bool(ecs_has_id(w, ecs_lookup(w, "flecs.stats"), EcsDisabled), true);
}
