/**
 * @file core/args.c
 * @brief Parses command line arguments into the App singleton.
 *
 * The engine owns a handful of options and an application adds its own as
 * AppOption entities before the parse: the flag is the name of the entity and
 * its value lands in the member of a singleton component the option names, so
 * a scene reads the flag as a component instead of the application passing it
 * around. Both sets are listed by --help and a flag nobody claims is an error.
 */

#include <box.h>
#include <string.h>
#include <stdlib.h>

/** Options of an application one parse resolves */
#define BOX_APP_OPTION_MAX (64)

/** Column the description of an option starts at in the usage text */
#define BOX_APP_USAGE_PAD (21)

/** One application option of the world, with the flag it is named after */
typedef struct box_option_t {
    const char *name;        /**< Flag of the option, without its dashes */
    ecs_entity_t member;     /**< Member of the component the value lands in */
    const char *fallback;    /**< Value the bare flag takes, NULL when it needs one */
    const char *description; /**< What the option does */
} box_option_t;

/** Collect the application options of the world, ordered by their flag.
 *
 * @param world The world
 * @param out Receives the options
 * @param max Size of the array
 * @return Number of options written
 */
static int32_t box_args_options(
    ecs_world_t *world,
    box_option_t *out,
    int32_t max)
{
    int32_t count = 0;
    ecs_iter_t it = ecs_each(world, AppOption);
    while (ecs_each_next(&it)) {
        const AppOption *option = ecs_field(&it, AppOption, 0);
        int32_t i;
        for (i = 0; i < it.count && count < max; i ++) {
            const char *name = ecs_get_name(world, it.entities[i]);
            int32_t j;
            if (!name) {
                continue;
            }

            j = count ++;
            while (j > 0 && strcmp(out[j - 1].name, name) > 0) {
                out[j] = out[j - 1];
                j --;
            }

            out[j].name = name;
            out[j].member = option[i].member;
            out[j].fallback = option[i].fallback;
            out[j].description = option[i].description;
        }
    }

    return count;
}

/** Test whether the member an option writes holds a boolean.
 *
 * @param world The world
 * @param member The member entity
 * @return Whether the member is a bool
 */
static bool box_args_is_bool(
    ecs_world_t *world,
    ecs_entity_t member)
{
    const EcsMember *m = member ? ecs_get(world, member, EcsMember) : NULL;

    return m != NULL && m->type == ecs_id(ecs_bool_t);
}

/** Write the value of an option into the member of its singleton component.
 *
 * @param world The world
 * @param member Member the option writes
 * @param value Text of the value
 * @return 0 when the value landed, nonzero when it did not
 */
static int box_args_set(
    ecs_world_t *world,
    ecs_entity_t member,
    const char *value)
{
    ecs_entity_t comp = member ? ecs_get_parent(world, member) : 0;
    const EcsComponent *info = comp ? ecs_get(world, comp, EcsComponent) : NULL;
    ecs_meta_cursor_t cur;
    char *path = NULL;
    void *ptr = NULL;
    int result = -1;
    if (!info) {
        ecs_err("option member is not a member of a component");
        goto done;
    }

    ptr = ecs_ensure_id(world, comp, comp, (size_t)info->size);
    path = ecs_get_path_w_sep(world, comp, member, ".", NULL);
    cur = ecs_meta_cursor(world, comp, ptr);
    result = ecs_meta_push(&cur);
    result = result ? result : ecs_meta_dotmember(&cur, path);
    result = result ? result : ecs_meta_set_string(&cur, value);
    ecs_meta_pop(&cur);
    ecs_modified_id(world, comp, comp);
    ecs_os_free(path);
done:
    return result;
}

/** Find the option of the world an argument names.
 *
 * @param options The options of the world
 * @param count Number of options
 * @param flag The argument without its dashes
 * @param len Bytes of the flag before its equals sign
 * @return The option, or NULL when none is named
 */
static const box_option_t* box_args_find(
    const box_option_t *options,
    int32_t count,
    const char *flag,
    size_t len)
{
    const box_option_t *result = NULL;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        if (!strncmp(options[i].name, flag, len) && !options[i].name[len]) {
            result = &options[i];
        }
    }

    return result;
}

/** Let the options of the world claim an argument and write its value.
 *
 * @param world The world
 * @param arg The argument, with its dashes
 * @param next The argument after it, NULL when there is none
 * @param used Receives whether the value came from the next argument
 * @return 0 when an option took the argument, nonzero when none claimed it
 */
