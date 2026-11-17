/**
 * @file WindowWarning.c
 * @brief Without the glfw platform (the browser build) the canvas is the window of a surface, so
 * opening a surface that does not render offscreen logs no "cannot open a window" warning.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

static char window_warnings[4096];

static void WindowCaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -2 &&
        strlen(window_warnings) + strlen(msg) + 2 < sizeof(window_warnings))
    {
        strcat(window_warnings, msg);
        strcat(window_warnings, "\n");
    }
}

void WindowWarning_no_warning_without_glfw(void)
{
    // surface/web_window_warning
#if !defined(__APPLE__) && !defined(_WIN32)
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, {
        .headless = false, .frames = 1, .unguarded = true, .unpaced = true });
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, { .width = 320, .height = 240 });

    window_warnings[0] = '\0';
    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    ecs_os_api_log_t real_log = ecs_os_api.log_;
    ecs_os_api.log_ = WindowCaptureLog;
    ecs_progress(world, 0);
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_assert(strstr(window_warnings, "cannot open a window") == NULL);

    ecs_fini(world);
#endif
}
