/**
 * @file app/benchmark.c
 * @brief The benchmark of App.benchmark: times the frames of a settled scene and the threads of
 * the load panel, prints their percentiles and stops the world.
 *
 * A frame is settled when no aperiodic work ran in it: no task, job, pipeline or shader compile,
 * bake, texture graph miss, terrain tile, impostor bake, capture or resize. Such work during the
 * measurement throws the frames away, so the benchmark settles and measures again.
 *
 * The GPU timing of the backend (box_render_gpu_timing) is on from the first frame, so the
 * timestamps cost the same while settling, and reports the GPU time of the measured frames.
 */

#include "app.h"
#include "benchmark.h"
#include "../jobs/jobs.h"
#include "../render/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Settled frames in a row before the measurement starts */
#define BOX_BENCHMARK_SETTLE (60)

/** Fewest frames before the measurement starts, which let the CPU and GPU clocks ramp up */
#define BOX_BENCHMARK_WARMUP (120)

/** Wall seconds the benchmark may spend settling in total before it fails */
#define BOX_BENCHMARK_TIMEOUT (300.0)

/** Frames a bake may leave the same probes before it no longer keeps the scene from settling */
#define BOX_BENCHMARK_BAKE_STALL (300)

/** Series of a benchmark: the frame interval and the five rows of the load panel */
#define BOX_BENCHMARK_SERIES (6)

/** Percentiles the report prints */
#define BOX_BENCHMARK_PERCENTILES (5)

/** Width of the label column of the report */
#define BOX_BENCHMARK_LABEL_WIDTH (34)

/** Frames the benchmark waits after the measurement for the GPU timings of its frames */
#define BOX_BENCHMARK_DRAIN (60)

/** GPU timings taken from the backend at once */
#define BOX_BENCHMARK_TIMINGS (32)

/** Counters of aperiodic work, which a settled frame leaves unchanged */
typedef enum box_benchmark_counter_t {
    BoxBenchmarkPipelines = 0,  /**< Pipelines the GPU created */
    BoxBenchmarkCompiles,       /**< Shader modules the GPU compiled */
    BoxBenchmarkGraphMisses,    /**< Texture graph stages computed into a new cached value */
    BoxBenchmarkGraphEvictions, /**< Texture graph values evicted from the cache */
    BoxBenchmarkAtmosphere,     /**< Builds of the LUTs of an atmosphere */
    BoxBenchmarkBlends,         /**< Builds of the arrays of TextureBlend layers */
    BoxBenchmarkClusters,       /**< Resizes of the cluster buffer of a view */
    BoxBenchmarkProbes,         /**< Probes captured */
    BoxBenchmarkDeviceLosses,   /**< GPU devices recreated */
    BoxBenchmarkTerrain,        /**< Terrain tiles made and dropped */
    BoxBenchmarkImpostors,      /**< Impostor bakes completed */
    BoxBenchmarkSurfaces,       /**< Render sizes of the surfaces */
    BoxBenchmarkCounters        /**< Number of counters */
} box_benchmark_counter_t;

/** What a change of each counter of box_benchmark_counter_t reports */
static const char *box_benchmark_counter_names[BoxBenchmarkCounters] = {
    "creating pipelines",
    "compiling shaders",
    "computing texture graphs",
    "evicting texture graphs",
    "building atmosphere",
    "building texture blends",
    "resizing light clusters",
    "capturing probes",
    "recreating the GPU device",
    "generating terrain",
    "baking impostors",
    "resizing surfaces"
};

/** Labels of the series of the report, the rows of the load panel after the frame */
static const char *box_benchmark_series_names[BOX_BENCHMARK_SERIES] = {
    "Frame",
    "Main thread",
    "Render CPU",
    "Render GPU wait",
    "Audio",
    "Jobs, busiest"
};

/** Percentiles of the report */
static const double box_benchmark_percentiles[BOX_BENCHMARK_PERCENTILES] = {
    50, 75, 90, 95, 99
};

