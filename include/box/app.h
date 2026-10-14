/**
 * @file box/app.h
 * @brief Application settings, headless operation and the frame loop.
 *
 * The box.app module owns the App singleton that the command line fills and the
 * AppStatus singleton the engine writes; box_app_run runs the world until App stops it.
 */

#ifndef BOX_APP_H
#define BOX_APP_H

#include <flecs.h>

#ifdef BOX_APP_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Environment variable that, set to anything but 0, makes every new App unguarded and unpaced */
#define BOX_TEST_ENV "BOX_TEST"

/** Folder the temporary folders of headless processes are created in */
#define BOX_TMP_ROOT "/tmp/box"

/** Port of the command endpoints when --port is not given */
#define BOX_APP_PORT (27750)

/** Frame rate the module sets in App before the command line is parsed */
#define BOX_APP_FPS (60)

/** Settings of the process, filled from the command line before the world runs.
 *
 * A headless run needs a frame limit or a timeout, is refused on an oversubscribed machine
 * unless unguarded, and sleeps when it uses more than its CPU budget unless unpaced.
 */
ECS_STRUCT(App, {
    bool headless;      /**< Run without a window, with a fixed delta time */
    int32_t frames;     /**< Stop after this many frames, 0 for no limit */
    double timeout;     /**< Stop after this many seconds, 0 for none, negative for infinite */
    float cpu_budget;   /**< Percent of one core a headless run may use, default 100, 0 no limit */
    float fps;          /**< Target frame rate, default 60, 0 uncapped with a measured delta */
    int32_t port;       /**< Port of the command endpoints, 0 disables them */
    bool unguarded;     /**< A headless run starts on an oversubscribed machine */
    bool unpaced;       /**< A headless run ignores its CPU budget */
    char *scene;        /**< Script file loaded before the first frame, NULL for none */
});

/** State of the process the engine writes, read only for applications */
ECS_STRUCT(AppStatus, {
    int32_t frame;      /**< Frames completed since the world was created */
    double elapsed;     /**< Wall seconds since the world was created */
    double slept;       /**< Wall seconds the CPU budget has slept in total */
    char *tmp_dir;      /**< Folder for temporary files of a headless process, else NULL */
    bool running;       /**< box_app_run is stepping the world */
});

/** Register the box.app module.
 *
 * @param world The world
 */
void BoxAppImport(
    ecs_world_t *world);

/** Write command line arguments into the App singleton.
 *
 * Accepts --headless, --frames <n>, --timeout <s|infinite>, --cpu-budget <pct>,
 * --fps <n>, --port <n> and one scene file.
 *
 * @param world The world
 * @param argc Argument count, including the executable
 * @param argv Argument array, starting with the executable
 * @return 0 on success, nonzero when an argument is invalid
 */
int box_app_parse(
    ecs_world_t *world,
    int argc,
    char *argv[]);

/** Run the world with the settings of the App singleton until a stop condition is met.
 *
 * @param world The world
 * @return 0 when the run completed, nonzero when it was refused or failed to start
 */
int box_app_run(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
