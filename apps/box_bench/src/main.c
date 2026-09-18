/**
 * @file main.c
 * @brief Benchmark driver: loads a scene headless, churns static entities and reports timings.
 */

#include <box.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/surface/surface_internal.h"

/** Benchmark settings and results */
typedef struct box_bench_t {
    const char *scene;     /**< Scene file */
    const char *report;    /**< JSON output path */
    int32_t frames;        /**< Frames to measure */
    int32_t warmup;        /**< Frames to skip before measuring */
    int32_t churn_min;     /**< Minimum static entities moved per frame */
    int32_t churn_max;     /**< Maximum static entities moved per frame */
    bool churn_color;      /**< Change colors instead of positions */
    int32_t width;         /**< Surface width */
    int32_t height;        /**< Surface height */
    double *samples;       /**< Frame times in seconds */
    int32_t sample_count;  /**< Samples recorded */
    ecs_query_t *statics;  /**< Query over static renderable entities */
    ecs_query_t *surfaces; /**< Query over the surfaces that own a renderer */
    ecs_vec_t entities;    /**< ecs_entity_t snapshot of static entities */
    double drawn;          /**< Instances the measured frames drew */
    double culled;         /**< Instances the measured frames skipped */
    double rebuilds;       /**< Cached static cascade layers the measured frames rendered */
    int32_t stat_count;    /**< Frames the instance counts were read on */
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
            "[--churn-color] [--report file.json] [--size WxH]");
        result = -1;
    }

    return result;
}

/** Collect the static renderable entities once after loading.
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

/** Compare doubles for qsort.
 *
 * @param a First value
 * @param b Second value
 * @return Ordering
 */
static int box_bench_compare(
    const void *a,
    const void *b)
{
    double da = *(const double*)a, db = *(const double*)b;

    return (da > db) - (da < db);
}

/** Write the benchmark report as JSON.
 *
 * @param world The world
 * @param bench The benchmark
 */
static void box_bench_report(
    ecs_world_t *world,
    box_bench_t *bench)
{
    qsort(bench->samples, (size_t)bench->sample_count, sizeof(double), box_bench_compare);
    double sum = 0;
    int32_t i;
    for (i = 0; i < bench->sample_count; i ++) {
        sum += bench->samples[i];
    }

    double mean = bench->sample_count ? sum / bench->sample_count : 0;
    double p50 = bench->samples[bench->sample_count / 2];
    double p99 = bench->samples[(bench->sample_count * 99) / 100];
    double worst = bench->samples[bench->sample_count - 1];
    int32_t entity_count = ecs_count_id(world, ecs_id(RenderMesh));
    int32_t static_count = ecs_vec_count(&bench->entities);

    FILE *f = bench->report ? fopen(bench->report, "w") : stdout;
    double scale = bench->stat_count ? 1.0 / bench->stat_count : 0;
    fprintf(f, "{\n  \"scene\": \"%s\",\n  \"entities\": %d,\n  \"static\": %d,\n"
        "  \"dynamic\": %d,\n  \"churn\": [%d, %d],\n  \"frames\": %d,\n"
        "  \"mean_ms\": %.3f,\n  \"p50_ms\": %.3f,\n  \"p99_ms\": %.3f,\n  \"max_ms\": %.3f,\n"
        "  \"fps\": %.1f,\n  \"drawn\": %.0f,\n  \"culled\": %.0f,\n"
        "  \"shadow_static_rebuilds\": %.0f\n}\n",
        bench->scene, entity_count, static_count, entity_count - static_count,
        bench->churn_min, bench->churn_max, bench->sample_count,
        mean * 1000, p50 * 1000, p99 * 1000, worst * 1000, mean > 0 ? 1.0 / mean : 0,
        bench->drawn * scale, bench->culled * scale, bench->rebuilds);
    if (f != stdout) {
        fclose(f);
        printf("entities %d, mean %.3f ms, p99 %.3f ms, max %.3f ms, %.1f fps, "
            "drawn %.0f, culled %.0f, shadow rebuilds %.0f\n",
            entity_count, mean * 1000, p99 * 1000, worst * 1000, mean > 0 ? 1.0 / mean : 0,
            bench->drawn * scale, bench->culled * scale, bench->rebuilds);
    }
}

/** Load the scene, run the frames and write the report.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 if success, nonzero if not successful
 */
int main(
    int argc,
    char *argv[])
{
    int result = 0;
    box_bench_t bench = { .frames = 300, .warmup = 30, .width = 1280, .height = 720 };
    if (box_bench_parse(&bench, argc, argv)) {
        result = -1;
        goto done;
    }

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxJob);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxMonitor);

    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    Surface *surface = ecs_singleton_ensure(world, Surface);
    surface->width = bench.width;
    surface->height = bench.height;
    ecs_singleton_modified(world, Surface);

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
    bench.surfaces = ecs_query(world, {
        .terms = {{ .id = ecs_id(SurfaceImpl) }}
    });
    box_bench_collect(world, &bench);
    bench.samples = ecs_os_malloc_n(double, bench.frames);

    int32_t frame;
    for (frame = 0; frame < bench.warmup + bench.frames; frame ++) {
        box_bench_churn(world, &bench);
        ecs_time_t start;
        ecs_os_get_time(&start);
        ecs_progress(world, 0);
        double elapsed = ecs_time_measure(&start);
        if (frame >= bench.warmup) {
            bench.samples[bench.sample_count ++] = elapsed;
            box_bench_stats(world, &bench);
        }
    }

    box_bench_report(world, &bench);
    ecs_os_free(bench.samples);
    ecs_vec_fini_t(NULL, &bench.entities, ecs_entity_t);
    ecs_query_fini(bench.statics);
    ecs_query_fini(bench.surfaces);
fini:
    ecs_fini(world);
done:
    return result;
}