/** Progress of a benchmark */
typedef enum box_benchmark_phase_t {
    BoxBenchmarkSettle = 0,     /**< Waiting for BOX_BENCHMARK_SETTLE settled frames */
    BoxBenchmarkMeasure,        /**< Recording the frames */
    BoxBenchmarkDrain,          /**< Waiting for the GPU timings of the measured frames */
    BoxBenchmarkDone,           /**< Reported */
    BoxBenchmarkFailed          /**< Did not settle in time */
} box_benchmark_phase_t;

/** State of the benchmark, the context of its system */
typedef struct box_benchmark_t {
    box_benchmark_phase_t phase;    /**< Progress */
    int32_t frames;                 /**< Frames since the run started */
    int32_t settled;                /**< Settled frames in a row */
    int32_t settled_at;             /**< Frame the last measurement started at */
    int32_t restarts;               /**< Measurements thrown away by aperiodic work */
    const char *busy;               /**< Last aperiodic work seen, NULL for none */
    bool counted;                   /**< counters holds the values of the previous frame */
    int64_t counters[BoxBenchmarkCounters]; /**< Counters at the previous frame */
    int32_t bake_left;              /**< Probes the bakes had left when they last progressed */
    int32_t bake_stalled;           /**< Frames since the bakes last progressed */
    double settle_start;            /**< Wall seconds the current settling started, 0 none */
    double settle_time;             /**< Wall seconds of the earlier settling */
    ecs_vec_t series[BOX_BENCHMARK_SERIES]; /**< Milliseconds per measured frame, of double */
    bool timing;                    /**< The GPU timing of the backend is on */
    ecs_vec_t gpu;                  /**< box_render_gpu_timing_t of the measured frames */
    int64_t gpu_first;              /**< Frame of the first measured packet */
    int64_t gpu_last;               /**< Frame of the last measured packet */
    int64_t gpu_seen;               /**< Frame of the newest GPU timing, -1 for none */
    int32_t drained;                /**< Frames waited for the GPU timings */
} box_benchmark_t;

/** Totals of a pass label over the GPU timings of the measured frames */
typedef struct box_benchmark_label_t {
    const char *label;              /**< The label */
    double ms;                      /**< Milliseconds of its passes over every frame */
} box_benchmark_label_t;

/** Free the state of the benchmark.
 *
 * @param ctx The state
 */
static void box_benchmark_free(
    void *ctx)
{
    box_benchmark_t *b = ctx;
    int32_t i;
    for (i = 0; i < BOX_BENCHMARK_SERIES; i ++) {
        ecs_vec_fini_t(NULL, &b->series[i], double);
    }

    ecs_vec_fini_t(NULL, &b->gpu, box_render_gpu_timing_t);
    ecs_os_free(b);
}

/** Find a task in progress other than a bake, which box_benchmark_baking follows.
 *
 * @param world The world
 * @return The name of the task, NULL when none is in progress
 */
static const char* box_benchmark_task(
    const ecs_world_t *world)
{
    const InProgressTask *tasks = ecs_id(InProgressTask)
        ? ecs_singleton_get(world, InProgressTask) : NULL;
    const char *result = NULL;
    int32_t i;
    for (i = 0; tasks && i < tasks->count && i < BOX_TASK_MAX && !result; i ++) {
        const char *name = tasks->tasks[i].name;
        if (name && strcmp(name, BOX_TASK_PROBES) && strcmp(name, BOX_TASK_REFLECTIONS)) {
            result = name;
        }
    }

    return result;
}

/** Check whether a light or reflection bake that can progress is still baking. A bake that
 * leaves the same probes for BOX_BENCHMARK_BAKE_STALL frames is given up on, with a warning.
 *
 * @param world The world
 * @param b The state
 * @return Whether a bake is progressing
 */
static bool box_benchmark_baking(
    ecs_world_t *world,
    box_benchmark_t *b)
{
    int32_t left = box_render_bake_pending(world) + box_render_reflect_pending(world);
    bool result = false;
    if (!left) {
        b->bake_left = 0;
        b->bake_stalled = 0;
    } else if (left != b->bake_left) {
        b->bake_left = left;
        b->bake_stalled = 0;
        result = true;
    } else if (b->bake_stalled < BOX_BENCHMARK_BAKE_STALL) {
        b->bake_stalled ++;
        result = b->bake_stalled < BOX_BENCHMARK_BAKE_STALL;
        if (!result) {
            ecs_warn("benchmark: the bakes left %d probes for %d frames, measuring without them",
                left, BOX_BENCHMARK_BAKE_STALL);
        }
    }

    return result;
}

