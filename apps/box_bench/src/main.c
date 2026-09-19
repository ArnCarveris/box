/**
 * @file main.c
 * @brief Benchmark driver: loads a scene headless, churns entities and reports timings.
 *
 * A run reports the distribution of its frames, the frames that exceeded the
 * spike budget and the system each of those frames spent the most time in, and
 * fails when a frame exceeded the budget.
 */

#include <box.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/surface/surface_internal.h"
#include "../../../src/monitor/monitor_internal.h"

/** Benchmark settings and results */
typedef struct box_bench_t {
    const char *scene;      /**< Scene file */
    const char *report;     /**< JSON output path */
    int32_t frames;         /**< Frames to measure */
    int32_t warmup;         /**< Frames to skip before measuring */
    int32_t churn_min;      /**< Minimum static entities moved per frame */
    int32_t churn_max;      /**< Maximum static entities moved per frame */
    bool churn_color;       /**< Change colors instead of positions */
    int32_t light_churn;    /**< Static lights changed per frame, which rebakes probes */
    double spike_factor;    /**< Spike budget as a multiple of the mean, zero disables */
    bool attribute;         /**< Attribute every frame to its slowest system */
    double fps;             /**< Fixed step in frames per second, zero measures the delta */
    int32_t width;          /**< Surface width */
    int32_t height;         /**< Surface height */
    box_spikes_t spikes;    /**< Frame times and the system behind each frame */
    ecs_query_t *statics;   /**< Query over static renderable entities */
    ecs_query_t *lights;    /**< Query over static point lights */
    ecs_query_t *surfaces;  /**< Query over the surfaces that own a renderer */
    ecs_vec_t entities;     /**< ecs_entity_t snapshot of static entities */
    ecs_vec_t light_list;   /**< ecs_entity_t snapshot of static point lights */
    int32_t light_next;     /**< Light the next churn starts at */
    double drawn;           /**< Instances the measured frames drew */
    double culled;          /**< Instances the measured frames skipped */
    double occluded;        /**< Instances the depth pyramid of the measured frames hid */
    double rebuilds;        /**< Cached static cascade layers the measured frames rendered */
    int32_t stat_count;     /**< Frames the instance counts were read on */
} box_bench_t;

/** Add the instance counts of the last rendered frame to the totals.
 *
 * @param world The world
 * @param bench The benchmark
 */
static void box_bench_stats(
    ecs_world_t *world,
    box_bench_t *bench)
{
    ecs_iter_t it = ecs_query_iter(world, bench->surfaces);
    while (ecs_query_next(&it)) {
        SurfaceImpl *impl = ecs_field(&it, SurfaceImpl, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!impl[i].renderer) {
                continue;
            }

            box_renderer_stats_t stats;
            box_renderer_stats(impl[i].renderer, &stats);
            bench->drawn += stats.drawn;
            bench->culled += stats.culled;
            bench->occluded += stats.occluded;
            bench->rebuilds += stats.shadow_rebuilds;
            bench->stat_count ++;
        }
    }
}

/** Parse benchmark options.
 *
 * @param bench The settings to fill
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, nonzero on invalid arguments
 */
