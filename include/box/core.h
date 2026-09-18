/**
 * @file box/core.h
 * @brief Engine bootstrap: process folder, headless options and the frame loop.
 *
 * The box.core module owns the App singleton and runs the main loop.
 */

#ifndef BOX_CORE_H
#define BOX_CORE_H

#include <flecs.h>

#ifdef BOX_CORE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Port the command endpoints listen on when --port is not given */
#define BOX_PORT_DEFAULT (27750)

/** Process wide settings, set once from the command line before the loop starts */
ECS_STRUCT(App, {
    bool headless;        /**< Run without a window */
    int32_t frames;       /**< Stop after this many frames, 0 for no limit */
    double timeout;       /**< Stop after this many seconds, 0 for no limit */
    double cpu_budget;    /**< Fraction of one core this process may use, 1 for all */
    double target_fps;    /**< Frame rate the loop paces to, 0 for uncapped */
    int32_t port;         /**< Port of the command endpoints, 0 disables the server */
    int32_t frame;        /**< Frames completed so far, written by the engine */
    char *process_dir;    /**< Folder with temporary files, written by the engine */
});

/** Register the box.core module.
 *
 * @param world The world
 */
void BoxCoreImport(
    ecs_world_t *world);

/** Parse command line arguments into the App singleton.
 *
 * @param world The world
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, nonzero when arguments are invalid
 */
int box_parse_args(
    ecs_world_t *world,
    int argc,
    char *argv[]);

/** Run the frame loop until a stop condition from App is met.
 *
 * @param world The world
 * @return 0 on success, nonzero when the loop aborted
 */
int box_run(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
