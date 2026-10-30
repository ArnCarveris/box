/**
 * @file showcase.c
 * @brief Loads the UI showcase scene and drives it through the command API, shared by the suites.
 */

#include "showcase.h"
#include <bake_test.h>
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
    char *out = box_terminal_run(world, session, line, &error);
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
        char *out = showcase_run(world, session, "input.wait");
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
    ecs_os_free(showcase_run(world, session, "mouse.move %s", point));
    showcase_settle(world, session);
    showcase_expect(world, session, "get box.picking.PickStatus PickStatus.handler", handler);
}

void showcase_click(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point)
{
    ecs_os_free(showcase_run(world, session, "mouse.click left %s", point));
    showcase_settle(world, session);
}

void showcase_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key)
{
    ecs_os_free(showcase_run(world, session, "key.press %s", key));
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
    *session = box_terminal_new(world);

    return world;
}
