/**
 * @file gallery.c
 * @brief Loads the widget gallery scene and drives it through the command API.
 */

#include "gallery.h"
#include <bake_test.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Path of the gallery scene in the repository */
#define GALLERY_SCENE "etc/scenes/widgets.flecs"

/** Find the scene in the working directory or one of its ancestors.
 *
 * @return The path of the scene, owned by the caller, NULL when it is not found
 */
static char* gallery_path(void) {
    char dir[4096];
    char *result = NULL, *slash = NULL;
    if (!getcwd(dir, sizeof(dir))) {
        goto done;
    }

    do {
        result = flecs_asprintf("%s/%s", dir, GALLERY_SCENE);
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

char* gallery_run(
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

void gallery_expect(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *expect)
{
    char *out = gallery_run(world, session, "%s", line);
    test_str(out, expect);
    ecs_os_free(out);
}

void gallery_settle(
    ecs_world_t *world,
    ecs_entity_t session)
{
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        char *out = gallery_run(world, session, "input.wait");
        bool drained = !strcmp(out, "0");
        ecs_os_free(out);
        if (drained) {
            break;
        }

        ecs_progress(world, 0);
    }

    test_assert(i < 1000);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }
}

void gallery_point(
    ecs_world_t *world,
    const char *path,
    float fx,
    float fy,
    char *out,
    size_t size)
{
    ecs_entity_t e = ecs_lookup(world, path);
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(e != 0);
    test_assert(window != 0);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const LayoutBounds *b = ecs_get(world, e, LayoutBounds);
    const SurfaceStatus *s = ecs_get(world, window, SurfaceStatus);
    test_assert(t != NULL);
    test_assert(b != NULL);
    test_assert(s != NULL);
    float x = (float)s->point_width * 0.5f + t->m[3][0] + fx * b->width * 0.5f;
    float y = (float)s->point_height * 0.5f - t->m[3][1] + fy * b->height * 0.5f;
    ecs_os_snprintf(out, (ecs_size_t)size, "[%.1f, %.1f]", (double)x, (double)y);
}

void gallery_hover(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    float fx,
    float fy,
    const char *handler)
{
    char point[64];
    gallery_point(world, path, fx, fy, point, sizeof(point));
    ecs_os_free(gallery_run(world, session, "mouse.move %s", point));
    gallery_settle(world, session);
    gallery_expect(world, session, "get box.picking.PickStatus PickStatus.handler", handler);
}

void gallery_click(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    float fx,
    float fy,
    const char *handler)
{
    char point[64];
    gallery_hover(world, session, path, fx, fy, handler);
    gallery_point(world, path, fx, fy, point, sizeof(point));
    ecs_os_free(gallery_run(world, session, "mouse.click left %s", point));
    gallery_settle(world, session);
}

void gallery_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key)
{
    ecs_os_free(gallery_run(world, session, "key.press %s", key));
    gallery_settle(world, session);
}

char* gallery_focused(
    ecs_world_t *world,
    ecs_entity_t session)
{
    return gallery_run(world, session, "get box.ui.FocusStatus FocusStatus.focused");
}

char* gallery_mut(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    const char *tmpl,
    const char *member)
{
    return gallery_run(world, session, "get " BASICS ".%s box.widgets.%s.mut.%s",
        path, tmpl, member);
}

double gallery_number(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    const char *tmpl,
    const char *member)
{
    char *out = gallery_mut(world, session, path, tmpl, member);
    double result = strtod(out, NULL);
    ecs_os_free(out);

    return result;
}

int32_t gallery_changes(
    ecs_world_t *world,
    ecs_entity_t session)
{
    char *out = gallery_run(world, session, "get gallery Gallery.mut.changes");
    int32_t result = atoi(out);
    ecs_os_free(out);

    return result;
}

ecs_world_t* gallery_load(
    ecs_entity_t *session)
{
    ecs_world_t *world = ecs_init();
    int32_t i;
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = gallery_path();
    test_assert(path != NULL);
    ecs_entity_t script = ecs_script(world, { .filename = path });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->script != NULL);
    test_assert(loaded->error == NULL);

    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    *session = box_terminal_new(world);

    return world;
}
