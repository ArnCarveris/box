/**
 * @file ui/console.c
 * @brief The in game console: its binding, its session and the parts it fills.
 *
 * The module holds no widget, so the console is a script the framework installs
 * and the glue here is everything that script cannot do for itself. Adding the
 * Console singleton registers an action under the actions scope and an input
 * map that fires it with the grave key, so the overlay opens through the same
 * action layer a key binding and a UI button share.
 *
 * The first open imports box.command, loads the script and takes its two marked
 * elements: the one with a ConsoleLog shows the scrollback and the one with a
 * TextInput holds the line. A line the input submits is set as TerminalInput on
 * the session of the console, which is an ordinary session with a folder and a
 * log of its own, and the log of that session is what the scrollback shows, so
 * the overlay and an agent over HTTP see the same terminal.
 *
 * An open console holds the keyboard: every input map but its own is disabled
 * while it is open, which is the focus rule of the device layer applied to the
 * action layer, and a console that closes turns the view of its tree off and
 * gives the maps back.
 */

#include "ui_internal.h"
#include "../command/command_internal.h"
#include "../shader/shader_internal.h"

ECS_COMPONENT_DECLARE(ConsoleImpl);

/** Direct pointer to the console state of a world.
 *
 * @param world The world
 * @return The state, NULL while the world has no console
 */
static ConsoleImpl* box_ui_console_state(
    ecs_world_t *world)
{
    return ecs_id(ConsoleImpl) ?
        ecs_get_mut(world, ecs_id(ConsoleImpl), ConsoleImpl) : NULL;
}

/** Register the actions and the map that open the console with the grave key.
 *
 * The entities are named, so a script that wants to await the console finds
 * actions.Console and actions.ConsoleLine, and the binding lives in a map of
 * its own, which is the one map the console leaves enabled while it holds the
 * keyboard.
 *
 * @param world The world
 */
static void box_ui_console_bind(
    ecs_world_t *world)
{
    ecs_entity_t scope = ecs_set_scope(world, 0);
    ecs_entity_t actions = ecs_entity(world, { .name = "actions" });
    ecs_entity_t action = ecs_entity(world, {
        .name = "Console", .parent = actions });

    if (!ecs_has(world, action, InputAction)) {
        ecs_set(world, action, InputAction, { .description =
            ecs_os_strdup("Open and close the in game console") });
    }

    ecs_entity_t submit = ecs_entity(world, {
        .name = "ConsoleLine", .parent = actions });
    if (!ecs_has(world, submit, InputAction)) {
        ecs_set(world, submit, InputAction, { .description =
            ecs_os_strdup("A line was submitted in the console") });
    }

    ecs_entity_t map = ecs_entity(world, { .name = BOX_UI_CONSOLE_MAP });
    if (!ecs_has(world, map, InputMap)) {
        ecs_entity_t binding = ecs_entity(world, {
            .name = "grave", .parent = map });

        ecs_set(world, map, InputMap, { .enabled = true });
        ecs_set(world, binding, Binding, {
            .mask = (uint64_t)BoxKeyGrave, .action = action });
    }

    ecs_set_scope(world, scope);
}

/** Drop the grave characters the frame that flipped the console collected.
 *
 * The key that opens the overlay is not typed into it, but everything else that
 * arrived in the same frame is, so a line that starts in the frame the console
 * opens keeps its first character.
 *
 * @param world The world
 */
static void box_ui_console_swallow(
    ecs_world_t *world)
{
    Input *in = ecs_get_mut(world, ecs_id(Input), Input);
    int32_t i, out = 0;
    if (!in) {
        goto done;
    }

    for (i = 0; i < in->text_len; i ++) {
        if (in->text[i] != '`') {
            in->text[out] = in->text[i];
            out ++;
        }
    }

    in->text_len = out;
done:
    return;
}

/** Resolve the actions and the map of the console, installing them when missing.
 *
 * @param world The world
 * @param impl The console state
 */