/** Read the terrain counters: the tiles made and dropped and the tiles still pending.
 *
 * @param world The world
 * @param counter Receives the tiles made and dropped by every terrain
 * @return The pending tiles and query tiles of every terrain
 */
static int32_t box_benchmark_terrain(
    const ecs_world_t *world,
    int64_t *counter)
{
    int32_t result = 0, i;
    *counter = 0;
    if (ecs_id(TerrainStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(TerrainStatus));
        while (ecs_each_next(&it)) {
            const TerrainStatus *s = ecs_field(&it, TerrainStatus, 0);
            for (i = 0; i < it.count; i ++) {
                *counter += (int64_t)s[i].created + s[i].dropped;
                result += s[i].pending;
            }
        }
    }

    if (ecs_id(TerrainQueryStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(TerrainQueryStatus));
        while (ecs_each_next(&it)) {
            const TerrainQueryStatus *s = ecs_field(&it, TerrainQueryStatus, 0);
            for (i = 0; i < it.count; i ++) {
                result += s[i].pending;
            }
        }
    }

    return result;
}

/** Read the impostor counters: the bakes completed and the bakes in progress.
 *
 * @param world The world
 * @param counter Receives the bakes completed by every impostor
 * @return The impostors that bake
 */
static int32_t box_benchmark_impostors(
    const ecs_world_t *world,
    int64_t *counter)
{
    int32_t result = 0, i;
    *counter = 0;
    if (ecs_id(ImpostorStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(ImpostorStatus));
        while (ecs_each_next(&it)) {
            const ImpostorStatus *s = ecs_field(&it, ImpostorStatus, 0);
            for (i = 0; i < it.count; i ++) {
                *counter += s[i].revision;
                result += s[i].baking;
            }
        }
    }

    return result;
}

/** Combine the render sizes of every surface into one value that changes with any of them.
 *
 * @param world The world
 * @return The combined sizes
 */
static int64_t box_benchmark_surfaces(
    const ecs_world_t *world)
{
    int64_t result = 0;
    int32_t i;
    if (ecs_id(SurfaceStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(SurfaceStatus));
        while (ecs_each_next(&it)) {
            const SurfaceStatus *s = ecs_field(&it, SurfaceStatus, 0);
            for (i = 0; i < it.count; i ++) {
                result = result * 31 + ((int64_t)s[i].render_width << 16) + s[i].render_height;
            }
        }
    }

    return result;
}

/** Read the counters of aperiodic work.
 *
 * @param world The world
 * @param counters Receives the counters, BoxBenchmarkCounters of them
 * @param pending Receives the terrain tiles and impostor bakes in progress
 */
static void box_benchmark_count(
    ecs_world_t *world,
    int64_t *counters,
    int32_t *pending)
{
    box_render_gpu_stats_t stats;
    const RenderStatus *render = ecs_id(RenderStatus)
        ? ecs_singleton_get(world, RenderStatus) : NULL;
    ecs_os_memset(counters, 0, BoxBenchmarkCounters * ECS_SIZEOF(int64_t));
    if (render && box_render_gpu_stats(world, &stats)) {
        counters[BoxBenchmarkPipelines] = stats.pipelines + stats.pipelines_building;
        counters[BoxBenchmarkCompiles] = stats.compiles;
        counters[BoxBenchmarkGraphMisses] = stats.texture_graph_misses;
        counters[BoxBenchmarkGraphEvictions] = stats.texture_graph_evictions;
        counters[BoxBenchmarkAtmosphere] = stats.atmosphere_builds;
        counters[BoxBenchmarkBlends] = stats.blend_builds;
        counters[BoxBenchmarkClusters] = stats.cluster_resizes;
        counters[BoxBenchmarkProbes] = stats.probes;
        *pending = stats.pipelines_building;
    }

    counters[BoxBenchmarkDeviceLosses] = render ? render->device_losses : 0;
    *pending += box_benchmark_terrain(world, &counters[BoxBenchmarkTerrain]);
    *pending += box_benchmark_impostors(world, &counters[BoxBenchmarkImpostors]);
    counters[BoxBenchmarkSurfaces] = box_benchmark_surfaces(world);
}

