/**
 * @file monitor/counters.c
 * @brief Publishes the per frame counts of the renderer on the statistics components.
 *
 * Adding a statistics singleton asks the renderer to count what it needs and
 * removing the last one stops the counting. A system copies each frame the
 * renderer finished into the singletons and marks them modified.
 */

#include "monitor_internal.h"
#include "../surface/surface_internal.h"

/** Path of the component that holds the renderer of a surface */
#define BOX_MONITOR_SURFACE_PATH "box.surface.SurfaceImpl"

/** What the statistics singletons ask the renderer to count */
typedef struct RendererCounting {
    uint32_t flags; /**< BOX_COUNT_ flags of the singletons that exist */
} RendererCounting;

/** Private singleton that exists while a statistics singleton exists */
static ECS_COMPONENT_DECLARE(RendererCounting);

/** A statistics component and what it asks the renderer to count */
typedef struct box_counted_t {
    ecs_entity_t component;    /**< The component */
    uint32_t flags;            /**< BOX_COUNT_ flags it needs */
    box_counted_write_t write; /**< Fills it from the counts of a frame */
    int32_t frame;             /**< Frame it was last filled from, -1 before that */
} box_counted_t;

/** Statistics components the counts are published on */
typedef struct box_counted_list_t {
    box_counted_t entries[BOX_MONITOR_COUNTED_MAX]; /**< The components */
    int32_t count;                                  /**< Entries in use */
} box_counted_list_t;

box_renderer_t* box_monitor_renderer(
    ecs_world_t *world)
{
    box_renderer_t *result = NULL;
    ecs_entity_t comp = ecs_lookup(world, BOX_MONITOR_SURFACE_PATH);
    if (!comp) {
        goto done;
    }

    ecs_iter_t it = ecs_each_id(world, comp);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const SurfaceImpl *impl = ecs_get_id(world, it.entities[i], comp);
            if (impl && impl->renderer && !result) {
                result = impl->renderer;
            }
        }
    }

done:
    return result;
}

void box_monitor_name(
    char **slot,
    const char *name)
{
    if (*slot && name && !ecs_os_strcmp(*slot, name)) {
        goto done;
    }

    ecs_os_free(*slot);
    *slot = name ? ecs_os_strdup(name) : NULL;
done:
    return;
}

/** Fill the RendererStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_renderer(
    void *ptr,
    const box_counters_t *c)
{
    RendererStats *s = ptr;
    s->frame = (int32_t)c->frame;
    s->indirect_resolved = c->indirect_resolved;
    s->draw_calls = (int32_t)c->draws;
    s->indirect_draw_calls = (int32_t)c->indirect_draws;
    s->render_passes = (int32_t)c->render_passes;
    s->compute_passes = (int32_t)c->compute_passes;
    s->pipeline_switches = (int32_t)c->pipeline_switches;
    s->bind_group_changes = (int32_t)c->bind_changes;
    s->buffer_uploads = (int32_t)c->buffer_uploads;
    s->bytes_uploaded = (int64_t)(c->buffer_upload_bytes + c->texture_upload_bytes);
    s->texture_uploads = (int32_t)c->texture_uploads;
    s->compute_dispatches = (int32_t)c->dispatches;
    s->instances = (int64_t)c->instances;
    s->triangles = (int64_t)c->triangles;
    s->buffer_memory = (int64_t)c->buffer_memory;
    s->texture_memory = (int64_t)c->texture_memory;
    s->gpu_memory = (int64_t)(c->buffer_memory + c->texture_memory + c->query_memory);
    s->buffers = (int32_t)c->buffers;
    s->textures = (int32_t)c->textures;
}

/** Order the entries of a table by a key, largest first, keeping ties in table order.
 *
 * @param keys Key of every entry
 * @param count Entries
 * @param order Receives the entry indices, count of them
 */
static void box_counted_order(
    const uint64_t *keys,
    int32_t count,
    int32_t *order)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t j = i;
        while (j > 0 && keys[order[j - 1]] < keys[i]) {
            order[j] = order[j - 1];
            j --;
        }

        order[j] = i;
    }
}

