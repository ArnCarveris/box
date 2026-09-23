/**
 * @file command/stats.c
 * @brief The stats command: switches the statistics panels and prints what they show.
 *
 * A panel is switched through its exported mut variable in box.monitor.stats,
 * which box.monitor ties to the singleton the panel reads. Naming a panel that
 * is already shown prints the last numbers it received.
 */

#include "command_internal.h"

/** Scope of the exported mut variables of the statistics panels */
#define BOX_STATS_SCOPE "box.monitor.stats"

/** Print what a panel shows.
 *
 * @param world The world
 * @param buf The output
 */
typedef void (*box_stats_print_t)(
    ecs_world_t *world,
    ecs_strbuf_t *buf);

/** A panel the command switches */
typedef struct box_stats_cmd_panel_t {
    const char *name;        /**< Name of the panel and of its variable */
    const char *brief;       /**< What the panel shows */
    box_stats_print_t print; /**< Prints what it shows */
} box_stats_cmd_panel_t;

/** Print one labelled integer.
 *
 * @param buf The output
 * @param label The label
 * @param value The value
 */
static void box_stats_line(
    ecs_strbuf_t *buf,
    const char *label,
    int64_t value)
{
    ecs_strbuf_append(buf, "%-20s %lld\n", label, (long long)value);
}

/** Print one labelled byte count in megabytes.
 *
 * @param buf The output
 * @param label The label
 * @param bytes The bytes
 */
static void box_stats_mb(
    ecs_strbuf_t *buf,
    const char *label,
    int64_t bytes)
{
    ecs_strbuf_append(buf, "%-20s %.1f MB\n", label, (double)bytes / (1024.0 * 1024.0));
}

/** Print the RendererStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_renderer(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const RendererStats *s = ecs_singleton_get(world, RendererStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_line(buf, "draw calls", s->draw_calls);
    box_stats_line(buf, "render passes", s->render_passes);
    box_stats_line(buf, "pipeline switches", s->pipeline_switches);
    box_stats_line(buf, "bind group changes", s->bind_group_changes);
    box_stats_line(buf, "buffer uploads", s->buffer_uploads);
    box_stats_line(buf, "bytes uploaded", s->bytes_uploaded);
    box_stats_line(buf, "texture uploads", s->texture_uploads);
    box_stats_line(buf, "compute dispatches", s->compute_dispatches);
    box_stats_line(buf, "instances rendered", s->instances);
    box_stats_line(buf, "triangles rendered", s->triangles);
    box_stats_mb(buf, "gpu memory", s->gpu_memory);
    box_stats_mb(buf, "buffer memory", s->buffer_memory);
    box_stats_mb(buf, "texture memory", s->texture_memory);
done:
    return;
}

/** Print the GpuTimings singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_gpu(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const GpuTimings *g = ecs_singleton_get(world, GpuTimings);
    if (!g) {
        goto done;
    }

    if (!g->supported) {
        ecs_strbuf_appendlit(buf, "the device has no timestamp queries\n");
        goto done;
    }

    box_stats_line(buf, "frame", g->frame);
    ecs_strbuf_append(buf, "%-20s %.3f ms\n", "gpu time", g->frame_ms);
    int32_t i;
    for (i = 0; i < g->pass_count && i < BOX_MONITOR_PASSES; i ++) {
        ecs_strbuf_append(buf, "%-20s %.3f ms\n",
            g->passes[i].name ? g->passes[i].name : "", g->passes[i].time_ms);
    }

done:
    return;
}

/** Print one row of a table: a name and up to four values.
 *
 * @param buf The output
 * @param name The name column
 * @param a First value column
 * @param b Second value column
 * @param c Third value column
 * @param d Fourth value column
 */
static void box_stats_row(
    ecs_strbuf_t *buf,
    const char *name,
    const char *a,
    const char *b,
    const char *c,
    const char *d)
{
    ecs_strbuf_append(buf, "%-24s %10s %10s %10s %10s\n", name ? name : "", a, b, c, d);
}

/** Format an integer into a buffer.
 *
 * @param dst The buffer
 * @param size Characters the buffer holds
 * @param value The value
 * @return The buffer
 */
static const char* box_stats_int(
    char *dst,
    size_t size,
    int64_t value)
{
    ecs_os_snprintf(dst, (int)size, "%lld", (long long)value);

    return dst;
}

