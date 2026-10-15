/**
 * @file app/args.c
 * @brief Writes command line arguments into the App singleton.
 */

#include "app.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Parse a whole argument as a number.
 *
 * @param text The argument
 * @param suffix Character allowed after the number, 0 for none
 * @param out Receives the number
 * @return 0 when the argument is a finite number, nonzero when it is not
 */
static int box_app_number(
    const char *text,
    char suffix,
    double *out)
{
    char *end = NULL;
    int result = -1;
    if (!text || !text[0]) {
        goto done;
    }

    *out = strtod(text, &end);
    if (suffix && end[0] == suffix) {
        end ++;
    }

    if (end[0] || !isfinite(*out)) {
        goto done;
    }

    result = 0;
done:
    return result;
}

/** Parse a size written as <width>x<height>, both whole numbers from 1 to 8192, the largest
 * size box.surface renders.
 *
 * @param text The argument
 * @param width Receives the width
 * @param height Receives the height
 * @return 0 when the argument is a valid size, nonzero when it is not
 */
static int box_app_size(
    const char *text,
    int32_t *width,
    int32_t *height)
{
    char *end = NULL;
    long w = 0, h = 0;
    int result = -1;
    if (!text || text[0] < '0' || text[0] > '9') {
        goto done;
    }

    w = strtol(text, &end, 10);
    if (end[0] != 'x' || end[1] < '0' || end[1] > '9') {
        goto done;
    }

    h = strtol(end + 1, &end, 10);
    if (end[0] || w < 1 || h < 1 || w > 8192 || h > 8192) {
        goto done;
    }

    *width = (int32_t)w;
    *height = (int32_t)h;
    result = 0;
done:
    return result;
}

/** Parse the value of one option into the App singleton.
 *
 * @param app The App singleton
 * @param flag The option without its dashes
 * @param value The argument after the option, NULL when there is none
 * @return 0 when the value is valid, nonzero when it is not
 */
static int box_app_option(
    App *app,
    const char *flag,
    const char *value)
{
    double v = 0;
    int result = -1;
    if (!strcmp(flag, "frames")) {
        if (!box_app_number(value, 0, &v) && v >= 1 && v <= INT32_MAX && v == floor(v)) {
            app->frames = (int32_t)v;
            result = 0;
        }
    } else if (!strcmp(flag, "timeout")) {
        if (value && !strcmp(value, "infinite")) {
            app->timeout = -1;
            result = 0;
        } else if (!box_app_number(value, 0, &v) && v > 0) {
            app->timeout = v;
            result = 0;
        }
    } else if (!strcmp(flag, "cpu-budget")) {
        if (!box_app_number(value, '%', &v) && v > 0) {
            app->cpu_budget = (float)v;
            result = 0;
        }
    } else if (!strcmp(flag, "fps")) {
        if (!box_app_number(value, 0, &v) && v >= 0) {
            app->fps = (float)v;
            result = 0;
        }
    } else if (!strcmp(flag, "surface")) {
        result = box_app_size(value, &app->surface_width, &app->surface_height);
    } else if (!strcmp(flag, "port")) {
        if (!box_app_number(value, 0, &v) && v >= 0 && v <= 65535 && v == floor(v)) {
            app->port = (int32_t)v;
            result = 0;
        }
    }

    if (result) {
        ecs_err("invalid value '%s' for --%s", value ? value : "", flag);
    }

    return result;
}

/** Test whether an option takes the argument after it as its value.
 *
 * @param flag The option without its dashes
 * @return Whether the option takes a value
 */
static bool box_app_has_value(
    const char *flag)
{
    return !strcmp(flag, "frames") || !strcmp(flag, "timeout") ||
        !strcmp(flag, "cpu-budget") || !strcmp(flag, "fps") || !strcmp(flag, "port") ||
        !strcmp(flag, "surface");
}

int box_app_parse(
    ecs_world_t *world,
    int argc,
    char *argv[])
{
    App *app = ecs_singleton_get_mut(world, App);
    int result = 0, i;
    bool scene = false;
    if (!app) {
        ecs_err("box.app is not imported");
        result = -1;
        goto done;
    }

    for (i = 1; i < argc && !result; i ++) {
        const char *arg = argv[i];
        if (arg[0] != '-') {
            if (scene) {
                ecs_err("more than one scene: '%s'", arg);
                result = -1;
            } else {
                ecs_os_free(app->scene);
                app->scene = ecs_os_strdup(arg);
                scene = true;
            }
        } else if (!strcmp(arg, "--headless")) {
            app->headless = true;
        } else if (arg[1] == '-' && box_app_has_value(&arg[2])) {
            result = box_app_option(app, &arg[2], i + 1 < argc ? argv[i + 1] : NULL);
            i ++;
        } else {
            ecs_err("unknown option '%s'", arg);
            result = -1;
        }
    }

    ecs_singleton_modified(world, App);
done:
    return result;
}
