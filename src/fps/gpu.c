/**
 * @file fps/gpu.c
 * @brief The stats of the GPU stats panel: while GpuPanel is open, every GpuPanel.interval,
 * GpuStats gets what the renderer and the GPU did per frame since the last update.
 *
 * The renderer counts on its thread and publishes cumulative counters; the differences since
 * the last update are divided by the packets it rendered meanwhile. The draw extract and the
 * mesh graphs count on the main thread and are divided by the frames of the main thread.
 */

#include "gpu.h"
#include "../render/render.h"

/** State of the GPU stats of a world, owned by box.fps */
typedef struct box_fps_gpu_t {
    ecs_query_t *views;         /**< Entities with a CullStatus */
    box_render_gpu_stats_t base; /**< Counters of the renderer at the last update */
    box_mesh_graph_stats_t mesh_base; /**< Counters of the mesh graphs at the last update */
    double start;               /**< Seconds of the wall clock at the last update */
    int32_t frames;             /**< Frames of the main thread since the last update */
    double processed;           /**< Entities the draw extract processed in those frames */
    double instance_bytes;      /**< Bytes the draw extract uploaded in those frames */
    bool active;                /**< The panel is open and the detailed counters are on */
    bool published;             /**< GpuStats was written since the panel opened */
} box_fps_gpu_t;

/** Bytes per kilobyte */
#define BOX_FPS_KB (1024.0)

/** Read the wall clock.
 *
 * @return Seconds
 */
static double box_fps_gpu_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Get the change of a counter per item.
 *
 * @param now The counter now
 * @param base The counter at the last update
 * @param count The items, packets or frames, since the last update
 * @return The change per item, 0 without items
 */
static double box_fps_gpu_rate(
    int64_t now,
    int64_t base,
    int32_t count)
{
    return count > 0 ? (double)(now - base) / (double)count : 0;
}

/** Sum the CullStatus of the views into the scene stats.
 *
 * @param world The world
 * @param g The state
 * @param out Receives the sums
 */
static void box_fps_gpu_cull(
    ecs_world_t *world,
    box_fps_gpu_t *g,
    GpuSceneStats *out)
{
    ecs_iter_t it = ecs_query_iter(world, g->views);
    int32_t i;
    while (ecs_query_next(&it)) {
        const CullStatus *cs = ecs_field(&it, CullStatus, 0);
        for (i = 0; i < it.count; i ++) {
            out->views ++;
            out->tested += cs[i].instances;
            out->visible += cs[i].visible;
            out->frustum_culled += cs[i].frustum_culled;
            out->occlusion_culled += cs[i].occlusion_culled;
            out->range_culled += cs[i].range_culled;
        }
    }
}

/** Fill the scene stats: the batches of the draw extract, the culling of the views and the
 * triangles the renderer drew.
 *
 * @param world The world
 * @param g The state
 * @param now The counters of the renderer
 * @param draws The counters of the draw extract
 * @param out Receives the stats
 */
static void box_fps_gpu_scene(
    ecs_world_t *world,
    box_fps_gpu_t *g,
    const box_render_gpu_stats_t *now,
    const box_render_draws_stats_t *draws,
    GpuSceneStats *out)
{
    const box_render_gpu_stats_t *base = &g->base;
    int32_t reads = now->triangle_reads - base->triangle_reads;
    int32_t packets = now->packets - base->packets;
    out->batches = draws->batches + draws->impostor_batches;
    out->instances = draws->instances + draws->dynamic;
    out->dynamic = draws->dynamic;
    out->impostors = draws->impostors;
    out->materials = draws->materials;
    box_fps_gpu_cull(world, g, out);
    out->triangles = box_fps_gpu_rate(now->view_triangles, base->view_triangles, reads);
    out->shadow_triangles = box_fps_gpu_rate(
        now->shadow_triangles, base->shadow_triangles, reads);
    out->other_triangles = box_fps_gpu_rate(now->api_triangles, base->api_triangles, packets);
}

