/**
 * @file fps/load.c
 * @brief The samples of the load panel: at the start of every frame the time the main thread,
 * the renderer, the audio thread and the job workers worked since the frame before. LoadSamples
 * is notified while the load panel is open, every interval of LoadPanel.
 *
 * Every thread has a clock that is busy while it works and waits while it blocks. The main thread
 * gets its clock from box.fps: busy from the start of the frame until the LoadEnd phase, after
 * every other phase, so the frame pacing between two frames counts for nothing.
 */

#include "load.h"
#include "../app/load.h"
#include "../audio/output.h"
#include "../jobs/jobs.h"
#include "../render/render.h"

/** Series of LoadSamples whose totals are read from a clock */
#define BOX_FPS_LOAD_CLOCKS (4)

/** State of the samples of a world, owned by box.fps */
typedef struct box_fps_load_t {
    box_load_clock_t main;      /**< Clock of the main thread */
    box_load_bound_t prev;      /**< Clock the main thread had before the frame bound main */
    bool bound;                 /**< main is bound to the main thread */
    bool sampled;               /**< The totals were read at the start of a frame before */
    double notified;            /**< Time LoadSamples was last notified, 0 before */
    double last[BOX_FPS_LOAD_CLOCKS]; /**< Seconds of the main thread, the renderer working, the
                                 *   renderer waiting and the audio thread at the last read */
    double jobs[BOX_JOBS_MAX_WORKERS]; /**< Seconds of every job worker at the last read */
} box_fps_load_t;

/** Get the milliseconds a total grew since the last read and remember the total.
 *
 * @param now The total in seconds
 * @param last The total of the last read, receives now
 * @return Milliseconds since the last read, 0 when the total restarted lower
 */
static double box_fps_load_ms(
    double now,
    double *last)
{
    double result = now > *last ? (now - *last) * 1000.0 : 0;
    *last = now;

    return result;
}

/** Read the seconds the audio thread rendered.
 *
 * @param world The world
 * @return The seconds, 0 without an output that measures them
 */
static double box_fps_load_audio(
    const ecs_world_t *world)
{
    box_audio_transport_t *t = box_audio_output_transport(world);
    box_audio_transport_stats_t stats = {0};
    if (t && t->stats) {
        t->stats(t, &stats);
    }

    return stats.busy;
}

/** Get the milliseconds the busiest job worker worked since the last read.
 *
 * @param world The world
 * @param load The state of the samples
 * @return The milliseconds, 0 without workers
 */
static double box_fps_load_jobs(
    ecs_world_t *world,
    box_fps_load_t *load)
{
    double busy[BOX_JOBS_MAX_WORKERS], result = 0;
    int32_t i, count = box_jobs_load(world, busy);
    for (i = 0; i < count; i ++) {
        double ms = box_fps_load_ms(busy[i], &load->jobs[i]);
        result = ms > result ? ms : result;
    }

    return result;
}

/** Add a sample to a series of LoadSamples unless it holds.
 *
 * @param samples The series
 * @param ms The sample in milliseconds
 * @return Whether the sample was added
 */
static bool box_fps_load_add(
    FrameSamples *samples,
    double ms)
{
    bool result = !samples->hold;
    if (result) {
        box_frame_samples_add(samples, ms);
    }

    return result;
}

/** Whether LoadSamples is notified now: while the load panel is open, every sample when its
 * interval is 0 or less, else when the interval passed since the last time.
 *
 * @param world The world
 * @param load The state of the samples
 * @return Whether the samples that were added are notified
 */
static bool box_fps_load_due(
    const ecs_world_t *world,
    box_fps_load_t *load)
{
    const LoadPanel *panel = ecs_singleton_get(world, LoadPanel);
    bool result = panel && panel->open;
    if (result && panel->interval > 0) {
        double now = box_fps_now();
        result = now - load->notified >= (double)panel->interval;
        if (result) {
            load->notified = now;
        }
    }

    return result;
}

/** Sample the threads at the start of a frame: what they worked since the start of the frame
 * before goes to LoadSamples, notified when a series took it and box_fps_load_due. Then the main
 * thread works.
 *
 * The system is immediate, so LoadSamples is set while the world is not readonly and the bars
 * of the load panel follow it at once.
 *
 * @param it The iterator of the system
 */
static void BoxFpsLoadSample(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_fps_load_t *load = it->ctx;
    LoadSamples *samples;
    double busy, wait, render, gpu, ms[BOX_LOAD_ROWS];
    bool added = false;
    box_load_clock_read(&load->main, &busy, &wait);
    box_render_load(world, &render, &gpu);
    ms[0] = box_fps_load_ms(busy, &load->last[0]);
    ms[1] = box_fps_load_ms(render, &load->last[1]);
    ms[2] = box_fps_load_ms(gpu, &load->last[2]);
    ms[3] = box_fps_load_ms(box_fps_load_audio(world), &load->last[3]);
    ms[4] = box_fps_load_jobs(world, load);
    samples = load->sampled ? ecs_singleton_get_mut(world, LoadSamples) : NULL;
    if (samples) {
        added |= box_fps_load_add(&samples->main, ms[0]);
        added |= box_fps_load_add(&samples->render, ms[1]);
        added |= box_fps_load_add(&samples->gpu, ms[2]);
        added |= box_fps_load_add(&samples->audio, ms[3]);
        added |= box_fps_load_add(&samples->jobs, ms[4]);
    }

    if (added && box_fps_load_due(world, load)) {
        ecs_singleton_modified(world, LoadSamples);
    }

    load->sampled = true;
    if (!load->bound) {
        load->prev = box_load_bind(&load->main);
        load->bound = true;
    }

    box_load_clock_enter(&load->main, BOX_LOAD_BUSY);
}