/** Fill the PassStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_passes(
    void *ptr,
    const box_counters_t *c)
{
    PassStats *s = ptr;
    int32_t count = (int32_t)c->pass_count;
    s->frame = (int32_t)c->frame;
    s->total = count;
    s->count = count < BOX_MONITOR_STAT_ROWS ? count : BOX_MONITOR_STAT_ROWS;
    int32_t i;
    for (i = 0; i < BOX_MONITOR_STAT_ROWS; i ++) {
        PassStat *row = &s->passes[i];
        const box_count_pass_t *p = i < s->count ? &c->passes[i] : NULL;
        box_monitor_name(&row->name, p ? p->name : NULL);
        row->compute = p ? p->compute : false;
        row->recorded = p ? (int32_t)p->recorded : 0;
        row->draw_calls = p ? (int32_t)p->draws : 0;
        row->dispatches = p ? (int32_t)p->dispatches : 0;
        row->instances = p ? (int64_t)p->instances : 0;
        row->triangles = p ? (int64_t)p->triangles : 0;
        row->pipeline_switches = p ? (int32_t)p->pipeline_switches : 0;
        row->bind_group_changes = p ? (int32_t)p->bind_changes : 0;
    }
}

/** Fill the PipelineStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_pipelines(
    void *ptr,
    const box_counters_t *c)
{
    PipelineStats *s = ptr;
    int32_t count = (int32_t)c->pipeline_count;
    uint64_t keys[BOX_COUNT_PIPELINE_MAX];
    int32_t order[BOX_COUNT_PIPELINE_MAX];
    int32_t i;
    for (i = 0; i < count; i ++) {
        keys[i] = ((uint64_t)c->pipelines[i].switches << 32) | c->pipelines[i].sets;
    }

    box_counted_order(keys, count, order);
    s->frame = (int32_t)c->frame;
    s->total = count;
    s->count = count < BOX_MONITOR_STAT_ROWS ? count : BOX_MONITOR_STAT_ROWS;
    s->sets = (int32_t)c->pipeline_sets;
    s->switches = (int32_t)c->pipeline_switches;
    for (i = 0; i < BOX_MONITOR_STAT_ROWS; i ++) {
        PipelineStat *row = &s->pipelines[i];
        const box_count_pipeline_t *p = i < s->count ? &c->pipelines[order[i]] : NULL;
        box_monitor_name(&row->name, p ? p->name : NULL);
        row->compute = p ? p->compute : false;
        row->sets = p ? (int32_t)p->sets : 0;
        row->switches = p ? (int32_t)p->switches : 0;
        row->draw_calls = p ? (int32_t)p->draws : 0;
        row->dispatches = p ? (int32_t)p->dispatches : 0;
        row->instances = p ? (int64_t)p->instances : 0;
        row->triangles = p ? (int64_t)p->triangles : 0;
    }
}

/** Fill the BindGroupStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_bind_groups(
    void *ptr,
    const box_counters_t *c)
{
    BindGroupStats *s = ptr;
    int32_t count = (int32_t)c->bind_count;
    uint64_t keys[BOX_COUNT_BIND_MAX];
    int32_t order[BOX_COUNT_BIND_MAX];
    int32_t i;
    for (i = 0; i < count; i ++) {
        keys[i] = ((uint64_t)c->binds[i].changes << 32) | c->binds[i].sets;
    }

    box_counted_order(keys, count, order);
    s->frame = (int32_t)c->frame;
    s->total = count;
    s->count = count < BOX_MONITOR_STAT_ROWS ? count : BOX_MONITOR_STAT_ROWS;
    s->sets = (int32_t)c->bind_sets;
    s->changes = (int32_t)c->bind_changes;
    for (i = 0; i < BOX_MONITOR_STAT_ROWS; i ++) {
        BindGroupStat *row = &s->slots[i];
        const box_count_bind_t *b = i < s->count ? &c->binds[order[i]] : NULL;
        box_monitor_name(&row->pass, b ? c->passes[b->pass].name : NULL);
        row->slot = b ? (int32_t)b->slot : 0;
        row->sets = b ? (int32_t)b->sets : 0;
        row->changes = b ? (int32_t)b->changes : 0;
    }
}

/** Fill the BufferStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_buffers(
    void *ptr,
    const box_counters_t *c)
{
    BufferStats *s = ptr;
    s->frame = (int32_t)c->frame;
    s->buffers = (int32_t)c->buffers;
    s->bytes = (int64_t)c->buffer_memory;
    s->uploads = (int32_t)c->buffer_uploads;
    s->upload_bytes = (int64_t)c->buffer_upload_bytes;
    int32_t i;
    for (i = 0; i < BOX_MONITOR_USAGES; i ++) {
        BufferStat *row = &s->usages[i];
        const box_count_buffer_t *u = &c->usages[i];
        box_monitor_name(&row->usage, box_count_usage_name((uint32_t)i));
        row->buffers = (int32_t)u->buffers;
        row->bytes = (int64_t)u->bytes;
        row->uploads = (int32_t)u->uploads;
        row->upload_bytes = (int64_t)u->upload_bytes;
    }
}

/** Fill the ComputeStats component from the counts of a frame.
 *
 * @param ptr The component
 * @param c The counts
 */
