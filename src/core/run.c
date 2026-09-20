/**
 * @file core/run.c
 * @brief Frame loop with stop conditions and CPU budget pacing.
 */

#include "core_internal.h"
#include <math.h>
#include <stdio.h>

#ifdef BOX_PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

/** State of a running frame loop */
typedef struct box_loop_t {
    ecs_world_t *world;   /**< The world being progressed */
    ecs_time_t start;     /**< Loop start time */
    box_cpu_t cpu;        /**< CPU budget accounting */
    double cpu_start;     /**< Process CPU seconds when the loop started */
    double wall_start;    /**< Wall seconds when the loop started */
    bool done;            /**< A stop condition was met */
} box_loop_t;

/** The share of one core a run may use, which a headless run always has.
 *
 * @param app The settings of the process
 * @return The budget, 1 when headless without one, 0 when a window is not paced
 */
static double box_cpu_budget(
    const App *app)
{
    double result = 0;
    if (app->cpu_budget > 0 && app->cpu_budget < 1.0) {
        result = app->cpu_budget;
    } else if (app->headless) {
        result = 1.0;
    }

    return result;
}

/** Keep the job pool of a headless world within its budget.
 *
 * A budget of one core leaves one worker: the pool would otherwise start one
 * per core, and each of them counts against the process. Settings that ask
 * for fewer workers, or for none, are left alone.
 *
 * @param world The world
 * @param budget The share of one core the process may use
 */
static void box_cpu_limit_workers(
    ecs_world_t *world,
    double budget)
{
    ecs_entity_t settings = ecs_lookup(world, "box.job.JobSettings");
    if (!settings) {
        goto done;
    }

    int32_t cap = (int32_t)ceil(budget);
    JobSettings *js = ecs_get_mut(world, settings, JobSettings);
    if (js && (js->workers == 0 || js->workers > cap)) {
        js->workers = cap;
        ecs_modified(world, settings, JobSettings);
    }

done:
    return;
}

/** Report the budget and the share of one core the loop used.
 *
 * @param loop The loop state
 * @param budget The share of one core the process may use
 */
static void box_cpu_report(
    const box_loop_t *loop,
    double budget)
{
    double wall = box_cpu_wall_time() - loop->wall_start;
    double cpu = box_cpu_process_time() - loop->cpu_start;
    double share = wall > 0 ? cpu / wall : 0;

    printf("box: cpu budget %.2f, share %.2f (%.2fs cpu over %.2fs)\n",
        budget, share, cpu, wall);
}

/** Progress the world one frame and evaluate the stop conditions.
 *
 * @param loop The loop state
 */
static void box_tick(
    box_loop_t *loop)
{
    ecs_world_t *world = loop->world;
    const App *cfg = ecs_singleton_get(world, App);
    float delta = 0;
    if (cfg->headless) {
        delta = (float)(1.0 / (cfg->target_fps > 0 ? cfg->target_fps : 60.0));
    }

    if (!ecs_progress(world, delta)) {
        loop->done = true;
        goto done;
    }

    App *app = ecs_singleton_ensure(world, App);
    app->frame ++;

    if (app->frames && app->frame >= app->frames) {
        loop->done = true;
    }

    ecs_time_t start = loop->start;
    if (app->timeout > 0 && ecs_time_measure(&start) >= app->timeout) {
        loop->done = true;
    }

    double budget = box_cpu_budget(app);
    if (budget > 0) {
        box_cpu_pace(&loop->cpu, budget);
    }

done:
    return;
}

#ifdef BOX_PLATFORM_WEB
/** Emscripten main loop callback.
 *
 * @param arg The loop state
 */
static void box_tick_web(
    void *arg)
{
    box_loop_t *loop = arg;
    box_tick(loop);
    if (loop->done) {
        emscripten_cancel_main_loop();
        ecs_fini(loop->world);
        ecs_os_free(loop);
    }
}
#endif

int box_run(
    ecs_world_t *world)
{
    int result = 0;
    App *app = ecs_singleton_ensure(world, App);
    if (app->headless && box_cpu_oversubscribed()) {
        result = -1;
        goto done;
    }

    ecs_set_target_fps(world, (float)app->target_fps);

    bool headless = app->headless;
    double budget = box_cpu_budget(app);
    if (headless) {
        box_cpu_limit_workers(world, budget);
    }

    box_loop_t *loop = ecs_os_calloc_t(box_loop_t);
    loop->world = world;
    loop->cpu_start = box_cpu_process_time();
    loop->wall_start = box_cpu_wall_time();
    ecs_os_get_time(&loop->start);

#ifdef BOX_PLATFORM_WEB
    emscripten_set_main_loop_arg(box_tick_web, loop, 0, 1);
#else
    while (!loop->done) {
        box_tick(loop);
    }

    if (headless) {
        box_cpu_report(loop, budget);
    }

    ecs_os_free(loop);
#endif
done:
    return result;
}
