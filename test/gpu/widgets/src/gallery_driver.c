/**
 * @file gallery_driver.c
 * @brief Loads the widget gallery scene and drives it through the command API, the functions of
 * gallery.h.
 */

#include "gallery.h"
#include <bake_test.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Path of the gallery scene in the repository */
#define GALLERY_SCENE "test/assets/ui/gallery.flecs"

/** Divides the render pixels of the surfaces of the gallery in the tests */
#define GALLERY_RESOLUTION_SCALE (4.0f)

/** Thickness of the scroll bars of the page, which cover its right and bottom edge */
#define GALLERY_PAGE_BAR (10.0f)

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

/** Find the members in the component argument of a get or set command line.
 *
 * The component is the path up to the first name that starts with an upper case letter, and
 * the names after it as long as they name a component, such as the mut of a template.
 *
 * @param world The world
 * @param arg The argument, such as box.widgets.Slider.mut.value
 * @return The dot before the members in arg, NULL when there are none
 */
static const char* gallery_members(
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
static char* gallery_errors(
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
static char* gallery_get_member(
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
        result = gallery_errors(&r);
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
static char* gallery_command(
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

    members = gallery_members(world, arg);
    if (!members) {
        goto run;
    }

    if (!strcmp(cmd, "get") && !line[n]) {
        result = gallery_get_member(world, session, entity, arg, members, error);
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
    char *out = gallery_command(world, session, line, &error);
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
    gallery_settle_frames(world, session, 1);
}

void gallery_settle_frames(
    ecs_world_t *world,
    ecs_entity_t session,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        char *out = gallery_run(world, session, "input_wait");
        bool drained = !strcmp(out, "0");
        ecs_os_free(out);
        if (drained) {
            break;
        }

        ecs_progress(world, 0);
    }

    test_assert(i < 1000);
    for (i = 0; i < frames; i ++) {
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

void gallery_rect(
    ecs_world_t *world,
    const char *path,
    float r[4])
{
    ecs_entity_t e = ecs_lookup(world, path);
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(e != 0);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const LayoutBounds *b = ecs_get(world, e, LayoutBounds);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_assert(t != NULL && b != NULL && ss != NULL);
    float x = (float)ss->point_width * 0.5f + t->m[3][0];
    float y = (float)ss->point_height * 0.5f - t->m[3][1];
    r[0] = x - b->width * 0.5f;
    r[1] = y - b->height * 0.5f;
    r[2] = x + b->width * 0.5f;
    r[3] = y + b->height * 0.5f;
}

/** Scroll the page of the gallery to offsets, each clamped to what the page scrolls.
 *
 * @param world The world
 * @param session The terminal session
 * @param x Offset of the content to the left
 * @param y Offset of the content upward
 */
static void gallery_scroll_page(
    ecs_world_t *world,
    ecs_entity_t session,
    float x,
    float y)
{
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c != NULL);
    x = x < 0 ? 0 : x > c->max_x ? c->max_x : x;
    y = y < 0 ? 0 : y > c->max_y ? c->max_y : y;
    if (x != c->x || y != c->y) {
        ecs_os_free(gallery_run(world, session, "set " PAGE " box.widgets.ScrollView.mut.x %.1f",
            (double)x));
        ecs_os_free(gallery_run(world, session, "set " PAGE " box.widgets.ScrollView.mut.y %.1f",
            (double)y));
        gallery_settle(world, session);
    }
}

void gallery_reveal(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    float page[4], r[4];
    gallery_rect(world, PAGE, page);
    gallery_rect(world, path, r);
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c != NULL);
    float x = c->x, y = c->y;
    page[2] -= GALLERY_PAGE_BAR;
    page[3] -= GALLERY_PAGE_BAR;
    if (r[0] < page[0]) {
        x -= page[0] - r[0];
    } else if (r[2] > page[2]) {
        x += r[2] - page[2];
    }

    if (r[1] < page[1]) {
        y -= page[1] - r[1];
    } else if (r[3] > page[3]) {
        y += r[3] - page[3];
    }

    gallery_scroll_page(world, session, x, y);
}

void gallery_show(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    float page[4], r[4];
    gallery_rect(world, PAGE, page);
    gallery_rect(world, path, r);
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c != NULL);
    gallery_scroll_page(world, session, c->x + r[0] - page[0], c->y + r[1] - page[1]);
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
    gallery_reveal(world, session, path);
    gallery_point(world, path, fx, fy, point, sizeof(point));
    ecs_os_free(gallery_run(world, session, "mouse_move %s", point));
    gallery_settle(world, session);
    char *expect = flecs_asprintf("\"%s\"", handler);
    gallery_expect(world, session, "get $ box.picking.PickStatus.handler", expect);
    ecs_os_free(expect);
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
    ecs_os_free(gallery_run(world, session, "mouse_click left %s", point));
    gallery_settle(world, session);
}

void gallery_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key)
{
    ecs_os_free(gallery_run(world, session, "key_press %s", key));
    gallery_settle(world, session);
}

char* gallery_focused(
    ecs_world_t *world,
    ecs_entity_t session)
{
    char *result = gallery_run(world, session, "get $ box.ui.FocusStatus.focused");
    size_t len = strlen(result);
    test_assert(len > 2 && result[0] == '"' && result[len - 1] == '"');
    memmove(result, result + 1, len - 2);
    result[len - 2] = '\0';

    return result;
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

void gallery_tab_to(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    int32_t i;
    bool found = false;
    for (i = 0; i < 40 && !found; i ++) {
        gallery_key(world, session, "Tab");
        char *focused = gallery_focused(world, session);
        found = !strcmp(focused, path);
        ecs_os_free(focused);
    }

    if (!found) {
        printf("Tab never reached '%s'\n", path);
    }

    test_assert(found);
}

void gallery_expect_focus(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    char *focused = gallery_focused(world, session);
    test_str(focused, path);
    ecs_os_free(focused);
}

double gallery_value(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char *line = flecs_vasprintf(fmt, args);
    va_end(args);
    char *out = gallery_run(world, session, "%s", line);
    double result = strtod(out, NULL);
    ecs_os_free(out);
    ecs_os_free(line);

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
    app->unpaced = true;
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

    /* The tests read state, not pixels: render the window and the preview at a quarter of
     * their size, so tests that share a GPU do not wait on each other */
    const char *surfaces[] = { "window", "gallery.preview_surface" };
    for (i = 0; i < 2; i ++) {
        ecs_entity_t e = ecs_lookup(world, surfaces[i]);
        test_assert(e != 0);
        Surface *surface = ecs_get_mut(world, e, Surface);
        surface->resolution_scale = GALLERY_RESOLUTION_SCALE;
        ecs_modified(world, e, Surface);
    }

    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    *session = ecs_shell_session_init(world, NULL);

    return world;
}
