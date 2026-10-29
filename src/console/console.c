/**
 * @file console/console.c
 * @brief Registers the box.console module: the Console singleton, the toggle action, the
 * keyboard capture and the system that opens, edits and draws the console.
 */

#define BOX_CONSOLE_IMPL
#include "console.h"
#include "../input/input.h"
#include <string.h>

ECS_COMPONENT_DECLARE(ConsoleHost);

/** Surface size in points the content is laid out for while the world has no Surface */
#define BOX_CONSOLE_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_CONSOLE_DEFAULT_HEIGHT (720.0f)

/** Construct Console values: closed, the left half of the surface, light text on a dark panel,
 * command lines colored in light tones, unknown names and extra arguments in the error color.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_console_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Console *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Console){
            .width = 0.5f,
            .height = 1.0f,
            .font_size = 16,
            .line_height = 20,
            .padding = 10,
            .scrollback = 1000,
            .background = {12, 16, 26, 240},
            .border = {80, 140, 220},
            .text = {215, 220, 230},
            .command = {120, 200, 255},
            .error = {255, 110, 100},
            .input = {255, 255, 255},
            .syntax = {
                .name = {110, 190, 255},
                .unknown = {255, 110, 100},
                .entity = {130, 220, 150},
                .missing = {255, 190, 80},
                .number = {200, 160, 255},
                .string = {235, 210, 140},
                .boolean = {255, 150, 200},
                .value = {140, 225, 225},
                .extra = {255, 110, 100}
            }
        };
    }
}

/** Get the console state of a world.
 *
 * @param world The world
 * @return The state, NULL when box.console is not imported
 */
static box_console_t* box_console_get(
    const ecs_world_t *world)
{
    const ConsoleHost *host = ecs_id(ConsoleHost) ?
        ecs_singleton_get(world, ConsoleHost) : NULL;

    return host ? host->con : NULL;
}

/** Input was written: while the console is open, take the typed and pasted text, the key presses
 * and the wheel, and release every key and button but those of the toggle so no other action
 * fires.
 *
 * @param it Iterator over the Input singleton
 */
static void box_console_capture(
    ecs_iter_t *it)
{
    box_console_t *con = it->ctx;
    Input *in = ecs_field(it, Input, 0);
    bool keep[BOX_KEY_COUNT] = {0};
    int32_t i, size;
    if (!con->shown || !it->count) {
        goto done;
    }

    box_input_action_keys(it->world, con->action, keep);
    con->clear = con->clear || (in->keys[KeyL].pressed &&
        (in->keys[KeyCtrlLeft].down || in->keys[KeyCtrlRight].down));
    for (i = KeyNone + 1; i < BOX_KEY_COUNT; i ++) {
        con->pressed[i] = con->pressed[i] || in->keys[i].pressed;
        if (!keep[i]) {
            in->keys[i] = (KeyState){0};
        }
    }

    size = in->text ? (int32_t)strlen(in->text) : 0;
    if (size) {
        memcpy(ecs_vec_grow_t(NULL, &con->typed, char, size), in->text, (size_t)size);
        in->text[0] = '\0';
    }

    size = in->paste ? (int32_t)strlen(in->paste) : 0;
    if (size) {
        memcpy(ecs_vec_grow_t(NULL, &con->typed, char, size), in->paste, (size_t)size);
    }

    ecs_os_free(in->paste);
    in->paste = NULL;
    con->wheel += in->mouse.scroll[1];
    in->mouse.scroll[0] = in->mouse.scroll[1] = 0;
done:
    return;
}

/** Console was set: lay the content out again for the new look.
 *
 * @param it Iterator over the Console singleton
 */
static void box_console_on_set(
    ecs_iter_t *it)
{
    box_console_t *con = it->ctx;
    con->relayout = true;
}

/** Forget the input collected since the last update.
 *
 * @param con The console state
 */
static void box_console_reset_input(
    box_console_t *con)
{
    ecs_os_zeromem(&con->pressed);
    ecs_vec_clear(&con->typed);
    con->wheel = 0;
    con->clear = false;
}

