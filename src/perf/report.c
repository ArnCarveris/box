/**
 * @file perf/report.c
 * @brief Frame interval statistics and the JSON report of a performance run.
 */

#include "perf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Order two frame intervals for qsort.
 *
 * @param a The first interval
 * @param b The second interval
 * @return Negative, zero or positive when a is shorter, equal or longer
 */
static int box_perf_compare(
    const void *a,
    const void *b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;

    return (x > y) - (x < y);
}

/** Pick a nearest rank percentile of sorted intervals.
 *
 * @param sorted The sorted intervals
 * @param count The number of intervals
 * @param pct The percentile in 0..100
 * @return The interval at the percentile
 */
static double box_perf_percentile(
    const double *sorted,
    int32_t count,
    double pct)
{
    int32_t rank = (int32_t)ceil(pct / 100.0 * (double)count);
    rank = rank < 1 ? 1 : rank;

    return sorted[(rank > count ? count : rank) - 1];
}

void box_perf_stats(
    const ecs_vec_t *values,
    box_perf_stats_t *stats)
{
    int32_t count = ecs_vec_count(values);
    double *sorted;
    double sum = 0;
    int32_t i;
    ecs_os_memset_t(stats, 0, box_perf_stats_t);
    if (!count) {
        goto done;
    }

    sorted = ecs_os_malloc_n(double, count);
    ecs_os_memcpy_n(sorted, ecs_vec_first_t(values, double), double, count);
    qsort(sorted, (size_t)count, sizeof(double), box_perf_compare);
    for (i = 0; i < count; i ++) {
        sum += sorted[i];
    }

    stats->min = sorted[0];
    stats->max = sorted[count - 1];
    stats->mean = sum / (double)count;
    stats->p50 = box_perf_percentile(sorted, count, 50);
    stats->p95 = box_perf_percentile(sorted, count, 95);
    stats->p99 = box_perf_percentile(sorted, count, 99);
    ecs_os_free(sorted);
done:
    return;
}

/** Append a culling range as a JSON member.
 *
 * @param buf The buffer
 * @param name The name of the member
 * @param range The range
 */
static void box_perf_report_range(
    ecs_strbuf_t *buf,
    const char *name,
    const PerfRange *range)
{
    ecs_strbuf_append(buf, "    \"%s\": {\"min\": %d, \"max\": %d, \"mean\": %.1f}",
        name, range->min, range->max, range->mean);
}

/** Append the orbit and the culling the orbiting views saw as JSON members.
 *
 * @param buf The buffer
 * @param status The completed status
 */
static void box_perf_report_orbit(
    ecs_strbuf_t *buf,
    const PerfStatus *status)
{
    ecs_strbuf_append(buf,
        "  \"warmup\": %d,\n  \"period\": %.3f,\n  \"revolutions\": %.3f,\n"
        "  \"views\": %d,\n  \"cull\": {\n    \"samples\": %d,\n    \"changes\": %d,\n",
        status->warmup, (double)status->period, (double)status->revolutions, status->views,
        status->cull_samples, status->cull_changes);
    box_perf_report_range(buf, "visible", &status->visible);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "frustum_culled", &status->frustum_culled);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "occlusion_culled", &status->occlusion_culled);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "range_culled", &status->range_culled);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "batches_skipped", &status->batches_skipped);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "casters_skipped", &status->casters_skipped);
    ecs_strbuf_appendstr(buf, "\n  },\n");
}

/** Append the texture graph cache counts per frame as JSON members.
 *
 * @param buf The buffer
 * @param status The completed status
 */
static void box_perf_report_graphs(
    ecs_strbuf_t *buf,
    const PerfStatus *status)
{
    const PerfTextureGraphs *g = &status->texture_graphs;
    ecs_strbuf_append(buf, "  \"texture_graphs\": {\n    \"samples\": %d,\n", g->samples);
    box_perf_report_range(buf, "hits", &g->hits);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "misses", &g->misses);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "evictions", &g->evictions);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "updates", &g->updates);
    ecs_strbuf_appendstr(buf, ",\n");
    box_perf_report_range(buf, "cached_kb", &g->cached_kb);
    ecs_strbuf_appendstr(buf, "\n  },\n");
}

/** Append the grass GPU time and blades as JSON members.
 *
 * @param buf The buffer
 * @param status The completed status
 */
static void box_perf_report_grass(
    ecs_strbuf_t *buf,
    const PerfStatus *status)
{
    const PerfGrass *g = &status->grass;
    const PerfGrassLevel *levels[3] = { &g->lod0, &g->lod1, &g->lod2 };
    int32_t i;
    ecs_strbuf_append(buf,
        "  \"grass\": {\n    \"passes\": %d,\n    \"timed\": %d,\n    \"samples\": %d,\n"
        "    \"mean_ms\": %.4f,\n    \"p50_ms\": %.4f,\n    \"p95_ms\": %.4f,\n"
        "    \"p99_ms\": %.4f,\n    \"max_ms\": %.4f,\n    \"reads\": %d,\n"
        "    \"generated\": %.1f,\n    \"drawn\": %.1f,\n    \"culled_patches\": %.1f,\n"
        "    \"culled_blades\": %.1f,\n    \"lods\": [",
        g->passes, g->timed, g->samples, g->mean_ms, g->p50_ms, g->p95_ms, g->p99_ms,
        g->max_ms, g->reads, g->generated, g->drawn, g->culled_patches, g->culled_blades);
    for (i = 0; i < 3; i ++) {
        ecs_strbuf_append(buf, "%s{\"generated\": %.1f, \"drawn\": %.1f}", i ? ", " : "",
            levels[i]->generated, levels[i]->drawn);
    }

    ecs_strbuf_appendstr(buf, "]\n  },\n");
}