/** Find the aperiodic work that ran since the previous frame.
 *
 * @param world The world
 * @param b The state, whose counters receive those of this frame
 * @return What ran, NULL when the frame is settled
 */
static const char* box_benchmark_busy(
    ecs_world_t *world,
    box_benchmark_t *b)
{
    int64_t counters[BoxBenchmarkCounters];
    int32_t pending = 0, i;
    const char *result = box_benchmark_task(world);
    bool render = ecs_id(RenderStatus) != 0;
    box_benchmark_count(world, counters, &pending);
    for (i = 0; i < BoxBenchmarkCounters && !result && b->counted; i ++) {
        if (counters[i] != b->counters[i]) {
            result = box_benchmark_counter_names[i];
        }
    }

    if (render && box_benchmark_baking(world, b) && !result) {
        result = "baking probes";
    }

    if (!result && (box_jobs_pending(world) || box_jobs_running(world))) {
        result = "running jobs";
    }

    if (!result && render && box_render_captures_pending(world)) {
        result = "capturing surfaces";
    }

    if (!result && (pending || !b->counted)) {
        result = pending ? "generating terrain, impostors or pipelines" : "starting";
    }

    ecs_os_memcpy(b->counters, counters, BoxBenchmarkCounters * ECS_SIZEOF(int64_t));
    b->counted = true;

    return result;
}

/** Add the samples of the previous frame to the series: its interval and the time every thread
 * of the load panel worked in it.
 *
 * @param world The world
 * @param b The state
 */
static void box_benchmark_sample(
    const ecs_world_t *world,
    box_benchmark_t *b)
{
    const FrameTiming *timing = ecs_singleton_get(world, FrameTiming);
    const LoadSamples *load = ecs_id(LoadSamples)
        ? ecs_singleton_get(world, LoadSamples) : NULL;
    double values[BOX_BENCHMARK_SERIES] = {
        timing ? timing->interval_ms : 0,
        load ? box_frame_sample(&load->main, 0) : 0,
        load ? box_frame_sample(&load->render, 0) : 0,
        load ? box_frame_sample(&load->gpu, 0) : 0,
        load ? box_frame_sample(&load->audio, 0) : 0,
        load ? box_frame_sample(&load->jobs, 0) : 0
    };
    int64_t frame = (int64_t)ecs_get_world_info(world)->frame_count_total - 1;
    int32_t i;
    for (i = 0; i < BOX_BENCHMARK_SERIES; i ++) {
        ecs_vec_append_t(NULL, &b->series[i], double)[0] = values[i];
    }

    b->gpu_first = ecs_vec_count(&b->series[0]) == 1 ? frame : b->gpu_first;
    b->gpu_last = frame;
}

/** Turn on the GPU timing of the backend until it is on, and take the GPU timings that
 * arrived: those of the measured frames are kept while measuring or draining.
 *
 * @param world The world
 * @param b The state
 */
static void box_benchmark_gpu(
    ecs_world_t *world,
    box_benchmark_t *b)
{
    box_render_gpu_timing_t timings[BOX_BENCHMARK_TIMINGS];
    bool keep = b->phase == BoxBenchmarkMeasure || b->phase == BoxBenchmarkDrain;
    int32_t i, count = BOX_BENCHMARK_TIMINGS;
    if (!ecs_id(RenderStatus)) {
        goto done;
    }

    b->timing = b->timing || box_render_gpu_timing(world, true);
    while (count == BOX_BENCHMARK_TIMINGS) {
        count = box_render_gpu_timings(world, timings, BOX_BENCHMARK_TIMINGS);
        for (i = 0; i < count; i ++) {
            b->gpu_seen = timings[i].frame > b->gpu_seen ? timings[i].frame : b->gpu_seen;
            if (keep && ecs_vec_count(&b->series[0]) && timings[i].frame >= b->gpu_first) {
                ecs_vec_append_t(NULL, &b->gpu, box_render_gpu_timing_t)[0] = timings[i];
            }
        }
    }

done:
    return;
}

