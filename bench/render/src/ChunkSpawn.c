/**
 * @file ChunkSpawn.c
 * @brief Benchmarks for streaming a ChunkGrid of forest cells around a moving focus.
 *
 * A cell is ChunkSpawnCell of test/assets/world/chunk_spawn.flecs: ten trees of Sweeps,
 * IcoSpheres and leaf cards, boulders, logs and about 2000 grass cards that are IsA instances.
 * An iteration is a frame in which the focus moves at walking (5 m/s) or flying (40 m/s) speed.
 */

#include <box.h>
#include <bake_bench.h>
#include <string.h>
#include "render.h"
#include "mesh_cache.h"

/** Size of a cell in meters */
#define BENCH_CHUNK_CELL (32.0f)

/** Frame rate the focus moves at */
#define BENCH_CHUNK_FPS (60.0)

/** Most frames the initial load may take */
#define BENCH_CHUNK_LOAD_FRAMES (1000)

/** Groups of systems the frame time is split into */
typedef enum bench_chunk_group_t {
    BENCH_CHUNK_GRID,      /**< box.world, the ChunkGrid stream */
    BENCH_CHUNK_TRANSFORM, /**< box.transform */
    BENCH_CHUNK_SPATIAL,   /**< box.spatial */
    BENCH_CHUNK_GEOMETRY,  /**< box.geometry and box.mesh */
    BENCH_CHUNK_EXTRACT,   /**< box.render */
    BENCH_CHUNK_OTHER,     /**< Every other system */
    BENCH_CHUNK_GROUPS     /**< Number of groups */
} bench_chunk_group_t;

/** A streaming run */
typedef struct bench_chunk_t {
    ecs_world_t *world;            /**< The world */
    ecs_entity_t grid;             /**< The ChunkGrid */
    ecs_entity_t focus;            /**< The camera, the WorldFocus */
    double x;                      /**< Absolute X of the focus */
    double speed;                  /**< Meters per second of the focus */
    ecs_vec_t systems;             /**< ecs_entity_t, every system */
    ecs_vec_t groups;              /**< int32_t, the group of every system */
    int32_t frames;                /**< Frames measured */
    double frame_ms;               /**< Sum of the frame times */
    double worst_ms;               /**< Longest frame */
    double group_ms[BENCH_CHUNK_GROUPS]; /**< Sums of the system times per group */
    double create_ms;              /**< Sum of ChunkGridStatus.create_ms */
    double delete_ms;              /**< Sum of ChunkGridStatus.delete_ms */
    double generate_ms;            /**< Milliseconds of mesh generation */
    int64_t generated;             /**< Meshes generated */
    int64_t cells;                 /**< Cells created or changed */
    int64_t deleted;               /**< Cells deleted */
} bench_chunk_t;

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_chunk_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Get the group of a system by the module it is in.
 *
 * @param world The world
 * @param system The system
 * @return The group
 */
static int32_t bench_chunk_group(
    const ecs_world_t *world,
    ecs_entity_t system)
{
    static const struct { const char *prefix; int32_t group; } prefixes[] = {
        { "box.world.", BENCH_CHUNK_GRID },
        { "box.transform.", BENCH_CHUNK_TRANSFORM },
        { "box.spatial.", BENCH_CHUNK_SPATIAL },
        { "box.geometry.", BENCH_CHUNK_GEOMETRY },
        { "box.mesh.", BENCH_CHUNK_GEOMETRY },
        { "box.render.", BENCH_CHUNK_EXTRACT }
    };
    char *path = ecs_get_path(world, system);
    int32_t result = BENCH_CHUNK_OTHER, i;
    for (i = 0; path && i < (int32_t)(sizeof(prefixes) / sizeof(prefixes[0])); i ++) {
        if (!strncmp(path, prefixes[i].prefix, strlen(prefixes[i].prefix))) {
            result = prefixes[i].group;
            break;
        }
    }

    ecs_os_free(path);

    return result;
}

/** Collect the systems of the world and their groups.
 *
 * @param c The run
 */