static void box_ui_console_actions(
    ecs_world_t *world,
    ConsoleImpl *impl)
{
    if (impl->map) {
        goto done;
    }

    box_ui_console_bind(world);
    impl = box_ui_console_state(world);
    impl->action = ecs_lookup(world, "actions.Console");
    impl->submit = ecs_lookup(world, "actions.ConsoleLine");
    impl->map = ecs_lookup(world, BOX_UI_CONSOLE_MAP);
done:
    return;
}

/** Give the console the terminal session it runs its lines on.
 *
 * @param world The world
 * @return The session entity
 */
static ecs_entity_t box_ui_console_session(
    ecs_world_t *world)
{
    ecs_entity_t terminals = ecs_lookup(world, "terminals");
    ecs_entity_t result = ecs_entity(world, {
        .name = BOX_UI_CONSOLE_SESSION, .parent = terminals });

    if (!ecs_has(world, result, Terminal)) {
        ecs_add(world, result, Terminal);
    }

    return result;
}

/** Load the console script and take the two elements it marks.
 *
 * @param world The world
 * @param console The Console singleton
 * @param impl The console state
 */
static void box_ui_console_load(
    ecs_world_t *world,
    Console *console,
    ConsoleImpl *impl)
{
    char resolved[1024];
    if (!box_shader_resolve(BOX_UI_CONSOLE, resolved, sizeof(resolved))) {
        ecs_err("box.ui: no console script at '%s'", BOX_UI_CONSOLE);
        goto done;
    }

    if (!ecs_script(world, { .filename = resolved })) {
        ecs_err("box.ui: failed to load '%s'", resolved);
        goto done;
    }

    ecs_entity_t log = 0, input = 0, root = 0;
    ecs_iter_t it = ecs_each(world, ConsoleLog);
    while (ecs_each_next(&it) && !log) {
        if (it.count) {
            log = it.entities[0];
            root = box_ui_root(world, log);
        }
    }

    it = ecs_each(world, TextInput);
    while (ecs_each_next(&it) && !input) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (box_ui_root(world, it.entities[i]) == root) {
                input = it.entities[i];
                break;
            }
        }
    }

    console = ecs_get_mut(world, ecs_id(Console), Console);
    impl = box_ui_console_state(world);
    console->root = root;
    impl->log = log;
    impl->input = input;
    impl->loaded = true;
    if (!log || !input) {
        ecs_err("box.ui: '%s' has no ConsoleLog or no TextInput element",
            BOX_UI_CONSOLE);
    }

done:
    return;
}

/** Disable every input map but the one of the console, or give them back.
 *
 * @param world The world
 * @param impl The console state
 * @param hold Whether the console takes the keyboard
 */
static void box_ui_console_capture(
    ecs_world_t *world,
    ConsoleImpl *impl,
    bool hold)
{
    int32_t i, count;
    if (!hold) {
        count = ecs_vec_count(&impl->captured);
        for (i = 0; i < count; i ++) {
            ecs_entity_t map = ecs_vec_get_t(&impl->captured, ecs_entity_t, i)[0];
            InputMap *state = ecs_is_alive(world, map) ?
                ecs_get_mut(world, map, InputMap) : NULL;

            if (state) {
                state->enabled = true;
            }
        }

        ecs_vec_clear(&impl->captured);
        goto done;
    }

    ecs_iter_t it = ecs_each(world, InputMap);
    while (ecs_each_next(&it)) {
        const InputMap *map = ecs_field(&it, InputMap, 0);
        for (i = 0; i < it.count; i ++) {
            if (it.entities[i] == impl->map || !map[i].enabled) {
                continue;
            }

            ecs_vec_append_t(NULL, &impl->captured, ecs_entity_t)[0] =
                it.entities[i];
        }
    }

    impl = box_ui_console_state(world);
    count = ecs_vec_count(&impl->captured);
    for (i = 0; i < count; i ++) {
        ecs_get_mut(world, ecs_vec_get_t(
            &impl->captured, ecs_entity_t, i)[0], InputMap)->enabled = false;
    }

done:
    return;
}

