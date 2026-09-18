/**
 * @file core/run.c
 * @brief Frame loop with stop conditions and CPU budget pacing.
 */

#include "core_internal.h"

#ifdef BOX_PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

/** State of a running frame loop */
typedef struct box_loop_t {
    ecs_world_t *world;   /**< The world being progressed */
    ecs_time_t start;     /**< Loop start time */
    box_cpu_t cpu;        /**< CPU budget accounting */
    bool done;            /**< A stop condition was met */
} box_loop_t;

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

    if (app->cpu_budget > 0 && app->cpu_budget < 1.0) {
        box_cpu_pace(&loop->cpu, app->cpu_budget);
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

    box_loop_t *loop = ecs_os_calloc_t(box_loop_t);
    loop->world = world;
    ecs_os_get_time(&loop->start);

#ifdef BOX_PLATFORM_WEB
    emscripten_set_main_loop_arg(box_tick_web, loop, 0, 1);
#else
    while (!loop->done) {
        box_tick(loop);
    }

    ecs_os_free(loop);
#endif
done:
    return result;
}
