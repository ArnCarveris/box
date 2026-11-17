/**
 * @file Editor.c
 * @brief The editor overlay: opening creates its view and content, closing deletes the view,
 * reopening works, and the Editor singleton drives all of it from C, scripts and commands.
 */

#include <box.h>
#include <bake_test.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../../../src/posix.h"

/** Path of the title label the script template creates in the panel */
#define EDITOR_TITLE BOX_EDITOR_PANEL ".title_bar.label"

/** Path of the scroll view of the entity tree */
#define EDITOR_BODY BOX_EDITOR_PANEL ".tree_view"

/** Path of the entity tree */
#define EDITOR_TREE EDITOR_BODY ".tree"

/** Path of the inspector of the selected entity */
#define EDITOR_INSPECTOR BOX_EDITOR_PANEL ".inspector_view.inspector"

/** Create a world with box, the widget library and no surface.
 *
 * @return The world
 */
static ecs_world_t* editor_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    return world;
}

/** Set Editor.open and run frames until the editor followed it.
 *
 * @param world The world
 * @param open Whether the editor is open
 */
static void editor_open(
    ecs_world_t *world,
    bool open)
{
    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->open = open;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Check that the editor view exists and draws the editor scene as an overlay.
 *
 * @param world The world
 * @return The view
 */
static ecs_entity_t editor_check_view(
    ecs_world_t *world)
{
    ecs_entity_t view = ecs_lookup(world, BOX_EDITOR_VIEW);
    test_assert(view != 0);
    const View *v = ecs_get(world, view, View);
    test_assert(v != NULL);
    test_int(v->order, BOX_VIEW_ORDER_EDITOR);
    test_assert(v->camera == ecs_lookup(world, BOX_EDITOR_UI ".camera"));
    test_bool(v->clear.enabled, false);
    test_bool(v->lighting.enabled, false);
    test_bool(v->shadows.enabled, false);
    test_bool(v->culling.enabled, false);
    test_int(ecs_vec_count(&v->effects), 0);

    return view;
}

void Editor_closed_by_default(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    ecs_progress(world, 0);

    test_bool(ecs_singleton_get(world, Editor)->open, false);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_UI) == 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_SCRIPT) == 0);

    ecs_fini(world);
}

void Editor_open_creates_view_and_content(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);

    editor_check_view(world);
    ecs_entity_t root = ecs_lookup(world, BOX_EDITOR_UI);
    test_assert(root != 0);
    test_assert(ecs_has(world, root, Scene));
    const Camera *cam = ecs_get(world, ecs_lookup(world, BOX_EDITOR_UI ".camera"), Camera);
    test_bool(cam->ortho, true);
    test_flt(cam->ortho_scale, 1);

    ecs_entity_t script = ecs_lookup(world, BOX_EDITOR_SCRIPT);
    test_assert(script != 0);
    const EcsScript *s = ecs_get(world, script, EcsScript);
    test_assert(s != NULL);
    test_assert(s->error == NULL);

    ecs_entity_t tmpl = ecs_lookup(world, BOX_EDITOR_TEMPLATE);
    test_assert(tmpl != 0);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has_id(world, panel, tmpl));
    test_assert(ecs_has(world, panel, InputBlocker));
    test_assert(ecs_has(world, panel, Rect));

    ecs_entity_t title = ecs_lookup(world, EDITOR_TITLE);
    test_assert(title != 0);
    const Text *t = ecs_get(world, title, Text);
    test_assert(t != NULL);
    test_str(t->value, "Entities");

    ecs_entity_t body = ecs_lookup(world, EDITOR_BODY);
    test_assert(body != 0);
    test_assert(ecs_has(world, body, ClipChildren));

    ecs_fini(world);
}

void Editor_close_removes_view(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);
    ecs_entity_t view = editor_check_view(world);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);

    editor_open(world, false);
    test_assert(!ecs_is_alive(world, view));
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);

    test_assert(ecs_is_alive(world, panel));
    test_assert(ecs_lookup(world, BOX_EDITOR_UI) != 0);

    ecs_fini(world);
}