/** Turn the view of the console tree on or off.
 *
 * @param world The world
 * @param root The root of the console tree
 * @param shown Whether the overlay is drawn
 */
static void box_ui_console_show(
    ecs_world_t *world,
    ecs_entity_t root,
    bool shown)
{
    const UiImpl *ui = root ? ecs_get(world, root, UiImpl) : NULL;
    View *view = ui && ui->view && ecs_is_alive(world, ui->view) ?
        ecs_get_mut(world, ui->view, View) : NULL;

    if (view) {
        view->enabled = shown;
        ecs_modified(world, ui->view, View);
    }
}

/** Open the console: import the commands, load the script and take the keyboard.
 *
 * @param world The world
 * @param console The Console singleton
 * @param impl The console state
 */
static void box_ui_console_open(
    ecs_world_t *world,
    Console *console,
    ConsoleImpl *impl)
{
    if (!ecs_id(Terminal)) {
        ecs_import_c(world, BoxCommandImport, "BoxCommand");
        console = ecs_get_mut(world, ecs_id(Console), Console);
        impl = box_ui_console_state(world);
    }

    if (!console->session || !ecs_is_alive(world, console->session)) {
        ecs_entity_t session = box_ui_console_session(world);

        console = ecs_get_mut(world, ecs_id(Console), Console);
        console->session = session;
        impl = box_ui_console_state(world);
    }

    if (!impl->loaded) {
        box_ui_console_load(world, console, impl);
        console = ecs_get_mut(world, ecs_id(Console), Console);
        impl = box_ui_console_state(world);
    }

    box_ui_console_show(world, console->root, true);
    box_ui_console_capture(world, impl, true);
    impl = box_ui_console_state(world);
    TextInput *input = impl->input && ecs_is_alive(world, impl->input) ?
        ecs_get_mut(world, impl->input, TextInput) : NULL;

    if (input) {
        input->focused = true;
    }
}

/** Close the console: hide its view, give the maps back and let the line go.
 *
 * @param world The world
 * @param console The Console singleton
 * @param impl The console state
 */
static void box_ui_console_close(
    ecs_world_t *world,
    Console *console,
    ConsoleImpl *impl)
{
    box_ui_console_show(world, console->root, false);
    box_ui_console_capture(world, impl, false);
    impl = box_ui_console_state(world);
    TextInput *input = impl->input && ecs_is_alive(world, impl->input) ?
        ecs_get_mut(world, impl->input, TextInput) : NULL;

    if (input) {
        input->focused = false;
    }
}

/** Run the line the input of the console submitted on its session.
 *
 * @param world The world
 * @param console The Console singleton
 * @param impl The console state
 */
static void box_ui_console_run(
    ecs_world_t *world,
    const Console *console,
    ConsoleImpl *impl)
{
    TextInput *input = impl->input && ecs_is_alive(world, impl->input) ?
        ecs_get_mut(world, impl->input, TextInput) : NULL;
    if (!input || !input->submitted || !input->submitted[0]) {
        goto done;
    }

    char *line = input->submitted;
    ecs_entity_t session = console->session, submit = impl->submit;
    ecs_entity_t source = impl->input;
    input->submitted = ecs_os_strdup("");

    ecs_set(world, session, TerminalInput, { .line = line });
    box_input_emit(world, submit, source);
done:
    return;
}

/** Write the last lines of the session log into the scrollback of the console.
 *
 * @param world The world
 * @param console The Console singleton
 * @param impl The console state
 */