/** Print the PassStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_pass(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const PassStats *s = ecs_singleton_get(world, PassStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_row(buf, "pass", "calls", "tris", "pipes", "binds");
    int32_t i;
    for (i = 0; i < s->count && i < BOX_MONITOR_STAT_ROWS; i ++) {
        const PassStat *p = &s->passes[i];
        char a[24], b[24], c[24], d[24];
        box_stats_row(buf, p->name, box_stats_int(a, sizeof(a), p->draw_calls + p->dispatches),
            box_stats_int(b, sizeof(b), p->triangles),
            box_stats_int(c, sizeof(c), p->pipeline_switches),
            box_stats_int(d, sizeof(d), p->bind_group_changes));
    }

done:
    return;
}

/** Print the PipelineStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_pipeline(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const PipelineStats *s = ecs_singleton_get(world, PipelineStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_row(buf, "pipeline", "sets", "switches", "calls", "tris");
    int32_t i;
    char a[24], b[24], c[24], d[24];
    for (i = 0; i < s->count && i < BOX_MONITOR_STAT_ROWS; i ++) {
        const PipelineStat *p = &s->pipelines[i];
        box_stats_row(buf, p->name, box_stats_int(a, sizeof(a), p->sets),
            box_stats_int(b, sizeof(b), p->switches),
            box_stats_int(c, sizeof(c), p->draw_calls + p->dispatches),
            box_stats_int(d, sizeof(d), p->triangles));
    }

    box_stats_row(buf, "total", box_stats_int(a, sizeof(a), s->sets),
        box_stats_int(b, sizeof(b), s->switches), "", "");
done:
    return;
}

/** Print the BindGroupStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_bindgroup(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const BindGroupStats *s = ecs_singleton_get(world, BindGroupStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_row(buf, "pass slot", "sets", "changes", "redundant", "");
    int32_t i;
    char name[64], a[24], b[24], c[24];
    for (i = 0; i < s->count && i < BOX_MONITOR_STAT_ROWS; i ++) {
        const BindGroupStat *g = &s->slots[i];
        ecs_os_snprintf(name, sizeof(name), "%s %d", g->pass ? g->pass : "", g->slot);
        box_stats_row(buf, name, box_stats_int(a, sizeof(a), g->sets),
            box_stats_int(b, sizeof(b), g->changes),
            box_stats_int(c, sizeof(c), g->sets - g->changes), "");
    }

    box_stats_row(buf, "total", box_stats_int(a, sizeof(a), s->sets),
        box_stats_int(b, sizeof(b), s->changes),
        box_stats_int(c, sizeof(c), s->sets - s->changes), "");
done:
    return;
}

/** Print the BufferStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_buffer(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const BufferStats *s = ecs_singleton_get(world, BufferStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_row(buf, "usage", "buffers", "bytes", "uploads", "bytes up");
    int32_t i;
    char a[24], b[24], c[24], d[24];
    for (i = 0; i < BOX_MONITOR_USAGES; i ++) {
        const BufferStat *u = &s->usages[i];
        if (!u->buffers && !u->uploads) {
            continue;
        }

        box_stats_row(buf, u->usage, box_stats_int(a, sizeof(a), u->buffers),
            box_stats_int(b, sizeof(b), u->bytes), box_stats_int(c, sizeof(c), u->uploads),
            box_stats_int(d, sizeof(d), u->upload_bytes));
    }

    box_stats_row(buf, "total", box_stats_int(a, sizeof(a), s->buffers),
        box_stats_int(b, sizeof(b), s->bytes), box_stats_int(c, sizeof(c), s->uploads),
        box_stats_int(d, sizeof(d), s->upload_bytes));
done:
    return;
}

/** Print the ComputeStats singleton.
 *
 * @param world The world
 * @param buf The output
 */
static void box_stats_print_compute(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const ComputeStats *s = ecs_singleton_get(world, ComputeStats);
    if (!s) {
        goto done;
    }

    box_stats_line(buf, "frame", s->frame);
    box_stats_row(buf, "pass", "dispatches", "groups", "pipes", "binds");
    int32_t i;
    char a[24], b[24], c[24], d[24];
    for (i = 0; i < s->count && i < BOX_MONITOR_STAT_ROWS; i ++) {
        const ComputeStat *p = &s->passes[i];
        box_stats_row(buf, p->name, box_stats_int(a, sizeof(a), p->dispatches),
            box_stats_int(b, sizeof(b), p->workgroups),
            box_stats_int(c, sizeof(c), p->pipeline_switches),
            box_stats_int(d, sizeof(d), p->bind_group_changes));
    }

    box_stats_row(buf, "total", box_stats_int(a, sizeof(a), s->dispatches),
        box_stats_int(b, sizeof(b), s->workgroups), "", "");
done:
    return;
}

/** The panels the command switches, in the order the listing prints them */
static const box_stats_cmd_panel_t box_stats_panels[] = {
    { "renderer", "draw calls, passes, state changes, uploads and memory per frame",
        box_stats_print_renderer },
    { "gpu", "GPU time of every pass", box_stats_print_gpu },
    { "pass", "draws, triangles and state changes per pass", box_stats_print_pass },
    { "pipeline", "sets, switches and draws per pipeline", box_stats_print_pipeline },
    { "bindgroup", "bind group sets and changes per pass and slot",
        box_stats_print_bindgroup },
    { "buffer", "live buffers and uploads per usage", box_stats_print_buffer },
    { "compute", "dispatches and workgroups per compute pass", box_stats_print_compute }
};