void Editor_reopen(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);
    ecs_entity_t script = ecs_lookup(world, BOX_EDITOR_SCRIPT);
    editor_open(world, false);
    editor_open(world, true);

    editor_check_view(world);
    test_uint(ecs_lookup(world, BOX_EDITOR_PANEL), panel);
    test_uint(ecs_lookup(world, BOX_EDITOR_SCRIPT), script);
    test_assert(ecs_lookup(world, EDITOR_TITLE) != 0);

    editor_open(world, false);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);

    ecs_fini(world);
}

void Editor_open_from_script(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    test_int(ecs_script_run(world, "open_editor",
        "using box.editor\n"
        "$ {\n"
        "  Editor: {open: true}\n"
        "}\n", NULL), 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    editor_check_view(world);
    test_assert(ecs_lookup(world, EDITOR_TITLE) != 0);

    test_int(ecs_script_run(world, "close_editor",
        "using box.editor\n"
        "$ {\n"
        "  Editor: {open: false}\n"
        "}\n", NULL), 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);

    ecs_fini(world);
}

void Editor_open_from_command(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "edit", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    editor_check_view(world);

    ecs_os_free(box_command_run(world, s, "edit", NULL));
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);

    ecs_os_free(box_command_run(world, s, "edit on", NULL));
    ecs_progress(world, 0);
    editor_check_view(world);

    ecs_fini(world);
}

void Editor_docks_right(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);

    ecs_entity_t screen = ecs_lookup(world, BOX_EDITOR_SCREEN);
    test_int(ecs_get(world, screen, Layout)->h, AlignRight);
    const LayoutItem *item = ecs_get(world, ecs_lookup(world, BOX_EDITOR_PANEL), LayoutItem);
    test_int(item->width.mode, SizeModeFixed);
    test_flt(item->width.value, 0.25f * 1280);
    test_int(item->height.mode, SizeModeGrow);

    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, BOX_EDITOR_PANEL), LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, 320);
    test_flt(b->height, 720);

    ecs_fini(world);
}

void Editor_width_and_side(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);

    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->width = 0.5f;
    e->side = EditorSideLeft;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t screen = ecs_lookup(world, BOX_EDITOR_SCREEN);
    test_int(ecs_get(world, screen, Layout)->h, AlignLeft);
    const LayoutItem *item = ecs_get(world, ecs_lookup(world, BOX_EDITOR_PANEL), LayoutItem);
    test_flt(item->width.value, 640);

    e = ecs_singleton_get_mut(world, Editor);
    e->width = 2;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    item = ecs_get(world, ecs_lookup(world, BOX_EDITOR_PANEL), LayoutItem);
    test_flt(item->width.value, 1280);

    e = ecs_singleton_get_mut(world, Editor);
    e->width = -1;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    item = ecs_get(world, ecs_lookup(world, BOX_EDITOR_PANEL), LayoutItem);
    test_flt(item->width.value, 0);

    ecs_fini(world);
}

void Editor_rebuilt_after_delete(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);

    ecs_delete(world, ecs_lookup(world, BOX_EDITOR_UI));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    editor_check_view(world);
    test_assert(ecs_lookup(world, BOX_EDITOR_PANEL) != 0);
    test_assert(ecs_lookup(world, EDITOR_TITLE) != 0);

    ecs_delete(world, ecs_lookup(world, BOX_EDITOR_VIEW));
    ecs_progress(world, 0);
    editor_check_view(world);

    ecs_fini(world);
}