/** Check whether a GPU timing is of a measured frame.
 *
 * @param b The state
 * @param t The timing
 * @return Whether the timing is of a frame between the first and the last measured frame
 */
static bool box_benchmark_measured(
    const box_benchmark_t *b,
    const box_render_gpu_timing_t *t)
{
    return t->frame >= b->gpu_first && t->frame <= b->gpu_last;
}

/** Compare two samples for qsort.
 *
 * @param a The first sample
 * @param b The second sample
 * @return Negative when a is smaller, positive when larger, 0 when equal
 */
static int box_benchmark_cmp(
    const void *a,
    const void *b)
{
    double x = *(const double*)a, y = *(const double*)b;

    return (x > y) - (x < y);
}

/** Get a percentile of sorted samples by the nearest rank.
 *
 * @param sorted The samples, smallest first
 * @param count Number of samples, at least 1
 * @param percentile The percentile, 0 to 100
 * @return The sample at the percentile
 */
static double box_benchmark_percentile(
    const double *sorted,
    int32_t count,
    double percentile)
{
    int32_t rank = (int32_t)ceil(percentile / 100.0 * (double)count) - 1;
    rank = rank < 0 ? 0 : rank;
    rank = rank >= count ? count - 1 : rank;

    return sorted[rank];
}

/** Get the sum of a series.
 *
 * @param series The series, of double
 * @return The sum of its samples
 */
static double box_benchmark_sum(
    const ecs_vec_t *series)
{
    const double *values = ecs_vec_first_t(series, double);
    int32_t i, count = ecs_vec_count(series);
    double result = 0;
    for (i = 0; i < count; i ++) {
        result += values[i];
    }

    return result;
}

/** Append a row of the report: the mean, percentiles and largest sample of a series, and its
 * share of the frame time for a row of the load panel.
 *
 * @param buf The report
 * @param label The label of the row
 * @param series The series, of double
 * @param frame_ms The sum of the frame intervals, 0 for the row of the frames themselves
 */
static void box_benchmark_row(
    ecs_strbuf_t *buf,
    const char *label,
    const ecs_vec_t *series,
    double frame_ms)
{
    int32_t i, count = ecs_vec_count(series);
    double *sorted = ecs_os_malloc_n(double, count);
    double sum = box_benchmark_sum(series);
    ecs_os_memcpy_n(sorted, ecs_vec_first_t(series, double), double, count);
    qsort(sorted, (size_t)count, sizeof(double), box_benchmark_cmp);
    ecs_strbuf_append(buf, "%-*.*s %8.3f", BOX_BENCHMARK_LABEL_WIDTH,
        BOX_BENCHMARK_LABEL_WIDTH, label, sum / (double)count);
    for (i = 0; i < BOX_BENCHMARK_PERCENTILES; i ++) {
        ecs_strbuf_append(buf, " %8.3f",
            box_benchmark_percentile(sorted, count, box_benchmark_percentiles[i]));
    }

    ecs_strbuf_append(buf, " %8.3f", sorted[count - 1]);
    if (frame_ms > 0) {
        ecs_strbuf_append(buf, " %6.1f%%", 100.0 * sum / frame_ms);
    }

    ecs_strbuf_appendch(buf, '\n');
    ecs_os_free(sorted);
}

/** Append the surface the benchmark rendered to: the first one that is not a texture.
 *
 * @param world The world
 * @param buf The report
 */