static int box_bench_parse(
    box_bench_t *bench,
    int argc,
    char *argv[])
{
    int result = 0;
    int i;
    for (i = 1; i < argc; i ++) {
        const char *arg = argv[i];
        const char *value = (i + 1) < argc ? argv[i + 1] : NULL;
        if (!strcmp(arg, "--frames") && value) {
            bench->frames = atoi(value);
            i ++;
        } else if (!strcmp(arg, "--warmup") && value) {
            bench->warmup = atoi(value);
            i ++;
        } else if (!strcmp(arg, "--churn") && value) {
            bench->churn_min = atoi(value);
            bench->churn_max = strchr(value, '-') ? atoi(strchr(value, '-') + 1) : bench->churn_min;
            i ++;
        } else if (!strcmp(arg, "--churn-color")) {
            bench->churn_color = true;
        } else if (!strcmp(arg, "--light-churn") && value) {
            bench->light_churn = atoi(value);
            i ++;
        } else if (!strcmp(arg, "--spike") && value) {
            bench->spike_factor = atof(value);
            i ++;
        } else if (!strcmp(arg, "--attribute")) {
            bench->attribute = true;
        } else if (!strcmp(arg, "--fps") && value) {
            bench->fps = atof(value);
            i ++;
        } else if (!strcmp(arg, "--report") && value) {
            bench->report = value;
            i ++;
        } else if (!strcmp(arg, "--size") && value) {
            bench->width = atoi(value);
            bench->height = strchr(value, 'x') ? atoi(strchr(value, 'x') + 1) : bench->width;
            i ++;
        } else if (arg[0] != '-') {
            bench->scene = arg;
        } else {
            ecs_err("unknown option '%s'", arg);
            result = -1;
        }
    }

    if (!bench->scene) {
        ecs_err("usage: box_bench scene.flecs [--frames n] [--warmup n] [--churn min-max] "
            "[--churn-color] [--light-churn n] [--spike factor] [--attribute] [--fps n] "
            "[--report file.json] [--size WxH]");
        result = -1;
    }

    return result;
}

/** Collect the static renderable entities and the static lights once after loading.
 *
 * @param world The world
 * @param bench The benchmark
 */
static void box_bench_collect(
    ecs_world_t *world,
    box_bench_t *bench)
{
    ecs_vec_init_t(NULL, &bench->entities, ecs_entity_t, 0);
    ecs_iter_t it = ecs_query_iter(world, bench->statics);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &bench->entities, ecs_entity_t)[0] = it.entities[i];
        }
    }

    ecs_vec_init_t(NULL, &bench->light_list, ecs_entity_t, 0);
    ecs_iter_t lit = ecs_query_iter(world, bench->lights);
    while (ecs_query_next(&lit)) {
        int32_t i;
        for (i = 0; i < lit.count; i ++) {
            ecs_vec_append_t(NULL, &bench->light_list, ecs_entity_t)[0] = lit.entities[i];
        }
    }
}

/** Move a random number of static entities by a small offset.
 *
 * @param world The world
 * @param bench The benchmark
 */
static void box_bench_churn(
    ecs_world_t *world,
    box_bench_t *bench)
{
    int32_t total = ecs_vec_count(&bench->entities);
    if (!total || bench->churn_max <= 0) {
        goto done;
    }

    int32_t span = bench->churn_max - bench->churn_min + 1;
    int32_t count = bench->churn_min + (span > 0 ? rand() % span : 0);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&bench->entities, ecs_entity_t, rand() % total)[0];
        if (bench->churn_color) {
            Rgb *c = ecs_ensure(world, e, Rgb);
            c->r = (uint8_t)(rand() % 256);
            ecs_modified(world, e, Rgb);
        } else {
            Position *p = ecs_ensure(world, e, Position);
            p->y += ((float)rand() / (float)RAND_MAX - 0.5f) * 0.01f;
            ecs_modified(world, e, Position);
        }
    }
done:
    return;
}

/** Change the intensity of the next static lights, which invalidates their probes.
 *
 * @param world The world
 * @param bench The benchmark
 * @param frame The frame being run
 */
static void box_bench_churn_lights(
    ecs_world_t *world,
    box_bench_t *bench,
    int32_t frame)
{
    int32_t total = ecs_vec_count(&bench->light_list);
    if (!total || bench->light_churn <= 0) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < bench->light_churn; i ++) {
        int32_t index = (bench->light_next + i) % total;
        ecs_entity_t e = ecs_vec_get_t(&bench->light_list, ecs_entity_t, index)[0];
        PointLight *light = ecs_ensure(world, e, PointLight);
        light->intensity = 1.0f + 0.5f * (float)((frame + index) % 3);
        ecs_modified(world, e, PointLight);
    }

    bench->light_next = (bench->light_next + bench->light_churn) % total;
done:
    return;
}

/** Record the time of a measured frame with the system that dominated it.
 *
 * @param world The world
 * @param bench The benchmark
 * @param elapsed Seconds the frame took
 */