void Editor_close_clears_focus(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    editor_open(world, true);

    ecs_entity_t body = ecs_lookup(world, EDITOR_BODY);
    ecs_entity_t field = ecs_entity(world, { .name = "field", .parent = body });
    ecs_add(world, field, Focusable);
    ecs_entity_t game = ecs_entity(world, { .name = "game_button" });
    ecs_add(world, game, Focusable);
    ecs_progress(world, 0);

    test_uint(ecs_script_set_focus(world, field), field);
    editor_open(world, false);
    test_uint(ecs_script_get_focus(world), 0);

    editor_open(world, true);
    test_uint(ecs_script_set_focus(world, game), game);
    editor_open(world, false);
    test_uint(ecs_script_get_focus(world), game);

    ecs_fini(world);
}

void Editor_view_order_with_console(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    ecs_singleton_modified(world, Console);
    editor_open(world, true);

    ecs_entity_t editor = editor_check_view(world);
    ecs_entity_t console = ecs_lookup(world, BOX_CONSOLE_VIEW);
    test_assert(console != 0);
    test_assert(ecs_get(world, editor, View)->order <
        ecs_get(world, console, View)->order);

    ecs_fini(world);
}

/** Read the editor script of the repository from the working directory or an ancestor of it.
 *
 * @param fonts Receives the absolute path of etc/fonts next to it, PATH_MAX bytes
 * @return The code, NULL when it is not found
 */
static char* editor_read_script(
    char *fonts)
{
    char path[512] = BOX_EDITOR_SCRIPT_PATH;
    char *result = NULL;
    int i;
    for (i = 0; i < 8 && !result; i ++) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            result = ecs_os_malloc(size + 1);
            test_int((long)fread(result, 1, size, f), size);
            result[size] = '\0';
            fclose(f);
            snprintf(path + strlen(path) - strlen("editor/editor.flecs"),
                sizeof(path) - strlen(path), "fonts");
            test_assert(realpath(path, fonts) != NULL);
        } else {
            char prev[512];
            ecs_os_strcpy(prev, path);
            snprintf(path, sizeof(path), "../%s", prev);
        }
    }

    return result;
}

/** Write the editor script with the given title to etc/editor/editor.flecs of the working
 * directory, a copy of the script of the repository with another default title.
 *
 * @param src The script of the repository
 * @param title The default title
 */
static void editor_write_script(
    const char *src,
    const char *title)
{
    char *code = ecs_os_strdup(src);
    char *at = strstr(code, "\"Entities\"");
    test_assert(at != NULL);
    *at = '\0';
    FILE *f = fopen("etc/editor/editor.flecs", "w");
    test_assert(f != NULL);
    fprintf(f, "%s\"%s\"%s", code, title, at + strlen("\"Entities\""));
    fclose(f);
    ecs_os_free(code);
}

void Editor_script_reload(void) {
    // editor/overlay
    char cwd[4096], dir[] = "/tmp/box_editor_XXXXXX";
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    ecs_world_t *world = editor_world();
    ecs_singleton_set(world, FileWatch, { .interval = 0 });
    char fonts[PATH_MAX];
    char *src = editor_read_script(fonts);
    test_assert(src != NULL);

    test_assert(mkdtemp(dir) != NULL);
    test_int(chdir(dir), 0);
    test_int(mkdir("etc", 0700), 0);
    test_int(mkdir("etc/editor", 0700), 0);
    test_int(symlink(fonts, "etc/fonts"), 0);
    editor_write_script(src, "Before");

    editor_open(world, true);
    const Text *t = ecs_get(world, ecs_lookup(world, EDITOR_TITLE), Text);
    test_assert(t != NULL);
    test_str(t->value, "Before");

    editor_write_script(src, "After reload");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    t = ecs_get(world, ecs_lookup(world, EDITOR_TITLE), Text);
    test_assert(t != NULL);
    test_str(t->value, "After reload");
    test_assert(ecs_get(world, ecs_lookup(world, BOX_EDITOR_SCRIPT), EcsScript)->error == NULL);
    editor_check_view(world);

    editor_open(world, false);
    editor_write_script(src, "Closed");
    ecs_progress(world, 0);
    editor_open(world, true);
    ecs_progress(world, 0);
    t = ecs_get(world, ecs_lookup(world, EDITOR_TITLE), Text);
    test_str(t->value, "Closed");

    ecs_fini(world);
    ecs_os_free(src);
    test_int(remove("etc/editor/editor.flecs"), 0);
    test_int(rmdir("etc/editor"), 0);
    test_int(unlink("etc/fonts"), 0);
    test_int(rmdir("etc"), 0);
    test_int(chdir(cwd), 0);
    test_int(rmdir(dir), 0);
}