static void box_benchmark_surface(
    const ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const SurfaceStatus *found = NULL;
    int32_t i;
    if (ecs_id(Surface) && ecs_id(SurfaceStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(Surface));
        while (!found && ecs_each_next(&it)) {
            const Surface *s = ecs_field(&it, Surface, 0);
            for (i = 0; i < it.count && !found; i ++) {
                found = s[i].texture ? NULL : ecs_get(world, it.entities[i], SurfaceStatus);
            }
        }

        if (found) {
            ecs_iter_fini(&it);
        }
    }

    if (found) {
        ecs_strbuf_append(buf, "%s %dx%d, rendered at %dx%d",
            found->window ? "window" : "offscreen", found->width, found->height,
            found->render_width, found->render_height);
    } else {
        ecs_strbuf_appendstr(buf, "no surface");
    }
}

/** Compare two pass labels by their milliseconds for qsort, the largest first.
 *
 * @param a The first label
 * @param b The second label
 * @return Negative when a took longer, positive when b did, 0 when they took as long
 */
static int box_benchmark_label_cmp(
    const void *a,
    const void *b)
{
    double x = ((const box_benchmark_label_t*)a)->ms;
    double y = ((const box_benchmark_label_t*)b)->ms;

    return (x < y) - (x > y);
}

/** Total the milliseconds of every pass label over the GPU timings of the measured frames.
 *
 * @param b The state
 * @param labels Receives the labels, the largest first, BOX_RENDER_GPU_TIMING_LABELS at most
 * @return The labels
 */
static int32_t box_benchmark_labels(
    const box_benchmark_t *b,
    box_benchmark_label_t *labels)
{
    const box_render_gpu_timing_t *t = ecs_vec_first_t(&b->gpu, box_render_gpu_timing_t);
    int32_t i, j, l, result = 0;
    for (i = 0; i < ecs_vec_count(&b->gpu); i ++) {
        for (j = 0; box_benchmark_measured(b, &t[i]) && j < t[i].labels; j ++) {
            for (l = 0; l < result && strcmp(labels[l].label, t[i].label[j]); l ++) { }
            if (l == result && result < BOX_RENDER_GPU_TIMING_LABELS) {
                labels[result ++] = (box_benchmark_label_t){ t[i].label[j], 0 };
            }

            labels[l].ms += l < result ? t[i].label_ms[j] : 0;
        }
    }

    qsort(labels, (size_t)result, sizeof(box_benchmark_label_t), box_benchmark_label_cmp);

    return result;
}

/** Fill a series with a value of every GPU timing of the measured frames: its busy time, its
 * span, or the milliseconds of the passes of a label.
 *
 * @param b The state
 * @param label The label of the passes, NULL for the busy time
 * @param span Fill the span instead of the busy time, when label is NULL
 * @param out Receives the values, of double
 */
static void box_benchmark_gpu_series(
    const box_benchmark_t *b,
    const char *label,
    bool span,
    ecs_vec_t *out)
{
    const box_render_gpu_timing_t *t = ecs_vec_first_t(&b->gpu, box_render_gpu_timing_t);
    int32_t i, j;
    ecs_vec_clear(out);
    for (i = 0; i < ecs_vec_count(&b->gpu); i ++) {
        double ms = span ? t[i].span_ms : t[i].busy_ms;
        if (label) {
            ms = 0;
            for (j = 0; j < t[i].labels; j ++) {
                ms += strcmp(t[i].label[j], label) ? 0 : t[i].label_ms[j];
            }
        }

        if (box_benchmark_measured(b, &t[i])) {
            ecs_vec_append_t(NULL, out, double)[0] = ms;
        }
    }
}

/** Append the GPU time of the measured frames to the report: the busy time and span of the
 * frames, as a share of the frame time, and the time of every pass label, as a share of busy.
 *
 * @param buf The report
 * @param b The state
 * @param frame_mean The mean frame interval in milliseconds
 */
