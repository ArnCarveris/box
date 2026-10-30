/**
 * @file showcase.c
 * @brief Loads the UI showcase scene and drives it through the command API, shared by the suites.
 */

#include "showcase.h"
#include <bake_test.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Path of the showcase scene in the repository */
#define SHOWCASE_SCENE "test/assets/scenes/ui_showcase.flecs"

/** Find the scene in the working directory or one of its ancestors.
 *
 * @return The path of the scene, owned by the caller, NULL when it is not found
 */
static char* showcase_path(void) {
    char dir[4096];
    char *result = NULL, *slash = NULL;
    if (!getcwd(dir, sizeof(dir))) {
        goto done;
    }

    do {
        result = flecs_asprintf("%s/%s", dir, SHOWCASE_SCENE);
        if (!access(result, R_OK)) {
            goto done;
        }

        ecs_os_free(result);
        result = NULL;
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
        }
    } while (slash && dir[0]);

done:
    return result;
}

/** Find the members in the component argument of a get or set command line.
 *
 * The component is the path up to the first name that starts with an upper case letter, and
 * the names after it as long as they name a component, such as the mut of a template.
 *
 * @param world The world
 * @param arg The argument, such as box.widgets.Slider.mut.value
 * @return The dot before the members in arg, NULL when there are none
 */
static const char* showcase_members(
    const ecs_world_t *world,
    const char *arg)
{
    const char *result = NULL, *name = arg;
    char path[256];
    while (name && name[0] && !isupper((unsigned char)name[0])) {
        name = strchr(name, '.');
        if (name) {
            name ++;
        }
    }

    if (!name || !name[0]) {
        goto done;
    }

    result = strchr(name, '.');
    while (result) {
        const char *next = strchr(result + 1, '.');
        int len = next ? (int)(next - arg) : (int)strlen(arg);
        ecs_os_snprintf(path, ECS_SIZEOF(path), "%.*s", len, arg);
        ecs_entity_t e = ecs_lookup(world, path);
        if (!e || !ecs_has(world, e, EcsComponent)) {
            goto done;
        }

        result = next;
    }

done:
    return result;
}

/** Format the errors of a command result as box_command_run does.
 *
 * @param r The result
 * @return The error lines, owned by the caller
 */
static char* showcase_errors(
    const ecs_shell_result_t *r)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char **errors = ecs_vec_first(&r->errors);
    int32_t i, count = ecs_vec_count(&r->errors);
    for (i = 0; i < count; i ++) {
        if (i) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        ecs_strbuf_appendstr(&buf, BOX_COMMAND_ERROR);
        ecs_strbuf_appendstr(&buf, errors[i]);
    }

    return ecs_strbuf_get(&buf);
}

/** Get a member of a component: run get for the component and format the member as JSON.
 *
 * @param world The world
 * @param session The session
 * @param entity The entity argument
 * @param arg The component argument with the members
 * @param members The dot before the members in arg
 * @param error Set to whether the command failed
 * @return The output, owned by the caller
 */
static char* showcase_get_member(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *entity,
    const char *arg,
    const char *members,
    bool *error)
{
    char *result = NULL;
    char *line = flecs_asprintf("get %s %.*s", entity, (int)(members - arg), arg);
    ecs_shell_result_t r = ecs_shell_run(world, session, line);
    *error = true;
    if (ecs_vec_count(&r.errors)) {
        result = showcase_errors(&r);
        goto done;
    }

    const ecs_value_t *ret = ecs_vec_first(&r.results);
    if (!ret || ret->type != ecs_id(ecs_value_t)) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "'%s' returned no value", line);
        goto done;
    }

    const ecs_value_t *value = ret->ptr;
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, value->type, value->ptr);
    if (ecs_meta_push(&cur) || ecs_meta_dotmember(&cur, members + 1)) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "'%s' has no member '%s'", arg, members + 1);
        goto done;
    }

    result = ecs_ptr_to_json(world, ecs_meta_get_type(&cur), ecs_meta_get_ptr(&cur));
    *error = result == NULL;

done:
    ecs_shell_result_fini(world, &r);
    ecs_os_free(line);

    return result;
}

