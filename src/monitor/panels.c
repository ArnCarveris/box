/**
 * @file monitor/panels.c
 * @brief Loads the statistics panels and ties each one to the singleton it reads.
 *
 * The templates load into box.monitor.panels and the layout into box.monitor.stats,
 * whose exported mut variable per panel shows it. Showing a panel adds the
 * singleton it reads, which starts its collection; hiding it removes it again.
 */

#include "monitor_internal.h"
#include "../shader/shader_internal.h"

/** Panels the layout script shows */
#define BOX_STATS_PANEL_COUNT (7)

/** A statistics panel: its variable in box.monitor.stats and the singleton it reads */
typedef struct box_stats_panel_t {
    const char *name;       /**< Name of the exported mut variable */
    ecs_entity_t component; /**< Singleton the panel reads */
    bool owned;             /**< The panel added the singleton, so hiding it removes it */
} box_stats_panel_t;

/** Load a script into a named child of box.monitor.
 *
 * @param world The world
 * @param path Path of the script relative to the engine search path
 * @param name Name of the script entity
 * @return true when the script was found and ran
 */
static bool box_stats_script(
    ecs_world_t *world,
    const char *path,
    const char *name)
{
    bool result = false;
    char resolved[1024];
    if (!box_locate(path, resolved, sizeof(resolved))) {
        ecs_warn("box.monitor: no statistics panel script at '%s'", path);
        goto done;
    }

    ecs_entity_t script = ecs_entity(world, {
        .name = name, .parent = ecs_lookup(world, "box.monitor") });
    result = ecs_script(world, { .entity = script, .filename = resolved }) != 0;
    if (!result) {
        ecs_err("box.monitor: failed to load '%s'", resolved);
    }

done:
    return result;
}

/** Add or remove the singleton of a panel when its variable changes.
 *
 * A singleton that existed before the panel showed belongs to someone else and
 * stays when the panel hides.
 *
 * @param it Iterator over the variable
 */
static void BoxStatsPanelVisible(
    ecs_iter_t *it)
{
    box_stats_panel_t *panel = it->ctx;
    ecs_entity_t var = it->sources[0];
    ecs_value_t value = ecs_mut_var_get(it->world, var);
    bool visible = value.ptr && value.type == ecs_id(ecs_bool_t) && *(bool*)value.ptr;
    bool present = ecs_has_id(it->world, panel->component, panel->component);
    if (visible && !present) {
        ecs_add_id(it->world, panel->component, panel->component);
        panel->owned = true;
    } else if (!visible && present && panel->owned) {
        ecs_remove_id(it->world, panel->component, panel->component);
        panel->owned = false;
    } else if (!visible) {
        panel->owned = false;
    }
}

/** Free the panel of a variable observer.
 *
 * @param ptr The panel
 */
static void box_stats_panel_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_stats_panels_init(
    ecs_world_t *world)
{
    box_stats_panel_t panels[BOX_STATS_PANEL_COUNT] = {
        { .name = "renderer", .component = ecs_id(RendererStats) },
        { .name = "gpu", .component = ecs_id(GpuTimings) },
        { .name = "pass", .component = ecs_id(PassStats) },
        { .name = "pipeline", .component = ecs_id(PipelineStats) },
        { .name = "bindgroup", .component = ecs_id(BindGroupStats) },
        { .name = "buffer", .component = ecs_id(BufferStats) },
        { .name = "compute", .component = ecs_id(ComputeStats) }
    };

    bool loaded = box_stats_script(world, BOX_MONITOR_STATS_TEMPLATES, "stats_templates");
    if (!loaded || !box_stats_script(world, BOX_MONITOR_STATS_PANEL, "stats_script")) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BOX_STATS_PANEL_COUNT; i ++) {
        char path[64];
        ecs_os_snprintf(path, sizeof(path), "box.monitor.stats.%s", panels[i].name);
        ecs_entity_t var = ecs_lookup(world, path);
        if (!var) {
            ecs_warn("box.monitor: statistics panel variable '%s' is missing", path);
            continue;
        }

        char name[64];
        ecs_os_snprintf(name, sizeof(name), "BoxShowStats%s",
            ecs_get_name(world, panels[i].component));
        ecs_observer(world, {
            .entity = ecs_entity(world, { .name = name }),
            .query.terms = {{ .id = ecs_id(EcsScriptMutVar), .src.id = var }},
            .events = { EcsOnSet },
            .callback = BoxStatsPanelVisible,
            .ctx = ecs_os_memdup_t(&panels[i], box_stats_panel_t),
            .ctx_free = box_stats_panel_free
        });
    }

done:
    return;
}