/** Fill the command stats from the webgpu commands the renderer recorded.
 *
 * @param now The counters of the renderer
 * @param base The counters at the last update
 * @param out Receives the stats
 */
static void box_fps_gpu_commands(
    const box_render_gpu_stats_t *now,
    const box_render_gpu_stats_t *base,
    GpuCommandStats *out)
{
    int32_t n = now->packets - base->packets;
    out->render_passes = box_fps_gpu_rate(now->api_render_passes, base->api_render_passes, n);
    out->compute_passes = box_fps_gpu_rate(
        now->api_compute_passes, base->api_compute_passes, n);
    out->draws = box_fps_gpu_rate(now->api_draws, base->api_draws, n);
    out->dispatches = box_fps_gpu_rate(now->api_dispatches, base->api_dispatches, n);
    out->workgroups = box_fps_gpu_rate(now->api_workgroups, base->api_workgroups, n);
    out->pipelines = box_fps_gpu_rate(now->api_pipelines, base->api_pipelines, n);
    out->bind_groups = box_fps_gpu_rate(now->api_bind_groups, base->api_bind_groups, n);
    out->buffer_binds = box_fps_gpu_rate(now->api_buffer_binds, base->api_buffer_binds, n);
    out->copies = box_fps_gpu_rate(now->api_copies, base->api_copies, n);
    out->copy_bytes = box_fps_gpu_rate(now->api_copy_bytes, base->api_copy_bytes, n);
    out->writes = box_fps_gpu_rate(now->api_writes, base->api_writes, n);
    out->write_bytes = box_fps_gpu_rate(now->api_write_bytes, base->api_write_bytes, n);
    out->submits = box_fps_gpu_rate(now->api_submits, base->api_submits, n);
    out->command_buffers = box_fps_gpu_rate(
        now->api_command_buffers, base->api_command_buffers, n);
    out->total = out->render_passes + out->compute_passes + out->draws + out->dispatches +
        out->pipelines + out->bind_groups + out->buffer_binds + out->copies + out->writes +
        out->submits;
    out->pipelines_cached = now->pipelines_cached;
    out->pipelines_created = now->pipelines;
    out->pipelines_building = now->pipelines_building;
}

/** Fill the packet, shadow and texture graph stats from the counters of the renderer.
 *
 * @param now The counters of the renderer
 * @param base The counters at the last update
 * @param out Receives the stats
 */
static void box_fps_gpu_work(
    const box_render_gpu_stats_t *now,
    const box_render_gpu_stats_t *base,
    GpuStats *out)
{
    int32_t n = now->packets - base->packets;
    GpuPacketStats *p = &out->packet;
    GpuShadowStats *s = &out->shadows;
    GpuTextureGraphStats *t = &out->texture_graphs;
    p->resources = box_fps_gpu_rate(now->packet_resources, base->packet_resources, n);
    p->uploads = box_fps_gpu_rate(now->packet_uploads, base->packet_uploads, n);
    p->scatters = box_fps_gpu_rate(now->packet_scatters, base->packet_scatters, n);
    p->copies = box_fps_gpu_rate(now->packet_copies, base->packet_copies, n);
    p->bytes = box_fps_gpu_rate(now->packet_bytes, base->packet_bytes, n);
    s->draws = box_fps_gpu_rate(now->shadow_draws, base->shadow_draws, n);
    s->tiles = box_fps_gpu_rate(now->shadow_tiles, base->shadow_tiles, n);
    s->tiles_kept = box_fps_gpu_rate(now->shadow_reused, base->shadow_reused, n);
    s->cascades = box_fps_gpu_rate(now->shadow_cascades, base->shadow_cascades, n);
    s->cascades_kept = box_fps_gpu_rate(
        now->shadow_cascades_kept, base->shadow_cascades_kept, n);
    t->graphs = now->texture_graphs;
    t->programs = now->texture_graph_programs;
    t->evaluations = box_fps_gpu_rate(
        now->texture_graph_evaluations, base->texture_graph_evaluations, n);
    t->dispatches = box_fps_gpu_rate(
        now->texture_graph_dispatches, base->texture_graph_dispatches, n);
    t->texels = box_fps_gpu_rate(now->texture_graph_texels, base->texture_graph_texels, n);
    t->hits = box_fps_gpu_rate(now->texture_graph_hits, base->texture_graph_hits, n);
    t->misses = box_fps_gpu_rate(now->texture_graph_misses, base->texture_graph_misses, n);
    t->evictions = box_fps_gpu_rate(
        now->texture_graph_evictions, base->texture_graph_evictions, n);
    t->cached = now->texture_graph_cached;
    t->cached_bytes = now->texture_graph_cached_bytes;
    t->pool_bytes = now->texture_graph_pool_bytes + now->texture_graph_key_bytes;
}