/** Panels the command switches */
#define BOX_STATS_CMD_PANELS \
    ((int32_t)(sizeof(box_stats_panels) / sizeof(box_stats_panels[0])))

/** Whether a panel is shown.
 *
 * @param world The world
 * @param panel The panel
 * @return true when its variable is set
 */
static bool box_stats_visible(
    ecs_world_t *world,
    const box_stats_cmd_panel_t *panel)
{
    char path[128];
    ecs_os_snprintf(path, sizeof(path), BOX_STATS_SCOPE ".%s", panel->name);
    bool result = false;
    if (ecs_lookup(world, path)) {
        result = ecs_mut_var_get_t(world, path, ecs_bool_t);
    }

    return result;
}

/** Show or hide a panel.
 *
 * @param world The world
 * @param panel The panel
 * @param visible Show it
 * @return 0 on success, nonzero when the panel script did not load
 */
static int box_stats_show(
    ecs_world_t *world,
    const box_stats_cmd_panel_t *panel,
    bool visible)
{
    char path[128];
    ecs_os_snprintf(path, sizeof(path), BOX_STATS_SCOPE ".%s", panel->name);
    int result = -1;
    if (ecs_lookup(world, path)) {
        result = ecs_mut_var_set_t(world, path, ecs_bool_t, {visible});
    }

    return result;
}

/** Print every panel and whether it is shown.
 *
 * @param world The world
 * @return The listing
 */
static char* box_stats_list(
    ecs_world_t *world)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;
    for (i = 0; i < BOX_STATS_CMD_PANELS; i ++) {
        const box_stats_cmd_panel_t *panel = &box_stats_panels[i];
        ecs_strbuf_append(&buf, "%-10s %-4s %s\n", panel->name,
            box_stats_visible(world, panel) ? "on" : "off", panel->brief);
    }

    return ecs_strbuf_get(&buf);
}

/** Hide every panel.
 *
 * @param world The world
 * @return The output
 */
static char* box_stats_all_off(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < BOX_STATS_CMD_PANELS; i ++) {
        box_stats_show(world, &box_stats_panels[i], false);
    }

    return ecs_os_strdup("stats panels off\n");
}

/** Switch one panel or print what it shows.
 *
 * @param world The world
 * @param panel The panel
 * @param state "on", "off" or NULL to show it and print its numbers
 * @return The output
 */
static char* box_stats_panel(
    ecs_world_t *world,
    const box_stats_cmd_panel_t *panel,
    const char *state)
{
    char *result = NULL;
    bool on = state && !ecs_os_strcmp(state, "on");
    bool off = state && !ecs_os_strcmp(state, "off");
    if (state && state[0] && !on && !off) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX
            "expected 'on' or 'off', got '%s'", state);
        goto done;
    }

    bool was = box_stats_visible(world, panel);
    if (box_stats_show(world, panel, !off)) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX
            "no statistics panel at '" BOX_STATS_SCOPE ".%s'", panel->name);
        goto done;
    }

    if (on || off || !was) {
        result = flecs_asprintf("%s panel %s\n", panel->name, off ? "off" : "on");
        goto done;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    panel->print(world, &buf);
    result = ecs_strbuf_get(&buf);
done:
    return result;
}

/** The stats command: list the panels, switch one or print what it shows.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_stats_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *name = *(char**)argv[1].ptr;
    const char *state = *(char**)argv[2].ptr;
    char *out = NULL;
    if (!name || !name[0]) {
        out = box_stats_list(world);
        goto done;
    }

    if (!ecs_os_strcmp(name, "off") && (!state || !state[0])) {
        out = box_stats_all_off(world);
        goto done;
    }

    int32_t i;
    for (i = 0; i < BOX_STATS_CMD_PANELS; i ++) {
        if (!ecs_os_strcmp(name, box_stats_panels[i].name)) {
            out = box_stats_panel(world, &box_stats_panels[i], state);
            goto done;
        }
    }

    out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "no statistics panel '%s'", name);
done:
    *(char**)result->ptr = out;
}

void box_stats_cmd_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxMonitor);

    box_command_param_t params[] = {
        { .name = "panel", .type = ecs_id(ecs_string_t), .optional = true },
        { .name = "state", .type = ecs_id(ecs_string_t), .optional = true }
    };

    box_command_register(world, "stats",
        "List the statistics panels, switch one on or off, or print what it shows",
        params, 2, box_stats_command);
}
