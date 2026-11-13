/**
 * @file main.c
 * @brief Runs a box world with the settings of the command line.
 */

#include <boxrun.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>

#define STDERR_FILENO (2)
#define isatty _isatty
#define localtime_r(sec, tm) localtime_s(tm, sec)

/** Seconds and microseconds of the time of day */
struct timeval {
    time_t tv_sec;  /**< Seconds since the epoch */
    long tv_usec;   /**< Microseconds */
};

/** Get the time of day.
 *
 * @param tv Receives the time
 * @param tz Unused
 * @return 0
 */
static int gettimeofday(
    struct timeval *tv,
    void *tz)
{
    struct timespec ts;
    (void)tz;
    timespec_get(&ts, TIME_UTC);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);

    return 0;
}
#else
#include <sys/time.h>
#include <unistd.h>
#endif

/** Print a log message to stderr with the local time of day.
 *
 * @param level The flecs log level
 * @param file The file that logged
 * @param line The line that logged
 * @param msg The message
 */
static void boxrun_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    struct timeval tv;
    struct tm tm;
    const char *name = "info", *color = "";
    bool colors = ecs_os_api.flags_ & EcsOsApiLogWithColors;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    localtime_r(&sec, &tm);
    if (level <= -4) {
        name = "fatal";
        color = "\033[1;31m";
    } else if (level == -3) {
        name = "error";
        color = "\033[1;31m";
    } else if (level < 0) {
        name = "warning";
        color = "\033[1;33m";
    } else if (level > 0) {
        name = "debug";
        color = "\033[0;34m";
    } else {
        color = "\033[0;35m";
    }

    fprintf(stderr, "%s%02d:%02d:%02d.%03d%s %s%s%s: %s\n",
        colors ? "\033[0;90m" : "", tm.tm_hour, tm.tm_min, tm.tm_sec,
        (int)(tv.tv_usec / 1000), colors ? "\033[0m" : "",
        colors ? color : "", name, colors ? "\033[0m" : "", msg ? msg : "");
}

/** Append .flecs to the scene of App when it has no extension and name the world after it.
 *
 * @param world The world
 */
static void boxrun_scene_name(
    ecs_world_t *world)
{
    App *app = ecs_singleton_get_mut(world, App);
    if (app && app->scene && app->scene[0]) {
        const char *file = strrchr(app->scene, '/');
        file = file ? file + 1 : app->scene;
        const char *ext = strrchr(file, '.');
        if (!ext || strcmp(ext, ".flecs")) {
            char *scene = flecs_asprintf("%s.flecs", app->scene);
            ecs_os_free(app->scene);
            app->scene = scene;
            file = strrchr(scene, '/');
            file = file ? file + 1 : scene;
            ext = strrchr(file, '.');
        }

        char *name = ecs_os_strdup(file);
        name[ext - file] = '\0';
        ecs_doc_set_name(world, EcsWorld, name);
        ecs_os_free(name);
    }
}

/** Parse the command line into the App singleton and run the world.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 when the run completed, nonzero when it was refused or failed
 */
int main(
    int argc,
    char *argv[])
{
    ecs_world_t *world = ecs_init();
    int result = 0;

    ecs_os_api.log_ = boxrun_log;
    ecs_log_enable_colors(isatty(STDERR_FILENO));

    ECS_IMPORT(world, Box);

    ecs_log_set_level(0);
    ecs_trace("imported box modules");

    result = box_app_parse(world, argc, argv);
    if (!result && !ecs_singleton_get(world, App)->help) {
        boxrun_scene_name(world);
        result = box_app_run(world);
    }

    ecs_trace("shutting down");
    ecs_log_set_level(-1);
    ecs_fini(world);

    return result ? 1 : 0;
}