/** Fill the memory and GPU time stats from the counters of the renderer.
 *
 * @param now The counters of the renderer
 * @param base The counters at the last update
 * @param draws The counters of the draw extract
 * @param out Receives the stats
 */
static void box_fps_gpu_resources(
    const box_render_gpu_stats_t *now,
    const box_render_gpu_stats_t *base,
    const box_render_draws_stats_t *draws,
    GpuStats *out)
{
    int32_t i, n = now->packets - base->packets, timed = 0;
    int64_t atmosphere = 0;
    GpuMemoryStats *m = &out->memory;
    m->buffers = now->buffers;
    m->meshes = now->meshes;
    m->targets = now->targets;
    m->shaders = now->shaders;
    m->instance_bytes = draws->page_bytes;
    m->blend_bytes = now->blend_bytes;
    m->graph_bytes = now->texture_graph_cached_bytes + now->texture_graph_pool_bytes +
        now->texture_graph_key_bytes;
    for (i = 0; i < BOX_RENDER_ATMOS_PASSES; i ++) {
        atmosphere += now->atmosphere_ns[i] - base->atmosphere_ns[i];
        timed += now->atmosphere_passes[i] - base->atmosphere_passes[i];
    }

    timed += now->cull_passes - base->cull_passes;
    timed += now->texture_graph_timed - base->texture_graph_timed;
    timed += now->grass_timed - base->grass_timed;
    out->time.measured = timed > 0;
    out->time.cull_ms = box_fps_gpu_rate(now->cull_ns, base->cull_ns, n) * 1e-6;
    out->time.texture_graph_ms = box_fps_gpu_rate(
        now->texture_graph_ns, base->texture_graph_ns, n) * 1e-6;
    out->time.atmosphere_ms = box_fps_gpu_rate(atmosphere, 0, n) * 1e-6;
    out->time.grass_ms = box_fps_gpu_rate(now->grass_ns, base->grass_ns, n) * 1e-6;
}

/** Fill the grass stats from the blades read back, per packet whose counts arrived.
 *
 * @param now The counters of the renderer
 * @param base The counters at the last update
 * @param out Receives the stats
 */
static void box_fps_gpu_grass(
    const box_render_gpu_stats_t *now,
    const box_render_gpu_stats_t *base,
    GpuGrassStats *out)
{
    int32_t n = now->packets - base->packets, reads = now->grass_reads - base->grass_reads;
    GpuGrassLevel *levels[BOX_GRASS_LODS] = { &out->lod0, &out->lod1, &out->lod2 };
    int32_t i;
    out->passes = box_fps_gpu_rate(now->grass_passes, base->grass_passes, n);
    out->culled_patches = box_fps_gpu_rate(
        now->grass_culled_patches, base->grass_culled_patches, reads);
    out->culled_blades = box_fps_gpu_rate(
        now->grass_culled_blades, base->grass_culled_blades, reads);
    for (i = 0; i < BOX_GRASS_LODS; i ++) {
        levels[i]->generated = box_fps_gpu_rate(
            now->grass_generated[i], base->grass_generated[i], reads);
        levels[i]->drawn = box_fps_gpu_rate(now->grass_drawn[i], base->grass_drawn[i], reads);
        out->generated += levels[i]->generated;
        out->drawn += levels[i]->drawn;
    }
}