static void box_benchmark_report_gpu(
    ecs_strbuf_t *buf,
    const box_benchmark_t *b,
    double frame_mean)
{
    box_benchmark_label_t labels[BOX_RENDER_GPU_TIMING_LABELS];
    ecs_vec_t series = {0};
    int32_t l, count = 0;
    double busy = 0;
    char label[64];
    box_benchmark_gpu_series(b, NULL, false, &series);
    count = ecs_vec_count(&series);
    busy = box_benchmark_sum(&series);
    if (!b->timing || !count) {
        ecs_strbuf_append(buf, "\nGPU time not measured: %s\n", b->timing
            ? "no GPU timings arrived" : "the device has no timestamp queries");
        goto done;
    }

    ecs_strbuf_append(buf, "\nGPU, %d of %d frames timed\n", count,
        ecs_vec_count(&b->series[0]));
    box_benchmark_row(buf, "GPU busy", &series, frame_mean * (double)count);
    box_benchmark_gpu_series(b, NULL, true, &series);
    box_benchmark_row(buf, "GPU span", &series, frame_mean * (double)count);
    ecs_strbuf_appendstr(buf, "\nGPU passes, share of GPU busy\n");
    count = box_benchmark_labels(b, labels);
    for (l = 0; l < count; l ++) {
        ecs_os_snprintf(label, sizeof(label), "  %s", labels[l].label);
        box_benchmark_gpu_series(b, labels[l].label, false, &series);
        box_benchmark_row(buf, label, &series, busy);
    }

done:
    ecs_vec_fini_t(NULL, &series, double);
}

/** Print the report of a benchmark to stdout.
 *
 * @param world The world
 * @param b The state
 */
static void box_benchmark_report(
    ecs_world_t *world,
    box_benchmark_t *b)
{
    const App *app = ecs_singleton_get(world, App);
    const RenderStatus *render = ecs_id(RenderStatus)
        ? ecs_singleton_get(world, RenderStatus) : NULL;
    double busy[BOX_JOBS_MAX_WORKERS];
    int32_t workers = box_jobs_load(world, busy);
    int32_t i, count = ecs_vec_count(&b->series[0]);
    double frame_ms = box_benchmark_sum(&b->series[0]);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_append(&buf, "benchmark: %s, %s, ",
        app->scene && app->scene[0] ? app->scene : "no scene",
        app->headless ? "headless" : "windowed");
    box_benchmark_surface(world, &buf);
    ecs_strbuf_append(&buf, ", vsync %s\n", render && render->vsync ? "on" : "off");
    ecs_strbuf_append(&buf, "settled after %d frames (%.1f s), measurements restarted: %d\n",
        b->settled_at, b->settle_time, b->restarts);
    ecs_strbuf_append(&buf, "%d frames in %.3f s: %.2f fps\n\n", count, frame_ms / 1000.0,
        frame_ms > 0 ? 1000.0 * (double)count / frame_ms : 0);
    ecs_strbuf_append(&buf, "%-*s %8s", BOX_BENCHMARK_LABEL_WIDTH, "milliseconds", "mean");
    for (i = 0; i < BOX_BENCHMARK_PERCENTILES; i ++) {
        ecs_strbuf_append(&buf, "      p%02.0f", box_benchmark_percentiles[i]);
    }

    ecs_strbuf_append(&buf, " %8s %7s\n", "max", "share");
    for (i = 0; i < BOX_BENCHMARK_SERIES; i ++) {
        char label[64];
        if (i == BOX_BENCHMARK_SERIES - 1) {
            ecs_os_snprintf(label, sizeof(label), "%s of %d",
                box_benchmark_series_names[i], workers);
        } else {
            ecs_os_snprintf(label, sizeof(label), "%s", box_benchmark_series_names[i]);
        }

        box_benchmark_row(&buf, label, &b->series[i], i ? frame_ms : 0);
    }

    box_benchmark_report_gpu(&buf, b, count ? frame_ms / (double)count : 0);
    char *report = ecs_strbuf_get(&buf);
    printf("%s", report);
    fflush(stdout);
    ecs_os_free(report);
}

/** Start settling: the next frames must be settled before the measurement starts.
 *
 * @param b The state
 * @param now Wall seconds
 */
static void box_benchmark_settle(
    box_benchmark_t *b,
    double now)
{
    int32_t i;
    b->phase = BoxBenchmarkSettle;
    b->settled = 0;
    b->settle_start = now;
    for (i = 0; i < BOX_BENCHMARK_SERIES; i ++) {
        ecs_vec_clear(&b->series[i]);
    }

    ecs_vec_clear(&b->gpu);
}