static void box_bench_record(
    ecs_world_t *world,
    box_bench_t *bench,
    double elapsed)
{
    ecs_entity_t system = 0;
    double system_ms = 0;
    if (bench->attribute) {
        const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
        if (m) {
            system = m->slowest[0].system;
            system_ms = m->slowest[0].time_ms;
        }
    }

    box_spikes_add(&bench->spikes, elapsed * 1000, system, system_ms);
}

/** Write the frames that exceeded the budget as a JSON array body.
 *
 * @param f The report file
 * @param world The world
 * @param report The evaluated run
 */
static void box_bench_spikes_json(
    FILE *f,
    ecs_world_t *world,
    const box_spike_report_t *report)
{
    int32_t i, count = report->count < BOX_SPIKE_MAX ? report->count : BOX_SPIKE_MAX;
    for (i = 0; i < count; i ++) {
        const box_spike_frame_t *frame = &report->worst[i];
        char *path = frame->system ? ecs_get_path(world, frame->system) : NULL;
        fprintf(f, "%s\n    {\"frame\": %d, \"ms\": %.3f, \"system\": \"%s\", "
            "\"system_ms\": %.3f}", i ? "," : "", frame->frame, frame->time_ms,
            path ? path : "", frame->system_ms);
        ecs_os_free(path);
    }
}

/** Print one line per frame that exceeded the budget, worst first.
 *
 * @param world The world
 * @param report The evaluated run
 */
static void box_bench_print_spikes(
    ecs_world_t *world,
    const box_spike_report_t *report)
{
    int32_t i, count = report->count < BOX_SPIKE_MAX ? report->count : BOX_SPIKE_MAX;
    for (i = 0; i < count; i ++) {
        const box_spike_frame_t *frame = &report->worst[i];
        char *path = frame->system ? ecs_get_path(world, frame->system) : NULL;
        printf("SPIKE frame %d took %.3f ms over the %.3f ms budget, top system %s %.3f ms\n",
            frame->frame, frame->time_ms, report->budget_ms,
            path ? path : "unattributed", frame->system_ms);
        ecs_os_free(path);
    }
}

/** Write the benchmark report as JSON and print its summary.
 *
 * @param world The world
 * @param bench The benchmark
 * @param report The evaluated run
 */
static void box_bench_report(
    ecs_world_t *world,
    box_bench_t *bench,
    const box_spike_report_t *report)
{
    int32_t entity_count = ecs_count_id(world, ecs_id(RenderMesh));
    int32_t static_count = ecs_vec_count(&bench->entities);
    double mean = report->mean_ms * 0.001;
    FILE *f = bench->report ? fopen(bench->report, "w") : stdout;
    double scale = bench->stat_count ? 1.0 / bench->stat_count : 0;
    fprintf(f, "{\n  \"scene\": \"%s\",\n  \"entities\": %d,\n  \"static\": %d,\n"
        "  \"dynamic\": %d,\n  \"churn\": [%d, %d],\n  \"lights\": %d,\n"
        "  \"light_churn\": %d,\n  \"frames\": %d,\n"
        "  \"mean_ms\": %.3f,\n  \"p50_ms\": %.3f,\n  \"p99_ms\": %.3f,\n  \"max_ms\": %.3f,\n"
        "  \"fps\": %.1f,\n  \"drawn\": %.0f,\n  \"culled\": %.0f,\n  \"occluded\": %.0f,\n"
        "  \"shadow_static_rebuilds\": %.0f,\n  \"spike_factor\": %.2f,\n"
        "  \"spike_budget_ms\": %.3f,\n  \"attributed\": %s,\n  \"spike_count\": %d,\n"
        "  \"spikes\": [",
        bench->scene, entity_count, static_count, entity_count - static_count,
        bench->churn_min, bench->churn_max, ecs_vec_count(&bench->light_list),
        bench->light_churn, bench->spikes.count,
        report->mean_ms, report->p50_ms, report->p99_ms, report->max_ms,
        mean > 0 ? 1.0 / mean : 0, bench->drawn * scale, bench->culled * scale,
        bench->occluded * scale, bench->rebuilds, bench->spike_factor, report->budget_ms,
        bench->attribute ? "true" : "false", report->count);
    box_bench_spikes_json(f, world, report);
    fprintf(f, "%s]\n}\n", report->count ? "\n  " : "");
    if (f != stdout) {
        fclose(f);
    }

    printf("entities %d, mean %.3f ms, p99 %.3f ms, max %.3f ms, %.1f fps, "
        "drawn %.0f, culled %.0f, occluded %.0f, shadow rebuilds %.0f, "
        "budget %.3f ms, spikes %d\n",
        entity_count, report->mean_ms, report->p99_ms, report->max_ms, mean > 0 ? 1.0 / mean : 0,
        bench->drawn * scale, bench->culled * scale, bench->occluded * scale,
        bench->rebuilds, report->budget_ms, report->count);
    box_bench_print_spikes(world, report);
}