/** Fill the mesh graph stats from their counters.
 *
 * @param g The state
 * @param now The counters of the mesh graphs
 * @param out Receives the stats
 */
static void box_fps_gpu_mesh(
    const box_fps_gpu_t *g,
    const box_mesh_graph_stats_t *now,
    GpuMeshGraphStats *out)
{
    const box_mesh_graph_stats_t *base = &g->mesh_base;
    out->jobs = box_fps_gpu_rate(now->jobs, base->jobs, g->frames);
    out->evaluated = box_fps_gpu_rate(now->evaluated, base->evaluated, g->frames);
    out->hits = box_fps_gpu_rate(now->hits, base->hits, g->frames);
    out->evicted = box_fps_gpu_rate(now->evicted, base->evicted, g->frames);
    out->values = now->values;
    out->bytes = now->bytes;
}

/** Write GpuStats from the counters now and at the last update.
 *
 * @param world The world
 * @param g The state
 * @param gpu Whether the world renders with the GPU backend
 * @param now The counters of the renderer
 * @param draws The counters of the draw extract
 * @param mesh The counters of the mesh graphs
 */
static void box_fps_gpu_publish(
    ecs_world_t *world,
    box_fps_gpu_t *g,
    bool gpu,
    const box_render_gpu_stats_t *now,
    const box_render_draws_stats_t *draws,
    const box_mesh_graph_stats_t *mesh)
{
    GpuStats out;
    ecs_os_memset_t(&out, 0, GpuStats);
    out.gpu = gpu;
    out.frames = g->frames;
    out.packets = now->packets - g->base.packets;
    box_fps_gpu_scene(world, g, now, draws, &out.scene);
    box_fps_gpu_commands(now, &g->base, &out.commands);
    box_fps_gpu_work(now, &g->base, &out);
    box_fps_gpu_resources(now, &g->base, draws, &out);
    box_fps_gpu_grass(now, &g->base, &out.grass);
    box_fps_gpu_mesh(g, mesh, &out.mesh_graphs);
    out.packet.processed = g->frames ? g->processed / g->frames : 0;
    out.packet.instance_bytes = g->frames ? g->instance_bytes / g->frames : 0;
    ecs_singleton_set_ptr(world, GpuStats, &out);
}

/** Start the averages of the next update from the counters now.
 *
 * @param g The state
 * @param now The counters of the renderer
 * @param mesh The counters of the mesh graphs
 * @param time Seconds of the wall clock
 */
static void box_fps_gpu_restart(
    box_fps_gpu_t *g,
    const box_render_gpu_stats_t *now,
    const box_mesh_graph_stats_t *mesh,
    double time)
{
    g->base = *now;
    g->mesh_base = *mesh;
    g->start = time;
    g->frames = 0;
    g->processed = 0;
    g->instance_bytes = 0;
}

/** Sample the counters every frame while GpuPanel is open and write GpuStats every interval,
 * and at once when the panel opens. Turns the detailed counters of the renderer on while the
 * panel is open and off when it closes.
 *
 * @param it The iterator of the system
 */
static void BoxFpsGpuSample(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_fps_gpu_t *g = it->ctx;
    const GpuPanel *panel = ecs_singleton_get(world, GpuPanel);
    bool open = panel && panel->open, gpu;
    box_render_gpu_stats_t now;
    box_render_draws_stats_t draws;
    box_mesh_graph_stats_t mesh;
    double time = box_fps_gpu_now();
    if (open != g->active) {
        box_render_gpu_stats_detail(world, open);
        g->active = open;
        g->published = false;
    }

    if (!open) {
        goto done;
    }

    gpu = box_render_gpu_stats(world, &now);
    box_render_draws_stats(world, &draws);
    box_mesh_graph_stats(world, &mesh);
    if (!g->published) {
        box_fps_gpu_restart(g, &now, &mesh, time);
    }

    g->frames ++;
    g->processed += draws.processed;
    g->instance_bytes += (double)(draws.upload_bytes + draws.clip_bytes + draws.material_bytes);
    if (g->published && time - g->start < panel->interval) {
        goto done;
    }

    box_fps_gpu_publish(world, g, gpu, &now, &draws, &mesh);
    box_fps_gpu_restart(g, &now, &mesh, time);
    g->published = true;
done:
    return;
}