/** Enable the toggle action while the world has a Surface to show the console on.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_enable(
    ecs_world_t *world,
    box_console_t *con)
{
    bool surface = ecs_query_is_true(con->surfaces);
    ActionState *state;
    if (surface == con->enabled || !ecs_is_alive(world, con->action)) {
        goto done;
    }

    state = ecs_get_mut(world, con->action, ActionState);
    if (state) {
        ecs_os_zeromem(state);
    }

    ecs_enable(world, con->action, surface);
    con->enabled = surface;
done:
    return;
}

/** Find the size in points of the surface the console shows on: the first one that does not
 * render into a texture.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_measure(
    ecs_world_t *world,
    box_console_t *con)
{
    float w = BOX_CONSOLE_DEFAULT_WIDTH, h = BOX_CONSOLE_DEFAULT_HEIGHT;
    ecs_entity_t surface = 0;
    bool found = false;
    ecs_iter_t it = ecs_query_iter(world, con->surfaces);
    int32_t i;
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count && !found; i ++) {
            if (!s[i].texture) {
                w = (float)s[i].width;
                h = (float)s[i].height;
                surface = it.entities[i];
                found = true;
            }
        }
    }

    if (surface != con->surface) {
        con->surface = surface;
        con->relayout = true;
    }

    if (w != con->width || h != con->height) {
        con->width = w;
        con->height = h;
        con->relayout = true;
    }
}

/** Create the view of the console, a view without clear, lighting, culling or effects that
 * renders after every other view at BOX_VIEW_ORDER_CONSOLE.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_view_create(
    ecs_world_t *world,
    box_console_t *con)
{
    con->view = ecs_entity(world, { .name = "view", .parent = box_console_scope(world, con) });
    ecs_set(world, con->view, View, {
        .camera = con->ui.camera,
        .order = BOX_VIEW_ORDER_CONSOLE
    });
}

/** Show the console: build its content when needed and create its view.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_show(
    ecs_world_t *world,
    box_console_t *con)
{
    box_console_ui_build(world, con);
    box_console_view_create(world, con);
    con->shown = true;
    con->relayout = true;
    box_console_reset_input(con);
}

/** Hide the console: delete its view and give the keyboard back.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_hide(
    ecs_world_t *world,
    box_console_t *con)
{
    if (con->view && ecs_is_alive(world, con->view)) {
        ecs_delete(world, con->view);
    }

    con->view = 0;
    con->shown = false;
    box_console_reset_input(con);
}

/** Toggle, edit and draw the console after the actions of the frame resolved.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxConsoleUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_console_t *con = it->ctx;
    const ActionState *toggle;
    bool toggled, escape = false;
    Console *console;
    box_console_enable(world, con);
    toggle = con->enabled && ecs_is_alive(world, con->action) ?
        ecs_get(world, con->action, ActionState) : NULL;
    toggled = toggle && toggle->pressed;
    if (con->shown && !toggled) {
        escape = box_console_edit(world, con);
    }

    box_console_reset_input(con);
    console = ecs_singleton_get_mut(world, Console);
    if (!console) {
        goto done;
    }

    if (toggled || (escape && console->open)) {
        console->open = !console->open;
        ecs_singleton_modified(world, Console);
        console = ecs_singleton_get_mut(world, Console);
    }

    if (console->open && !con->shown) {
        box_console_show(world, con);
    } else if (!console->open && con->shown) {
        box_console_hide(world, con);
    }

    if (con->shown && (!ecs_is_alive(world, con->view) || !ecs_is_alive(world, con->ui.root))) {
        box_console_hide(world, con);
        box_console_show(world, con);
    }

    if (con->shown) {
        box_console_measure(world, con);
        box_console_ui_sync(world, con, console);
    }

done:
    return;
}

/** Script function box.console.run(line): run a line in the console and return its output.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The line
 * @param result Receives the output
 */
static void box_console_run_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *line = *(const char**)argv[0].ptr;
    (void)argc;
    *(char**)result->ptr = box_console_run(ctx->world, line ? line : "", NULL);
}

char* box_console_run(
    ecs_world_t *world,
    const char *line,
    bool *error)
{
    ecs_world_t *real = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    box_console_t *con = box_console_get(real);
    char *result;
    if (!con) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR "box.console is not imported");
        if (error) {
            error[0] = true;
        }

        goto done;
    }

    result = box_console_exec(real, con, line, error);
done:
    return result;
}

ecs_entity_t box_console_scope(
    ecs_world_t *world,
    box_console_t *con)
{
    if (!con->scope || !ecs_is_alive(world, con->scope)) {
        ecs_entity_t prev = ecs_set_scope(world, 0);
        con->scope = ecs_entity(world, { .name = BOX_CONSOLE_SCOPE });
        ecs_set_scope(world, prev);
    }

    return con->scope;
}

/** Free the console state when the world is deleted.
 *
 * @param world The world
 * @param ctx The console state
 */
static void box_console_free(
    ecs_world_t *world,
    void *ctx)
{
    box_console_t *con = ctx;
    (void)world;
    box_console_edit_fini(con);
    ecs_os_free(con);
}

/** Create the toggle action, bound to the grave key and disabled until there is a Surface.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_action(
    ecs_world_t *world,
    box_console_t *con)
{
    con->action = ecs_entity(world, { .name = "toggle", .parent = con->module });
    ecs_set(world, con->action, Action, {
        .description = ECS_CONST_CAST(char*, "Open and close the console") });
    ecs_set(world, con->action, Binding, { .key = KeyGrave });
    ecs_enable(world, con->action, false);
}

void BoxConsoleImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxConsole);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxCommand);

    ECS_META_COMPONENT(world, ConsoleSyntax);
    ECS_META_COMPONENT(world, Console);
    ecs_set_hooks(world, Console, { .ctor = box_console_ctor });
    ECS_COMPONENT_DEFINE(world, ConsoleHost);
    ecs_set_hooks(world, ConsoleHost, { .ctor = flecs_default_ctor });

    box_console_t *con = ecs_os_calloc_t(box_console_t);
    con->module = ecs_id(BoxConsole);
    con->line = ecs_os_strdup("");
    ecs_vec_init_t(NULL, &con->lines, box_console_line_t, 0);
    ecs_vec_init_t(NULL, &con->history, char*, 0);
    ecs_vec_init_t(NULL, &con->ui.lines, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &con->typed, char, 0);
    ecs_vec_init_t(NULL, &con->spans, box_console_span_t, 0);
    ecs_singleton_set(world, ConsoleHost, { con });
    ecs_atfini(world, box_console_free, con);
    box_console_action(world, con);
    ecs_singleton_add(world, Console);

    con->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ConsoleSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ConsoleCapture" }),
        .query.terms = {{ .id = ecs_id(Input), .inout = EcsInOut }},
        .events = { EcsOnSet },
        .callback = box_console_capture,
        .ctx = con
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ConsoleOnSet" }),
        .query.terms = {{ .id = ecs_id(Console), .inout = EcsInOutNone }},
        .events = { EcsOnSet },
        .callback = box_console_on_set,
        .ctx = con
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ConsoleUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxConsoleUpdate,
        .ctx = con,
        .immediate = true
    });

    ecs_function(world, {
        .name = "run",
        .parent = con->module,
        .return_type = ecs_id(ecs_string_t),
        .params = {{ .name = "line", .type = ecs_id(ecs_string_t) }},
        .callback = box_console_run_function
    });
}