/** Advance a settling benchmark: start the measurement once enough frames in a row settled, or
 * fail when settling took longer than BOX_BENCHMARK_TIMEOUT.
 *
 * @param world The world
 * @param b The state
 * @param busy The aperiodic work of the frame, NULL when it is settled
 * @param now Wall seconds
 */
static void box_benchmark_settling(
    ecs_world_t *world,
    box_benchmark_t *b,
    const char *busy,
    double now)
{
    b->settled = busy ? 0 : b->settled + 1;
    b->busy = busy ? busy : b->busy;
    if (b->settled >= BOX_BENCHMARK_SETTLE && b->frames >= BOX_BENCHMARK_WARMUP) {
        b->phase = BoxBenchmarkMeasure;
        b->settled_at = b->frames;
        b->settle_time += now - b->settle_start;
        ecs_trace("benchmark: settled after %d frames, measuring %d frames",
            b->frames, BOX_APP_BENCHMARK_FRAMES);
    } else if (b->settle_time + now - b->settle_start > BOX_BENCHMARK_TIMEOUT) {
        ecs_err("benchmark: the scene did not settle within %.0f s, last busy %s",
            BOX_BENCHMARK_TIMEOUT, b->busy ? b->busy : "with nothing");
        b->phase = BoxBenchmarkFailed;
        ecs_quit(world);
    }
}

/** Settle, measure and report the benchmark, at the start of every frame after the frame
 * timing and the load samples of the previous frame were written.
 *
 * @param it The iterator of the system
 */
static void BoxAppBenchmark(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_benchmark_t *b = it->ctx;
    const App *app = ecs_singleton_get(world, App);
    double now = box_app_wall_time();
    const char *busy = NULL;
    if (!app || !app->benchmark || b->phase >= BoxBenchmarkDone) {
        goto done;
    }

    if (!b->frames) {
        box_benchmark_settle(b, now);
    }

    b->frames ++;
    busy = box_benchmark_busy(world, b);
    box_benchmark_gpu(world, b);
    if (b->phase == BoxBenchmarkDrain) {
        b->drained ++;
        if (b->gpu_seen >= b->gpu_last || !b->timing || b->drained >= BOX_BENCHMARK_DRAIN) {
            box_benchmark_report(world, b);
            b->phase = BoxBenchmarkDone;
            ecs_quit(world);
        }
    } else if (b->phase == BoxBenchmarkMeasure && busy) {
        ecs_trace("benchmark: %s during the measurement, settling again", busy);
        b->restarts ++;
        b->busy = busy;
        box_benchmark_settle(b, now);
    } else if (b->phase == BoxBenchmarkMeasure) {
        box_benchmark_sample(world, b);
        if (ecs_vec_count(&b->series[0]) >= BOX_APP_BENCHMARK_FRAMES) {
            b->phase = BoxBenchmarkDrain;
            b->drained = 0;
        }
    } else {
        box_benchmark_settling(world, b, busy, now);
    }

done:
    return;
}

int box_app_benchmark_end(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    ecs_entity_t e = ecs_lookup(world, "box.app.Benchmark");
    const ecs_system_t *system = e ? ecs_system_get(world, e) : NULL;
    const box_benchmark_t *b = system ? system->ctx : NULL;
    int result = 0;
    if (!app || !app->benchmark || !b || b->phase == BoxBenchmarkDone) {
        goto done;
    }

    if (b->phase != BoxBenchmarkFailed) {
        ecs_err("benchmark: the run ended after %d of %d measured frames",
            ecs_vec_count(&b->series[0]), BOX_APP_BENCHMARK_FRAMES);
    }

    result = -1;
done:
    return result;
}

void box_app_benchmark_init(
    ecs_world_t *world)
{
    box_benchmark_t *b = ecs_os_calloc_t(box_benchmark_t);
    b->gpu_seen = -1;
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Benchmark" }),
        .phase = EcsOnLoad,
        .run = BoxAppBenchmark,
        .ctx = b,
        .ctx_free = box_benchmark_free,
        .immediate = true
    });
}