static void box_counted_compute(
    void *ptr,
    const box_counters_t *c)
{
    ComputeStats *s = ptr;
    s->frame = (int32_t)c->frame;
    s->total = 0;
    s->count = 0;
    s->dispatches = (int32_t)c->dispatches;
    s->workgroups = (int64_t)c->workgroups;
    uint32_t i;
    for (i = 0; i < c->pass_count; i ++) {
        const box_count_pass_t *p = &c->passes[i];
        if (!p->compute) {
            continue;
        }

        s->total ++;
        if (s->count == BOX_MONITOR_STAT_ROWS) {
            continue;
        }

        ComputeStat *row = &s->passes[s->count ++];
        box_monitor_name(&row->name, p->name);
        row->recorded = (int32_t)p->recorded;
        row->dispatches = (int32_t)p->dispatches;
        row->workgroups = (int64_t)p->workgroups;
        row->pipeline_switches = (int32_t)p->pipeline_switches;
        row->bind_group_changes = (int32_t)p->bind_changes;
    }

    int32_t row;
    for (row = s->count; row < BOX_MONITOR_STAT_ROWS; row ++) {
        box_monitor_name(&s->passes[row].name, NULL);
        s->passes[row].recorded = 0;
        s->passes[row].dispatches = 0;
        s->passes[row].workgroups = 0;
        s->passes[row].pipeline_switches = 0;
        s->passes[row].bind_group_changes = 0;
    }
}

/** The statistics components of the world.
 *
 * @param world The world
 * @return The list, owned by the module
 */
static box_counted_list_t* box_counted_list(
    ecs_world_t *world)
{
    const ecs_world_t *real = ecs_get_world(world);
    ecs_entity_t system = ecs_lookup(real, "box.monitor.BoxPublishRendererCounters");

    return ecs_system_get(real, system)->ctx;
}

/** Flags the statistics singletons of the world ask for.
 *
 * @param world The world
 * @param list The statistics components
 * @param removed Component that is being removed, 0 for none
 * @return The BOX_COUNT_ flags
 */
static uint32_t box_counted_flags(
    ecs_world_t *world,
    const box_counted_list_t *list,
    ecs_entity_t removed)
{
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < list->count; i ++) {
        const box_counted_t *entry = &list->entries[i];
        if (entry->component != removed &&
            ecs_has_id(world, entry->component, entry->component))
        {
            result |= entry->flags;
        }
    }

    return result;
}

/** Copy each frame the renderer finished into the statistics singletons.
 *
 * @param it Iterator over the RendererCounting singleton
 */