/** End the frame of the main thread: its clock stops until the next frame starts.
 *
 * @param it The iterator of the system
 */
static void BoxFpsLoadIdle(
    ecs_iter_t *it)
{
    box_fps_load_t *load = it->ctx;
    box_load_clock_enter(&load->main, BOX_LOAD_IDLE);
    if (load->bound) {
        box_load_restore(load->prev);
        load->bound = false;
    }
}

/** Free the state of the samples when the world is deleted.
 *
 * @param world The world
 * @param ctx The state
 */
static void box_fps_load_free(
    ecs_world_t *world,
    void *ctx)
{
    box_fps_load_t *load = ctx;
    (void)world;
    if (load->bound) {
        box_load_restore(load->prev);
    }

    box_load_clock_fini(&load->main);
    ecs_os_free(load);
}

/** Summarize the newest samples of a series.
 *
 * @param samples The series
 * @param frames Samples of the last second
 * @param frame_ms Sums of the newest frame samples, frame_ms[n] of the n newest
 * @param out Receives the summary
 */
static void box_load_stat(
    const FrameSamples *samples,
    int32_t frames,
    const double *frame_ms,
    LoadStat *out)
{
    int32_t age, count = samples->count < frames ? samples->count : frames;
    double sum = 0, worst = 0;
    for (age = 0; age < count; age ++) {
        double ms = box_frame_sample(samples, age);
        sum += ms;
        worst = ms > worst ? ms : worst;
    }

    out->mean_ms = count > 0 ? sum / (double)count : 0;
    out->worst_ms = worst;
    out->share = count > 0 && frame_ms[count] > 0 ? 100.0 * sum / frame_ms[count] : 0;
}

void box_load_summarize(
    const FrameSamples *frames,
    const LoadSamples *samples,
    int32_t workers,
    LoadSummary *out)
{
    double frame_ms[BOX_FRAME_WINDOW + 1] = {0};
    int32_t age, n = 0;
    ecs_os_memset_t(out, 0, LoadSummary);
    for (age = 0; frames && age < BOX_FRAME_WINDOW && frame_ms[n] < BOX_FPS_SUMMARY_MS; age ++) {
        double ms = box_frame_sample(frames, age);
        if (ms <= 0) {
            break;
        }

        frame_ms[n + 1] = frame_ms[n] + ms;
        n ++;
    }

    out->frames = n;
    out->workers = workers;
    if (samples) {
        box_load_stat(&samples->main, n, frame_ms, &out->main);
        box_load_stat(&samples->render, n, frame_ms, &out->render);
        box_load_stat(&samples->gpu, n, frame_ms, &out->gpu);
        box_load_stat(&samples->audio, n, frame_ms, &out->audio);
        box_load_stat(&samples->jobs, n, frame_ms, &out->jobs);
    }
}

/** Whether the load panel shows two stats differently: the times and the share as the script
 * formats them, or the color of the share.
 *
 * @param a A stat
 * @param b Another stat
 * @return Whether what the panel shows of them differs
 */
static bool box_load_stat_differs(
    const LoadStat *a,
    const LoadStat *b)
{
    char ta[96], tb[96];
    ecs_os_snprintf(ta, sizeof(ta), "%.1f %.1f %.0f %d", a->mean_ms, a->worst_ms, a->share,
        box_fps_level(a->mean_ms));
    ecs_os_snprintf(tb, sizeof(tb), "%.1f %.1f %.0f %d", b->mean_ms, b->worst_ms, b->share,
        box_fps_level(b->mean_ms));

    return ecs_os_strcmp(ta, tb) != 0;
}

void box_fps_load_summary_sync(
    ecs_world_t *world)
{
    double busy[BOX_JOBS_MAX_WORKERS];
    LoadSummary sum;
    const LoadSummary *cur = ecs_singleton_get(world, LoadSummary);
    box_load_summarize(ecs_singleton_get(world, FrameSamples),
        ecs_singleton_get(world, LoadSamples), box_jobs_load(world, busy), &sum);
    if (!cur || cur->frames != sum.frames || cur->workers != sum.workers ||
        box_load_stat_differs(&cur->main, &sum.main) ||
        box_load_stat_differs(&cur->render, &sum.render) ||
        box_load_stat_differs(&cur->gpu, &sum.gpu) ||
        box_load_stat_differs(&cur->audio, &sum.audio) ||
        box_load_stat_differs(&cur->jobs, &sum.jobs))
    {
        ecs_singleton_set_ptr(world, LoadSummary, &sum);
    }
}

void box_fps_load_init(
    ecs_world_t *world)
{
    box_fps_load_t *load = ecs_os_calloc_t(box_fps_load_t);
    box_load_clock_init(&load->main);
    ecs_atfini(world, box_fps_load_free, load);

    ecs_entity_t end = ecs_entity(world, { .name = "LoadEnd" });
    ecs_add_id(world, end, EcsPhase);
    ecs_add_pair(world, end, EcsDependsOn, TaskPublish);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LoadSample" }),
        .phase = EcsPreFrame,
        .run = BoxFpsLoadSample,
        .ctx = load,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LoadIdle" }),
        .phase = end,
        .run = BoxFpsLoadIdle,
        .ctx = load
    });
}