static void box_ui_console_scrollback(
    ecs_world_t *world,
    const Console *console,
    ConsoleImpl *impl)
{
    const TerminalSession *s = console->session &&
        ecs_is_alive(world, console->session) ?
        ecs_get(world, console->session, TerminalSession) : NULL;
    const ConsoleLog *log = impl->log && ecs_is_alive(world, impl->log) ?
        ecs_get(world, impl->log, ConsoleLog) : NULL;
    if (!s || !log || s->count == impl->shown) {
        goto done;
    }

    int32_t lines = log->lines > 0 ? log->lines : BOX_UI_CONSOLE_LINES;
    if (lines > BOX_COMMAND_LOG_MAX) {
        lines = BOX_COMMAND_LOG_MAX;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i = box_ring_begin(s->count, lines, BOX_COMMAND_LOG_MAX);
    for (; i < s->count; i ++) {
        const box_command_entry_t *entry =
            &s->ring[box_ring_slot(i, BOX_COMMAND_LOG_MAX)];

        ecs_strbuf_append(&buf, "$ %s\n%s\n", entry->line,
            entry->result ? entry->result : "");
    }

    ecs_entity_t element = impl->log;
    int32_t count = s->count;
    ecs_set(world, element, Text, { .value = ecs_strbuf_get(&buf) });
    box_ui_console_state(world)->shown = count;
done:
    return;
}

void box_ui_console_update(
    ecs_world_t *world)
{
    Console *console = ecs_id(Console) ?
        ecs_get_mut(world, ecs_id(Console), Console) : NULL;
    if (!console) {
        goto done;
    }

    ConsoleImpl *impl = ecs_singleton_ensure(world, ConsoleImpl);
    box_ui_console_actions(world, impl);
    impl = box_ui_console_state(world);
    console = ecs_get_mut(world, ecs_id(Console), Console);
    const InputAction *action = ecs_get(world, impl->action, InputAction);
    if (action && action->pressed) {
        console->open = !console->open;
        box_ui_console_swallow(world);
    }

    if (console->open != impl->open) {
        bool open = console->open;

        if (open) {
            box_ui_console_open(world, console, impl);
        } else {
            box_ui_console_close(world, console, impl);
        }

        console = ecs_get_mut(world, ecs_id(Console), Console);
        impl = box_ui_console_state(world);
        impl->open = open;
    }

done:
    return;
}

void box_ui_console_sync(
    ecs_world_t *world)
{
    Console *console = ecs_id(Console) ?
        ecs_get_mut(world, ecs_id(Console), Console) : NULL;
    ConsoleImpl *impl = console && console->open ?
        box_ui_console_state(world) : NULL;
    if (!impl) {
        goto done;
    }

    box_ui_console_run(world, console, impl);
    console = ecs_get_mut(world, ecs_id(Console), Console);
    impl = box_ui_console_state(world);
    box_ui_console_scrollback(world, console, impl);
done:
    return;
}

/** ConsoleImpl constructor: a console that is closed and holds nothing.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_console_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ConsoleImpl *impl = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(ConsoleImpl));
    for (i = 0; i < count; i ++) {
        ecs_vec_init_t(NULL, &impl[i].captured, ecs_entity_t, 0);
    }
}

/** ConsoleImpl destructor: release the maps the console remembered.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_console_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ConsoleImpl *impl = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &impl[i].captured, ecs_entity_t);
    }
}

/** Console observer: install the binding as soon as the world gains a console.
 *
 * The actions are in place before the first frame reads the keyboard, which is
 * what lets a script await them and what makes the first press of the grave key
 * open the overlay.
 *
 * @param it Iterator over the world that gained a console
 */
static void box_ui_console_added(
    ecs_iter_t *it)
{
    box_ui_console_bind(it->world);
}

void box_ui_console_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, ConsoleImpl);

    ecs_set_hooks(world, ConsoleImpl, {
        .ctor = box_ui_console_ctor,
        .dtor = box_ui_console_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_add_pair(world, ecs_id(ConsoleImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ConsoleLog), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Console), .inout = EcsInOutNone }},
        .events = { EcsOnAdd },
        .callback = box_ui_console_added
    });
}