/** Run a command line with box_command_run, with member paths for get and set.
 *
 * "get e C.m.n" gets C and returns member m.n as JSON, "set e C.m.n v" runs
 * "set e C {m: {n: v}}", which assigns only that member.
 *
 * @param world The world
 * @param session The session
 * @param line The command line
 * @param error Set to whether the command failed
 * @return The output, owned by the caller
 */
static char* showcase_command(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error)
{
    char cmd[8], entity[256], arg[256];
    const char *members = NULL, *name = NULL, *dot = NULL;
    char *result = NULL;
    int n = 0;
    if (sscanf(line, "%7s %255s %255s %n", cmd, entity, arg, &n) != 3) {
        goto run;
    }

    members = showcase_members(world, arg);
    if (!members) {
        goto run;
    }

    if (!strcmp(cmd, "get") && !line[n]) {
        result = showcase_get_member(world, session, entity, arg, members, error);
        goto done;
    }

    if (strcmp(cmd, "set") || !line[n]) {
        goto run;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t depth = 0;
    ecs_strbuf_append(&buf, "set %s %.*s ", entity, (int)(members - arg), arg);
    for (name = members + 1; name; name = dot ? dot + 1 : NULL, depth ++) {
        dot = strchr(name, '.');
        ecs_strbuf_append(&buf, "{%.*s: ", dot ? (int)(dot - name) : (int)strlen(name), name);
    }

    ecs_strbuf_appendstr(&buf, line + n);
    for (; depth; depth --) {
        ecs_strbuf_appendch(&buf, '}');
    }

    char *set = ecs_strbuf_get(&buf);
    result = box_command_run(world, session, set, error);
    ecs_os_free(set);
    goto done;

run:
    result = box_command_run(world, session, line, error);

done:
    return result;
}

char* showcase_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char *line = flecs_vasprintf(fmt, args);
    va_end(args);
    bool error = true;
    char *out = showcase_command(world, session, line, &error);
    if (error) {
        printf("command '%s' failed: %s\n", line, out);
    }

    test_bool(error, false);
    ecs_os_free(line);

    return out;
}

void showcase_expect(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *expect)
{
    char *out = showcase_run(world, session, "%s", line);
    test_str(out, expect);
    ecs_os_free(out);
}

void showcase_step(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
    }
}

void showcase_settle(
    ecs_world_t *world,
    ecs_entity_t session)
{
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        char *out = showcase_run(world, session, "input_wait");
        bool drained = !strcmp(out, "0");
        ecs_os_free(out);
        if (drained) {
            break;
        }

        ecs_progress(world, 0);
    }

    test_assert(i < 1000);
    showcase_step(world, 3);
}

void showcase_hover(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point,
    const char *handler)
{
    ecs_os_free(showcase_run(world, session, "mouse_move %s", point));
    showcase_settle(world, session);
    char *expect = flecs_asprintf("\"%s\"", handler);
    showcase_expect(world, session, "get $ box.picking.PickStatus.handler", expect);
    ecs_os_free(expect);
}

void showcase_click(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point)
{
    ecs_os_free(showcase_run(world, session, "mouse_click left %s", point));
    showcase_settle(world, session);
}

void showcase_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key)
{
    ecs_os_free(showcase_run(world, session, "key_press %s", key));
    showcase_settle(world, session);
}

char* showcase_state(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member)
{
    return showcase_run(world, session, "get showcase Showcase.mut.%s", member);
}

double showcase_value(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line)
{
    char *out = showcase_run(world, session, "%s", line);
    double result = strtod(out, NULL);
    ecs_os_free(out);

    return result;
}

double showcase_number(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member)
{
    char *out = showcase_state(world, session, member);
    double result = strtod(out, NULL);
    ecs_os_free(out);

    return result;
}

ecs_world_t* showcase_load(
    ecs_entity_t *session)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = showcase_path();
    test_assert(path != NULL);
    ecs_entity_t script = ecs_script(world, { .filename = path });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->script != NULL);
    test_assert(loaded->error == NULL);

    showcase_step(world, 2);
    *session = ecs_shell_session_init(world, NULL);

    return world;
}