/** Construct GpuPanel: closed, updating every BOX_GPU_INTERVAL.
 *
 * @param ptr The panels
 * @param count Number of panels
 * @param ti Type info, unused
 */
static void box_fps_gpu_panel_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    GpuPanel *panels = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        panels[i] = (GpuPanel){ .interval = BOX_GPU_INTERVAL };
    }
}

/** Format a count compactly: box.fps.count(value), 3 significant digits with k, M or G above a
 * thousand, one decimal below 100 when it has a fraction.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The value, an f64
 * @param result Receives the text
 */
static void box_fps_count_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double v = *(const double*)argv[0].ptr, a = v < 0 ? -v : v;
    char *text;
    (void)ctx;
    (void)argc;
    if (a >= 1e9) {
        text = flecs_asprintf("%.2fG", v / 1e9);
    } else if (a >= 1e6) {
        text = flecs_asprintf("%.2fM", v / 1e6);
    } else if (a >= 1e3) {
        text = flecs_asprintf("%.1fk", v / 1e3);
    } else if (a < 100 && v != (double)(int64_t)v) {
        text = flecs_asprintf("%.1f", v);
    } else {
        text = flecs_asprintf("%.0f", v);
    }

    *(char**)result->ptr = text;
}

/** Format a number of bytes compactly: box.fps.bytes(value), in B, KB, MB or GB.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The bytes, an f64
 * @param result Receives the text
 */
static void box_fps_bytes_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double v = *(const double*)argv[0].ptr;
    char *text;
    (void)ctx;
    (void)argc;
    if (v >= BOX_FPS_KB * BOX_FPS_KB * BOX_FPS_KB) {
        text = flecs_asprintf("%.2f GB", v / (BOX_FPS_KB * BOX_FPS_KB * BOX_FPS_KB));
    } else if (v >= BOX_FPS_KB * BOX_FPS_KB) {
        text = flecs_asprintf("%.1f MB", v / (BOX_FPS_KB * BOX_FPS_KB));
    } else if (v >= BOX_FPS_KB) {
        text = flecs_asprintf("%.1f KB", v / BOX_FPS_KB);
    } else {
        text = flecs_asprintf("%.0f B", v);
    }

    *(char**)result->ptr = text;
}

/** Free the state when the world is deleted.
 *
 * @param world The world
 * @param ctx The state
 */
static void box_fps_gpu_free(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    ecs_os_free(ctx);
}

void box_fps_gpu_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    box_fps_gpu_t *g = ecs_os_calloc_t(box_fps_gpu_t);
    ecs_atfini(world, box_fps_gpu_free, g);
    ecs_set_hooks(world, GpuPanel, { .ctor = box_fps_gpu_panel_ctor });
    ecs_set_hooks(world, GpuStats, { .ctor = flecs_default_ctor });
    ecs_singleton_add(world, GpuPanel);
    ecs_singleton_set(world, GpuStats, {0});

    ecs_function(world, {
        .name = "count",
        .parent = module,
        .return_type = ecs_id(ecs_string_t),
        .params = {{ .name = "value", .type = ecs_id(ecs_f64_t) }},
        .callback = box_fps_count_function
    });

    ecs_function(world, {
        .name = "bytes",
        .parent = module,
        .return_type = ecs_id(ecs_string_t),
        .params = {{ .name = "value", .type = ecs_id(ecs_f64_t) }},
        .callback = box_fps_bytes_function
    });

    g->views = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "CullViews" }),
        .terms = {{ .id = ecs_id(CullStatus), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "GpuSample" }),
        .phase = EcsPreFrame,
        .run = BoxFpsGpuSample,
        .ctx = g,
        .immediate = true
    });
}