static int box_args_option(
    ecs_world_t *world,
    const char *arg,
    const char *next,
    bool *used)
{
    box_option_t options[BOX_APP_OPTION_MAX];
    int32_t count = box_args_options(world, options, BOX_APP_OPTION_MAX);
    const char *flag = arg, *eq, *value = NULL;
    const box_option_t *option;
    int result = -1;
    used[0] = false;
    while (flag[0] == '-') {
        flag ++;
    }

    eq = strchr(flag, '=');
    option = box_args_find(options, count, flag,
        eq ? (size_t)(eq - flag) : strlen(flag));
    if (!option) {
        goto done;
    }

    if (eq) {
        value = eq + 1;
    } else if (option->fallback && option->fallback[0]) {
        value = option->fallback;
    } else if (box_args_is_bool(world, option->member)) {
        value = "true";
    } else if (next && next[0] != '-') {
        value = next;
        used[0] = true;
    }

    if (!value) {
        ecs_err("option '%s' needs a value", arg);
        result = -2;
        goto done;
    }

    result = box_args_set(world, option->member, value) ? -2 : 0;
done:
    return result;
}

/** Append one application option to the usage text.
 *
 * @param world The world
 * @param buf The text being built
 * @param option The option
 */
static void box_args_usage_option(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    const box_option_t *option)
{
    const char *form = " <value>";
    int32_t pad;
    if (option->fallback && option->fallback[0]) {
        form = "[=<value>]";
    } else if (box_args_is_bool(world, option->member)) {
        form = "";
    }

    pad = BOX_APP_USAGE_PAD -
        (int32_t)(strlen(option->name) + strlen(form)) - 2;
    ecs_strbuf_append(buf, "  --%s%s", option->name, form);
    if (option->description) {
        while (pad -- > 0) {
            ecs_strbuf_appendch(buf, ' ');
        }

        ecs_strbuf_append(buf, " %s", option->description);
    }

    ecs_strbuf_appendch(buf, '\n');
}

char* box_args_usage(
    ecs_world_t *world,
    const char *exe)
{
    box_option_t options[BOX_APP_OPTION_MAX];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = box_args_options(world, options, BOX_APP_OPTION_MAX);
    ecs_strbuf_append(&buf,
        "Usage: %s [options] [scene.flecs]\n"
        "  --headless            run without a window\n"
        "  --frames <n>          stop after n frames\n"
        "  --timeout <s|infinite> stop after s seconds\n"
        "  --cpu-budget <0..1>   fraction of one core to use\n"
        "  --fps <n>             target frame rate, 0 for uncapped\n"
        "  --port <n>            port of the command endpoints, 0 disables them\n",
        exe);
    for (i = 0; i < count; i ++) {
        box_args_usage_option(world, &buf, &options[i]);
    }

    return ecs_strbuf_get(&buf);
}

/** The entity a scene of the command line is loaded on: scenes.<stem of the file>.
 *
 * A script loaded on no entity takes its entity from its path, and an absolute
 * path leaves a root entity with an empty name in the world, which is what a
 * command that walks the root then trips over. A reload of the same file lands
 * on the same entity and replaces the scene.
 *
 * @param world The world
 * @param path The path of the scene file
 * @return The scene entity, under the scenes scope
 */
static ecs_entity_t box_scene_entity(
    ecs_world_t *world,
    const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    char *stem = ecs_os_malloc((ecs_size_t)len + 1);

    memcpy(stem, base, len);
    stem[len] = '\0';

    ecs_entity_t scenes = ecs_entity(world, { .name = "scenes" });
    ecs_entity_t result = ecs_entity(world, { .name = stem, .parent = scenes });

    ecs_os_free(stem);

    return result;
}

/** Print usage to stderr.
 *
 * @param world The world
 * @param exe Executable name
 */
static void box_print_usage(
    ecs_world_t *world,
    const char *exe)
{
    char *usage = box_args_usage(world, exe);
    fputs(usage, stderr);
    ecs_os_free(usage);
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
        bool used = false;
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
        } else if (!strcmp(arg, "--port") && value) {
            app->port = atoi(value);
            i ++;
        } else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            ecs_singleton_modified(world, App);
            box_print_usage(world, argv[0]);
            goto error;
        } else if (arg[0] == '-') {
            int claimed;
            ecs_singleton_modified(world, App);
            claimed = box_args_option(world, arg, value, &used);
            app = ecs_singleton_ensure(world, App);
            if (claimed) {
                if (claimed == -1) {
                    ecs_err("unknown option '%s'", arg);
                }

                box_print_usage(world, argv[0]);
                goto error;
            }

            if (used) {
                i ++;
            }
        } else {
            ecs_singleton_modified(world, App);
            if (!ecs_script(world, {
                .entity = box_scene_entity(world, arg), .filename = arg }))
            {
                ecs_err("failed to load scene '%s'", arg);
                goto error;
            }

            app = ecs_singleton_ensure(world, App);
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