/** Warnings the log captured, one per line */
static char editor_warnings[8192];

/** Log function that keeps the warnings, so a test can count them.
 *
 * @param level The level of the message
 * @param file The file that logged it
 * @param line The line that logged it
 * @param msg The message
 */
static void editor_capture_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -2 &&
        strlen(editor_warnings) + strlen(msg) + 2 < sizeof(editor_warnings))
    {
        strcat(editor_warnings, msg);
        strcat(editor_warnings, "\n");
    }
}

/** Count the occurrences of a string in another.
 *
 * @param str The string to search
 * @param sub The string to count
 * @return The number of occurrences
 */
static int editor_count(
    const char *str,
    const char *sub)
{
    int count = 0;
    const char *at = str;
    while ((at = strstr(at, sub)) != NULL) {
        count ++;
        at += strlen(sub);
    }

    return count;
}

void Editor_script_missing_looked_for_on_open(void) {
    // editor/overlay
    char cwd[4096], dir[] = "/tmp/box_editor_XXXXXX";
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    ecs_world_t *world = editor_world();
    ecs_singleton_set(world, FileWatch, { .interval = 0 });
    char fonts[PATH_MAX];
    char *src = editor_read_script(fonts);
    test_assert(src != NULL);

    test_assert(mkdtemp(dir) != NULL);
    test_int(chdir(dir), 0);
    test_int(mkdir("etc", 0700), 0);
    test_int(symlink(fonts, "etc/fonts"), 0);

    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    ecs_os_api_log_t real_log = ecs_os_api.log_;
    editor_warnings[0] = '\0';
    ecs_os_api.log_ = editor_capture_log;

    editor_open(world, true);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    editor_check_view(world);
    test_assert(ecs_lookup(world, BOX_EDITOR_PANEL) != 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_SCRIPT) == 0);
    test_int(editor_count(editor_warnings, "not found, the editor is empty"), 1);

    test_int(mkdir("etc/editor", 0700), 0);
    editor_write_script(src, "Found");
    editor_open(world, false);
    editor_open(world, true);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_SCRIPT) != 0);
    const Text *t = ecs_get(world, ecs_lookup(world, EDITOR_TITLE), Text);
    test_assert(t != NULL);
    test_str(t->value, "Found");

    ecs_fini(world);
    ecs_os_free(src);
    test_int(remove("etc/editor/editor.flecs"), 0);
    test_int(rmdir("etc/editor"), 0);
    test_int(unlink("etc/fonts"), 0);
    test_int(rmdir("etc"), 0);
    test_int(chdir(cwd), 0);
    test_int(rmdir(dir), 0);
}

/** Check that the editor screen covers a window and the docked panel follows its size.
 *
 * @param world The world
 * @param w Width of the window in points
 * @param h Height of the window in points
 */
static void editor_check_window(
    ecs_world_t *world,
    float w,
    float h)
{
    const LayoutBounds *screen = ecs_get(world, ecs_lookup(world, BOX_EDITOR_SCREEN), LayoutBounds);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);
    test_assert(screen != NULL);
    test_flt(screen->width, w);
    test_flt(screen->height, h);
    test_flt(ecs_get(world, panel, LayoutItem)->width.value, 0.25f * w);
    const LayoutBounds *b = ecs_get(world, panel, LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, 0.25f * w);
    test_flt(b->height, h);
}

/** The editor fills the window after a resize, which only updates SurfaceStatus: the requested
 * Surface size stays the same. */