static void BoxPublishRendererCounters(
    ecs_iter_t *it)
{
    const RendererCounting *counting = ecs_field(it, RendererCounting, 0);
    box_counted_list_t *list = it->ctx;
    box_renderer_t *r = box_monitor_renderer(it->world);
    if (!r) {
        goto done;
    }

    box_renderer_set_counters(r, counting->flags);
    box_counters_t *c = ecs_os_malloc_t(box_counters_t);
    if (!box_renderer_counters(r, c)) {
        ecs_os_free(c);
        goto done;
    }

    int32_t i;
    for (i = 0; i < list->count; i ++) {
        box_counted_t *entry = &list->entries[i];
        void *ptr = ecs_get_mut_id(it->world, entry->component, entry->component);
        bool counted = (c->flags & entry->flags) == entry->flags;
        if (!ptr || !counted || entry->frame == (int32_t)c->frame) {
            continue;
        }

        entry->write(ptr, c);
        entry->frame = (int32_t)c->frame;
        ecs_modified_id(it->world, entry->component, entry->component);
    }

    ecs_os_free(c);
done:
    return;
}

/** Keep the RendererCounting singleton and the renderer in line with the singletons.
 *
 * A world that is being torn down removes the singletons by deleting their
 * entities, which move to the empty root table; its surface may already have
 * destroyed the renderer, so nothing is changed then.
 *
 * @param it Iterator over the added or removed statistics singleton
 */
static void BoxRendererCountersChanged(
    ecs_iter_t *it)
{
    bool removed = it->event == EcsOnRemove;
    if (removed && it->other_table && !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    box_counted_list_t *list = it->ctx;
    uint32_t flags = box_counted_flags(it->world, list, removed ? it->event_id : 0);
    int32_t i;
    for (i = 0; i < list->count; i ++) {
        if (list->entries[i].component != it->event_id) {
            continue;
        }

        list->entries[i].frame = -1;
        if (!removed) {
            flags |= list->entries[i].flags;
        }
    }

    if (flags) {
        ecs_singleton_set(it->world, RendererCounting, { flags });
    } else {
        ecs_singleton_remove(it->world, RendererCounting);
    }

    box_renderer_t *r = box_monitor_renderer(it->world);
    if (r) {
        box_renderer_set_counters(r, flags);
    }

done:
    return;
}

/** Free the list of statistics components.
 *
 * @param ptr The list
 */
static void box_counted_list_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_monitor_counted(
    ecs_world_t *world,
    ecs_entity_t component,
    uint32_t flags,
    box_counted_write_t write)
{
    box_counted_list_t *list = box_counted_list(world);
    ecs_assert(list->count < BOX_MONITOR_COUNTED_MAX, ECS_INVALID_OPERATION, NULL);
    box_counted_t *entry = &list->entries[list->count ++];
    entry->component = component;
    entry->flags = flags;
    entry->write = write;
    entry->frame = -1;

    char name[64];
    ecs_os_snprintf(name, sizeof(name), "BoxCounted%s", ecs_get_name(world, component));
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = name }),
        .query.terms = {{ .id = component, .src.id = component }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxRendererCountersChanged,
        .ctx = list
    });
}

void box_counters_monitor_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, RendererCounting);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxPublishRendererCounters" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(RendererCounting), .src.id = ecs_id(RendererCounting) }},
        .callback = BoxPublishRendererCounters,
        .ctx = ecs_os_calloc_t(box_counted_list_t),
        .ctx_free = box_counted_list_free,
        .immediate = true
    });

    box_monitor_counted(world, ecs_id(RendererStats), BOX_COUNT_FRAME, box_counted_renderer);
    box_monitor_counted(world, ecs_id(PassStats), BOX_COUNT_PASSES, box_counted_passes);
    box_monitor_counted(world, ecs_id(PipelineStats), BOX_COUNT_PIPELINES,
        box_counted_pipelines);
    box_monitor_counted(world, ecs_id(BindGroupStats), BOX_COUNT_BIND_GROUPS,
        box_counted_bind_groups);
    box_monitor_counted(world, ecs_id(BufferStats), BOX_COUNT_BUFFERS, box_counted_buffers);
    box_monitor_counted(world, ecs_id(ComputeStats), BOX_COUNT_COMPUTE, box_counted_compute);
}