static void bench_chunk_systems(
    bench_chunk_t *c)
{
    ecs_vec_init_t(NULL, &c->systems, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &c->groups, int32_t, 0);
    ecs_iter_t it = ecs_each_id(c->world, EcsSystem);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, &c->systems, ecs_entity_t) = it.entities[i];
            *ecs_vec_append_t(NULL, &c->groups, int32_t) =
                bench_chunk_group(c->world, it.entities[i]);
        }
    }
}

/** Get the milliseconds a system took in the last frame, over every stage.
 *
 * @param world The world
 * @param system The system
 * @return The milliseconds
 */
static double bench_chunk_system_ms(
    const ecs_world_t *world,
    ecs_entity_t system)
{
    const ecs_system_t *s = ecs_system_get(world, system);
    double result = 0;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->time_spent); i ++) {
        result += (double)*ecs_vec_get_t(&s->time_spent, ecs_ftime_t, i);
    }

    return result * 1000.0;
}

/** Place the focus at its absolute position.
 *
 * @param c The run
 */
static void bench_chunk_place(
    bench_chunk_t *c)
{
    double absolute[3] = { c->x, 1.7, BENCH_CHUNK_CELL * 0.5 };
    vec3 local;
    box_world_to_local(c->world, absolute, local);
    ecs_set(c->world, c->focus, Position, {local[0], local[1], local[2]});
}

/** Create the world, the grid of the forest cells and the focus, and load the cells in range.
 *
 * @param c The run
 * @param speed Meters per second of the focus
 * @param budget_ms The budget_ms of the grid
 */
static void bench_chunk_init(
    bench_chunk_t *c,
    double speed,
    float budget_ms)
{
    ecs_os_memset_t(c, 0, bench_chunk_t);
    ecs_world_t *world = c->world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_chunk_drop };
    box_render_set_backend(world, &backend);

    char *path = box_app_locate("test/assets/world/chunk_spawn.flecs");
    if (!path || ecs_script_run_file(world, path)) {
        ecs_err("ChunkSpawn: cannot run test/assets/world/chunk_spawn.flecs");
    }

    ecs_os_free(path);
    ecs_entity_t template = ecs_lookup(world, "ChunkSpawnCell");

    c->speed = speed;
    c->x = BENCH_CHUNK_CELL * 0.5;
    c->focus = ecs_new(world);
    ecs_set(world, c->focus, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_add(world, c->focus, WorldFocus);
    bench_chunk_place(c);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = c->focus, .lighting.enabled = true,
        .culling = {.enabled = true, .min_dimension_px = 1}});

    c->grid = ecs_new(world);
    ecs_set(world, c->grid, ChunkGrid, {
        .template = template,
        .cell_size = {BENCH_CHUNK_CELL, 0, BENCH_CHUNK_CELL},
        .levels = {48, 96},
        .hysteresis = 8,
        .budget = 16,
        .budget_ms = budget_ms
    });

    int32_t i;
    for (i = 0; i < BENCH_CHUNK_LOAD_FRAMES; i ++) {
        ecs_progress(world, (ecs_ftime_t)(1.0 / BENCH_CHUNK_FPS));
        const ChunkGridStatus *s = ecs_get(world, c->grid, ChunkGridStatus);
        if (i > 1 && s && !s->pending && !s->unloading) {
            break;
        }
    }

    ecs_measure_system_time(world, true);
    bench_chunk_systems(c);
}

/** Run a frame: move the focus and account where the time went.
 *
 * @param c The run
 */
static void bench_chunk_frame(
    bench_chunk_t *c)
{
    ecs_world_t *world = c->world;
    box_mesh_cache_stats_t before, after;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_time_t t;
    int32_t i;
    c->x += c->speed / BENCH_CHUNK_FPS;
    bench_chunk_place(c);
    box_mesh_cache_stats(cache, &before);
    ecs_os_get_time(&t);
    ecs_progress(world, (ecs_ftime_t)(1.0 / BENCH_CHUNK_FPS));
    double ms = ecs_time_measure(&t) * 1000.0;
    box_mesh_cache_stats(cache, &after);

    c->frames ++;
    c->frame_ms += ms;
    c->worst_ms = ms > c->worst_ms ? ms : c->worst_ms;
    c->generated += after.generated - before.generated;
    c->generate_ms += after.generate_ms - before.generate_ms;
    for (i = 0; i < ecs_vec_count(&c->systems); i ++) {
        ecs_entity_t system = *ecs_vec_get_t(&c->systems, ecs_entity_t, i);
        int32_t group = *ecs_vec_get_t(&c->groups, int32_t, i);
        c->group_ms[group] += bench_chunk_system_ms(world, system);
    }

    const ChunkGridStatus *s = ecs_get(world, c->grid, ChunkGridStatus);
    if (s) {
        c->create_ms += s->create_ms;
        c->delete_ms += s->delete_ms;
        c->cells += s->created + s->changed;
        c->deleted += s->deleted;
    }
}