void Editor_follows_window_resize(void) {
    // editor/overlay
    ecs_world_t *world = editor_world();
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    editor_open(world, true);
    editor_check_window(world, 640, 360);

    int32_t sizes[][2] = {{640, 600}, {960, 800}, {400, 200}};
    for (int32_t i = 0; i < 3; i ++) {
        int32_t w = sizes[i][0], h = sizes[i][1];
        ecs_set(world, s, SurfaceStatus, {
            .width = w, .height = h, .point_width = w, .point_height = h,
            .render_width = w, .render_height = h, .window = true
        });
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        const Surface *surface = ecs_get(world, s, Surface);
        test_int(surface->width, 640);
        test_int(surface->height, 360);
        editor_check_window(world, (float)w, (float)h);
    }

    ecs_fini(world);
}

/** Run frames until the editor script handled the events of the last input.
 *
 * @param world The world
 */
static void editor_frames(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }
}

/** Get a member of a component of an entity through its reflection.
 *
 * @param world The world
 * @param e The entity
 * @param type The component
 * @param member Name of the member, an entity
 * @return The value of the member
 */
static ecs_entity_t editor_member(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t type,
    const char *member)
{
    test_assert(type != 0);
    const void *ptr = ecs_get_id(world, e, type);
    test_assert(ptr != NULL);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, type, ECS_CONST_CAST(void*, ptr));
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, member), 0);

    return ecs_meta_get_entity(&cur);
}

/** Get the selected entity, the mut selected of the panel.
 *
 * @param world The world
 * @return The selected entity, 0 when there is none
 */
static ecs_entity_t editor_selected(
    ecs_world_t *world)
{
    return editor_member(world, ecs_lookup(world, BOX_EDITOR_PANEL),
        ecs_lookup(world, BOX_EDITOR_TEMPLATE ".mut"), "selected");
}

/** Find the tree row of an entity below a parent of the editor content.
 *
 * @param world The world
 * @param parent Where to look
 * @param node The EntityTreeNode template
 * @param e The entity of the row
 * @return The EntityTreeNode instance of the entity, 0 when there is none
 */