/** Append a series of values per measured frame as a JSON member.
 *
 * @param buf The buffer
 * @param name The name of the member
 * @param values The values, of double
 * @param last Whether the member is the last of the object
 */
static void box_perf_report_series(
    ecs_strbuf_t *buf,
    const char *name,
    const ecs_vec_t *values,
    bool last)
{
    const double *v = ecs_vec_first_t(values, double);
    int32_t count = ecs_vec_count(values), i;
    ecs_strbuf_append(buf, "  \"%s\": [", name);
    for (i = 0; i < count; i ++) {
        ecs_strbuf_append(buf, i ? ", %.4f" : "%.4f", v[i]);
    }

    ecs_strbuf_appendstr(buf, last ? "]\n" : "],\n");
}

/** Append the statistics of a status as JSON members.
 *
 * @param buf The buffer
 * @param status The completed status
 */
static void box_perf_report_stats(
    ecs_strbuf_t *buf,
    const PerfStatus *status)
{
    const PerfChecks *checks = &status->checks;
    double fps = status->mean_ms > 0 ? 1000.0 / status->mean_ms : 0;
    ecs_strbuf_append(buf,
        "  \"passed\": %s,\n  \"frames\": %d,\n  \"errors\": %d,\n  \"slow\": %d,\n"
        "  \"fast\": %d,\n  \"target_ms\": %.4f,\n  \"tolerance\": %.2f,\n"
        "  \"min_ms\": %.4f,\n  \"mean_ms\": %.4f,\n  \"p50_ms\": %.4f,\n"
        "  \"p95_ms\": %.4f,\n  \"p99_ms\": %.4f,\n  \"max_ms\": %.4f,\n  \"fps\": %.2f,\n"
        "  \"slept_ms\": %.3f,\n  \"stalls\": %d,\n  \"shapes\": %d,\n"
        "  \"audio_load\": %.4f,\n",
        status->passed ? "true" : "false", status->frame, status->errors, status->slow,
        status->fast, status->target_ms, BOX_PERF_TOLERANCE, status->min_ms, status->mean_ms,
        status->p50_ms, status->p95_ms, status->p99_ms, status->max_ms, fps, status->slept_ms,
        status->stalls, status->shapes, (double)status->audio_load);
    ecs_strbuf_append(buf,
        "  \"delta_errors\": %d,\n  \"delta_tolerance_ms\": %.3f,\n  \"delta_max_ms\": %.4f,\n"
        "  \"present_mean_ms\": %.4f,\n"
        "  \"cpu_percent\": %.2f,\n  \"cpu_ms\": %.4f,\n  \"fly\": %s,\n"
        "  \"travelled\": %.3f,\n",
        checks->delta_errors, checks->delta_tolerance_ms, checks->delta_max_ms,
        checks->present_mean_ms, checks->cpu_percent, checks->cpu_ms,
        checks->fly ? "true" : "false", (double)checks->travelled);
    ecs_strbuf_append(buf,
        "  \"center\": [%.4f, %.4f, %.4f],\n  \"radius\": %.4f,\n  \"distance\": %.4f,\n",
        (double)status->center[0], (double)status->center[1], (double)status->center[2],
        (double)status->radius, (double)status->distance);
}

int box_perf_report(
    ecs_world_t *world,
    const PerfRun *run,
    const PerfStatus *status,
    const box_perf_t *perf)
{
    const App *app = ecs_singleton_get(world, App);
    char *scene = flecs_astresc('"', app && app->scene ? app->scene : "");
    int target = (int)(status->target_ms > 0 ? 1000.0 / status->target_ms + 0.5 : 0);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char *json;
    FILE *f;
    int result = -1;
    ecs_strbuf_append(&buf, "{\n  \"scene\": \"%s\",\n  \"target_fps\": %d,\n", scene, target);
    ecs_strbuf_append(&buf, "  \"stall_ms\": %.3f,\n", (double)run->stall);
    box_perf_report_stats(&buf, status);
    box_perf_report_orbit(&buf, status);
    box_perf_report_graphs(&buf, status);
    box_perf_report_grass(&buf, status);
    box_perf_report_series(&buf, "grass_ms", &perf->grass_ms, false);
    box_perf_report_series(&buf, "grass_drawn", &perf->grass_drawn, false);
    box_perf_report_series(&buf, "intervals_ms", &perf->intervals, true);
    ecs_strbuf_appendstr(&buf, "}\n");
    json = ecs_strbuf_get(&buf);
    f = fopen(run->report, "w");
    if (!f) {
        ecs_err("cannot write perf report '%s'", run->report);
        goto done;
    }

    if (fputs(json, f) >= 0) {
        result = 0;
    }

    fclose(f);
done:
    ecs_os_free(scene);
    ecs_os_free(json);

    return result;
}