/** Import the modules a scene needs and open a headless surface.
 *
 * @param world The world
 * @param bench The benchmark
 */
static void box_bench_init(
    ecs_world_t *world,
    box_bench_t *bench)
{
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxJob);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxGltf);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxController);
    ECS_IMPORT(world, BoxMonitor);
    ECS_IMPORT(world, BoxScript);

    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    Surface *surface = ecs_singleton_ensure(world, Surface);
    surface->width = bench->width;
    surface->height = bench->height;
    ecs_singleton_modified(world, Surface);
    if (bench->attribute) {
        ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    }
}

/** Load the scene, run the frames and write the report.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 if no frame exceeded the spike budget, nonzero if one did
 */
int main(
    int argc,
    char *argv[])
{
    int result = 0;
    box_bench_t bench = { .frames = 300, .warmup = 30, .width = 1280, .height = 720,
        .spike_factor = BOX_SPIKE_FACTOR };
    if (box_bench_parse(&bench, argc, argv)) {
        result = -1;
        goto done;
    }

    ecs_world_t *world = ecs_init();
    box_bench_init(world, &bench);

    ecs_time_t load_start;
    ecs_os_get_time(&load_start);
    if (!ecs_script(world, { .filename = bench.scene })) {
        ecs_err("failed to load '%s'", bench.scene);
        result = -1;
        goto fini;
    }

    int32_t loaded = ecs_count_id(world, ecs_id(RenderMesh));
    printf("loaded %s in %.2f s, %d renderable entities\n", bench.scene,
        ecs_time_measure(&load_start), loaded);
    if (!loaded) {
        ecs_err("scene '%s' has no renderable entities", bench.scene);
        result = -1;
        goto fini;
    }

    bench.statics = ecs_query(world, {
        .expr = "box.render.RenderMesh, box.transform.Position, "
            "!box.transform.DynamicTransform(self|up), !box.material.DynamicMaterial(self|up)"
    });
    bench.lights = ecs_query(world, {
        .expr = "box.light.PointLight, "
            "!box.transform.DynamicTransform(self|up), !box.material.DynamicMaterial(self|up)"
    });
    bench.surfaces = ecs_query(world, {
        .terms = {{ .id = ecs_id(SurfaceImpl) }}
    });
    box_bench_collect(world, &bench);
    box_spikes_init(&bench.spikes, bench.frames, bench.spike_factor);

    float delta = bench.fps > 0 ? (float)(1.0 / bench.fps) : 0;
    int32_t frame;
    for (frame = 0; frame < bench.warmup + bench.frames; frame ++) {
        box_bench_churn(world, &bench);
        box_bench_churn_lights(world, &bench, frame);
        ecs_time_t start;
        ecs_os_get_time(&start);
        ecs_progress(world, delta);
        double elapsed = ecs_time_measure(&start);
        if (frame >= bench.warmup) {
            box_bench_record(world, &bench, elapsed);
            box_bench_stats(world, &bench);
        }
    }

    box_spike_report_t report;
    box_spikes_eval(&bench.spikes, &report);
    box_bench_report(world, &bench, &report);
    result = report.count ? -1 : 0;
    box_spikes_fini(&bench.spikes);
    ecs_vec_fini_t(NULL, &bench.entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &bench.light_list, ecs_entity_t);
    ecs_query_fini(bench.statics);
    ecs_query_fini(bench.lights);
    ecs_query_fini(bench.surfaces);
fini:
    ecs_fini(world);
done:
    return result;
}