static ecs_entity_t editor_find_node(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t node,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (!result && ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            ecs_entity_t child = it.entities[i];
            if (ecs_has_id(world, child, node) &&
                editor_member(world, child, node, "entity") == e)
            {
                result = child;
            } else {
                result = editor_find_node(world, child, node, e);
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Get the head of the tree row of an entity, the part that is clicked.
 *
 * @param world The world
 * @param e The entity of the row
 * @return The head of the row
 */
static ecs_entity_t editor_head(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t node = editor_find_node(world, ecs_lookup(world, EDITOR_TREE),
        ecs_lookup(world, "box.editor.EntityTreeNode"), e);
    test_assert(node != 0);
    ecs_entity_t result = ecs_lookup_child(world, node, "head");
    test_assert(result != 0);

    return result;
}

/** Click an entity of the editor with the left button, a press and a release on it.
 *
 * @param world The world
 * @param target The entity clicked
 */
static void editor_click(
    ecs_world_t *world,
    ecs_entity_t target)
{
    test_assert(target != 0);
    ecs_script_mouse_event(world, &(EcsScriptMouseEvent){ .target = target, .buttons = 1 });
    ecs_script_mouse_event(world, &(EcsScriptMouseEvent){ .target = target });
    editor_frames(world);
}

/** Get the text of a part of the inspector.
 *
 * @param world The world
 * @param part Name of the part, as header
 * @return The text, NULL when the part does not exist
 */
static const char* editor_inspector_text(
    ecs_world_t *world,
    const char *part)
{
    ecs_entity_t inspector = ecs_lookup(world, EDITOR_INSPECTOR);
    test_assert(inspector != 0);
    ecs_entity_t e = ecs_lookup_child(world, inspector, part);
    const Text *t = e ? ecs_get(world, e, Text) : NULL;

    return t ? t->value : NULL;
}

/** Check that a row is highlighted as selected with the selection color of the theme.
 *
 * @param world The world
 * @param head The head of the row
 * @param selected Whether it is selected
 */
static void editor_check_highlight(
    ecs_world_t *world,
    ecs_entity_t head,
    bool selected)
{
    const UiTheme *theme = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    const Rgb *c = ecs_get(world, head, Rgb);
    if (selected) {
        test_assert(c != NULL);
        test_int(c->r, theme->selection.r);
        test_int(c->g, theme->selection.g);
        test_int(c->b, theme->selection.b);
    } else {
        test_assert(c == NULL || c->r != theme->selection.r || c->g != theme->selection.g ||
            c->b != theme->selection.b);
    }
}

void Editor_inspector_empty_without_selection(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    editor_open(world, true);
    editor_frames(world);

    test_uint(editor_selected(world), 0);
    test_str(editor_inspector_text(world, "empty"), "No entity selected");
    test_assert(editor_inspector_text(world, "header") == NULL);

    ecs_entity_t inspector_view = ecs_lookup(world, BOX_EDITOR_PANEL ".inspector_view");
    test_assert(inspector_view != 0);
    test_assert(ecs_has(world, inspector_view, ClipChildren));

    ecs_fini(world);
}

void Editor_select_entity_from_tree(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    ecs_entity_t alpha = ecs_entity(world, { .name = "alpha" });
    ecs_entity_t beta = ecs_entity(world, { .name = "beta" });
    editor_open(world, true);
    editor_frames(world);

    editor_click(world, editor_head(world, alpha));
    test_uint(editor_selected(world), alpha);
    test_str(editor_inspector_text(world, "header"), "alpha");
    test_assert(editor_inspector_text(world, "parent_path") == NULL);
    test_assert(editor_inspector_text(world, "empty") == NULL);
    editor_check_highlight(world, editor_head(world, alpha), true);
    editor_check_highlight(world, editor_head(world, beta), false);

    // Clicking the selected row again keeps it selected
    editor_click(world, editor_head(world, alpha));
    test_uint(editor_selected(world), alpha);
    editor_check_highlight(world, editor_head(world, alpha), true);

    // Another row moves the selection
    editor_click(world, editor_head(world, beta));
    test_uint(editor_selected(world), beta);
    test_str(editor_inspector_text(world, "header"), "beta");
    editor_check_highlight(world, editor_head(world, alpha), false);
    editor_check_highlight(world, editor_head(world, beta), true);

    ecs_fini(world);
}

void Editor_select_child_shows_parent_path(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    ecs_entity_t parent = ecs_entity(world, { .name = "outer" });
    ecs_entity_t mid = ecs_entity(world, { .name = "mid", .parent = parent });
    ecs_entity_t child = ecs_entity(world, { .name = "inner", .parent = mid });
    editor_open(world, true);
    editor_frames(world);

    editor_click(world, ecs_lookup_child(world, editor_head(world, parent), "toggle"));
    editor_click(world, ecs_lookup_child(world, editor_head(world, mid), "toggle"));
    editor_click(world, editor_head(world, child));

    test_uint(editor_selected(world), child);
    test_str(editor_inspector_text(world, "header"), "inner");
    test_str(editor_inspector_text(world, "parent_path"), "outer.mid");
    editor_check_highlight(world, editor_head(world, child), true);

    editor_click(world, editor_head(world, mid));
    test_uint(editor_selected(world), mid);
    test_str(editor_inspector_text(world, "header"), "mid");
    test_str(editor_inspector_text(world, "parent_path"), "outer");

    ecs_fini(world);
}

void Editor_chevron_does_not_select(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    ecs_entity_t alpha = ecs_entity(world, { .name = "alpha" });
    ecs_entity_t parent = ecs_entity(world, { .name = "outer" });
    ecs_entity_t child = ecs_entity(world, { .name = "inner", .parent = parent });
    editor_open(world, true);
    editor_frames(world);

    // Without a selection the chevron opens the row and selects nothing
    ecs_entity_t toggle = ecs_lookup_child(world, editor_head(world, parent), "toggle");
    test_assert(toggle != 0);
    editor_click(world, toggle);
    test_uint(editor_selected(world), 0);
    test_assert(editor_head(world, child) != 0);
    test_str(editor_inspector_text(world, "empty"), "No entity selected");

    // With a selection it closes the row and keeps the selection
    editor_click(world, editor_head(world, alpha));
    test_uint(editor_selected(world), alpha);
    editor_click(world, ecs_lookup_child(world, editor_head(world, parent), "toggle"));
    test_uint(editor_selected(world), alpha);
    test_assert(editor_find_node(world, ecs_lookup(world, EDITOR_TREE),
        ecs_lookup(world, "box.editor.EntityTreeNode"), child) == 0);
    editor_check_highlight(world, editor_head(world, parent), false);

    // And opens it again
    editor_click(world, ecs_lookup_child(world, editor_head(world, parent), "toggle"));
    test_uint(editor_selected(world), alpha);
    test_assert(editor_head(world, child) != 0);
    test_str(editor_inspector_text(world, "header"), "alpha");

    ecs_fini(world);
}

/** Delete the entity of a row with its delete button, which shows while the row is hovered.
 *
 * @param world The world
 * @param e The entity of the row
 */
static void editor_delete_row(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t head = editor_head(world, e);
    ecs_script_mouse_event(world, &(EcsScriptMouseEvent){ .target = head });
    editor_frames(world);
    ecs_entity_t remove = ecs_lookup_child(world, editor_head(world, e), "remove");
    test_assert(remove != 0);
    editor_click(world, remove);
}

void Editor_delete_selected_clears_selection(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    ecs_entity_t alpha = ecs_entity(world, { .name = "alpha" });
    ecs_entity_t beta = ecs_entity(world, { .name = "beta" });
    ecs_entity_t parent = ecs_entity(world, { .name = "outer" });
    ecs_entity_t child = ecs_entity(world, { .name = "inner", .parent = parent });
    editor_open(world, true);
    editor_frames(world);

    // Deleting another entity keeps the selection
    editor_click(world, editor_head(world, alpha));
    test_uint(editor_selected(world), alpha);
    editor_delete_row(world, beta);
    test_assert(!ecs_is_alive(world, beta));
    test_uint(editor_selected(world), alpha);
    test_str(editor_inspector_text(world, "header"), "alpha");

    // Deleting the selected entity clears it
    editor_delete_row(world, alpha);
    test_assert(!ecs_is_alive(world, alpha));
    test_uint(editor_selected(world), 0);
    test_str(editor_inspector_text(world, "empty"), "No entity selected");

    // So does deleting an ancestor of it
    editor_click(world, ecs_lookup_child(world, editor_head(world, parent), "toggle"));
    editor_click(world, editor_head(world, child));
    test_uint(editor_selected(world), child);
    editor_delete_row(world, parent);
    test_assert(!ecs_is_alive(world, parent));
    test_assert(!ecs_is_alive(world, child));
    test_uint(editor_selected(world), 0);
    test_str(editor_inspector_text(world, "empty"), "No entity selected");

    ecs_fini(world);
}

void Editor_select_unnamed_entity(void) {
    // editor/inspector
    ecs_world_t *world = editor_world();
    ecs_entity_t e = ecs_new(world);
    editor_open(world, true);
    editor_frames(world);

    editor_click(world, editor_head(world, e));
    test_uint(editor_selected(world), e);
    char expect[32];
    snprintf(expect, sizeof(expect), "#%u", (uint32_t)e);
    test_str(editor_inspector_text(world, "header"), expect);
    test_assert(editor_inspector_text(world, "parent_path") == NULL);
    editor_check_highlight(world, editor_head(world, e), true);

    ecs_fini(world);
}