/** Report the counters of a run and free it.
 *
 * Times are milliseconds per frame: template_ms creates and changes cells, the evaluation of
 * the template and the observers of its entities, meshgen_ms generates meshes, delete_ms
 * deletes cells, and the systems split into transform, spatial, geometry, extract and other.
 *
 * @param b The bench
 * @param c The run
 */
static void bench_chunk_fini(
    bench_t *b,
    bench_chunk_t *c)
{
    double n = c->frames ? (double)c->frames : 1.0;
    double template_ms = c->create_ms - c->generate_ms;
    double other_ms = c->frame_ms - c->create_ms - c->delete_ms;
    int32_t g;
    for (g = BENCH_CHUNK_TRANSFORM; g < BENCH_CHUNK_OTHER; g ++) {
        other_ms -= c->group_ms[g];
    }

    box_render_draws_stats_t draws = {0};
    box_render_draws_stats(c->world, &draws);
    bench_counter(b, "frames", (double)c->frames);
    bench_counter(b, "worst_ms", c->worst_ms);
    bench_counter(b, "template_ms", (template_ms > 0 ? template_ms : 0) / n);
    bench_counter(b, "meshgen_ms", c->generate_ms / n);
    bench_counter(b, "delete_ms", c->delete_ms / n);
    bench_counter(b, "transform_ms", c->group_ms[BENCH_CHUNK_TRANSFORM] / n);
    bench_counter(b, "spatial_ms", c->group_ms[BENCH_CHUNK_SPATIAL] / n);
    bench_counter(b, "geometry_ms", c->group_ms[BENCH_CHUNK_GEOMETRY] / n);
    bench_counter(b, "extract_ms", c->group_ms[BENCH_CHUNK_EXTRACT] / n);
    bench_counter(b, "other_ms", other_ms / n);
    bench_counter(b, "cell_ms", c->cells ? c->create_ms / (double)c->cells : 0);
    bench_counter(b, "delete_cell_ms", c->deleted ? c->delete_ms / (double)c->deleted : 0);
    bench_counter(b, "entities", (double)ecs_get_entities(c->world).alive_count);
    bench_counter(b, "meshes", (double)box_mesh_cache_count(box_mesh_cache_of(c->world)));
    bench_counter(b, "batches", (double)draws.batches);
    bench_counter(b, "generated", (double)c->generated);

    ecs_vec_fini_t(NULL, &c->systems, ecs_entity_t);
    ecs_vec_fini_t(NULL, &c->groups, int32_t);
    ecs_fini(c->world);
}

/** Stream the grid frame by frame with the focus at a speed.
 *
 * @param b The bench
 * @param speed Meters per second of the focus
 * @param budget_ms The budget_ms of the grid
 */
static void bench_chunk_run(
    bench_t *b,
    double speed,
    float budget_ms)
{
    bench_chunk_t c;
    bench_chunk_init(&c, speed, budget_ms);
    while (bench_iter(b)) {
        bench_chunk_frame(&c);
    }

    bench_chunk_fini(b, &c);
}

void ChunkSpawn_walk(
    bench_t *b)
{
    bench_chunk_run(b, 5.0, 0);
}

void ChunkSpawn_fly(
    bench_t *b)
{
    bench_chunk_run(b, 40.0, 0);
}

void ChunkSpawn_fly_budget_4ms(
    bench_t *b)
{
    bench_chunk_run(b, 40.0, 4.0f);
}
