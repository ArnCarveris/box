/**
 * @file core/args.c
 * @brief Parses command line arguments into the App singleton.
 */

#include <box.h>
#include <string.h>
#include <stdlib.h>

/** Print usage to stderr.
 *
 * @param exe Executable name
 */
static void box_print_usage(
    const char *exe)
{
    fprintf(stderr,
        "Usage: %s [options] [scene.flecs]\n"
        "  --headless            run without a window\n"
        "  --frames <n>          stop after n frames\n"
        "  --timeout <s|infinite> stop after s seconds\n"
        "  --cpu-budget <0..1>   fraction of one core to use\n"
        "  --fps <n>             target frame rate, 0 for uncapped\n",
        exe);
}

int box_parse_args(
    ecs_world_t *world,
    int argc,
    char *argv[])
{
    App *app = ecs_singleton_ensure(world, App);
    int i;
    for (i = 1; i < argc; i ++) {
        const char *arg = argv[i];
        const char *value = (i + 1) < argc ? argv[i + 1] : NULL;
        if (!strcmp(arg, "--headless")) {
            app->headless = true;
        } else if (!strcmp(arg, "--frames") && value) {
            app->frames = atoi(value);
            i ++;
        } else if (!strcmp(arg, "--timeout") && value) {
            app->timeout = strcmp(value, "infinite") ? atof(value) : -1.0;
            i ++;
        } else if (!strcmp(arg, "--cpu-budget") && value) {
            app->cpu_budget = atof(value);
            i ++;
        } else if (!strcmp(arg, "--fps") && value) {
            app->target_fps = atof(value);
            i ++;
        } else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            box_print_usage(argv[0]);
            goto error;
        } else if (arg[0] == '-') {
            ecs_err("unknown option '%s'", arg);
            box_print_usage(argv[0]);
            goto error;
        } else {
            ecs_singleton_modified(world, App);
            ecs_script(world, { .filename = arg });
        }
    }

    if (app->headless && !app->frames && !app->timeout) {
        ecs_err("--headless requires --frames or --timeout");
        goto error;
    }

    ecs_singleton_modified(world, App);
    return 0;
error:
    return -1;
}
