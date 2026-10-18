/**
 * @file command/cmd_input.c
 * @brief Input commands: key, mouse, input and action commands that feed the synthetic queue,
 * and the clipboard commands.
 */

#include "command.h"
#include "../input/input.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/** Get the argument of a parameter as a string.
 *
 * @param argv The arguments
 * @param i Index of the argument
 * @return The string, NULL when the argument was omitted
 */
static const char* box_cmd_input_str(
    const ecs_value_t *argv,
    int32_t i)
{
    return *(char**)argv[i].ptr;
}

/** Get the argument of a parameter as an i32.
 *
 * @param argv The arguments
 * @param i Index of the argument
 * @return The value, 0 when the argument was omitted
 */
static int32_t box_cmd_input_i32(
    const ecs_value_t *argv,
    int32_t i)
{
    return *(ecs_i32_t*)argv[i].ptr;
}

/** Get the argument of a parameter as a bool.
 *
 * @param argv The arguments
 * @param i Index of the argument
 * @return The value, false when the argument was omitted
 */
static bool box_cmd_input_bool(
    const ecs_value_t *argv,
    int32_t i)
{
    return *(ecs_bool_t*)argv[i].ptr;
}

/** Fail the command when the world has no box.input module.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @return 0 when box.input is imported, -1 otherwise
 */
static int box_cmd_input_ready(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result)
{
    int ret = 0;
    if (!ecs_lookup(ctx->world, "box.input.Input")) {
        box_command_error(ctx, result, "box.input is not imported");
        ret = -1;
    }

    return ret;
}

/** Parse a position such as [10, 20], {10 20} or 10,20.
 *
 * @param str The position
 * @param out Receives the position
 * @return 0 on success, -1 when the string is not a position
 */
static int box_cmd_input_parse_pos(
    const char *str,
    vec2 out)
{
    const char *p = str;
    char *end = NULL;
    int32_t i;
    int result = -1;
    while (p[0] == ' ' || p[0] == '[' || p[0] == '{' || p[0] == '(') {
        p ++;
    }

    for (i = 0; i < 2; i ++) {
        out[i] = strtof(p, &end);
        if (end == p) {
            goto done;
        }

        p = end;
        while (p[0] == ' ' || (!i && p[0] == ',')) {
            p ++;
        }
    }

    while (p[0] == ' ' || p[0] == ']' || p[0] == '}' || p[0] == ')') {
        p ++;
    }

    result = p[0] ? -1 : 0;
done:
    return result;
}

/** Convert a position argument, failing the command when it is not a position.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param name Name of the parameter
 * @param str The argument
 * @param out Receives the position
 * @return 0 on success, -1 when the command failed
 */
static int box_cmd_input_pos(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *name,
    const char *str,
    vec2 out)
{
    int ret = box_cmd_input_parse_pos(str, out);
    if (ret) {
        box_command_error(ctx, result, "%s: invalid position '%s'", name, str);
    }

    return ret;
}

/** Fail the command when a frame or step count is negative or above its maximum.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param name Name of the parameter
 * @param frames The count
 * @param max The largest valid count
 * @return 0 when the count is valid, -1 when the command failed
 */
static int box_cmd_input_count(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *name,
    int32_t frames,
    int32_t max)
{
    int ret = 0;
    if (frames < 0) {
        box_command_error(ctx, result, "%s: negative frame count %d", name, frames);
        ret = -1;
    } else if (frames > max) {
        box_command_error(ctx, result, "%s: %d is out of range, 0 to %d", name, frames, max);
        ret = -1;
    }

    return ret;
}

/** Fail the command when a frame count is negative or above BOX_COMMAND_FRAMES_MAX.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param name Name of the parameter
 * @param frames The frame count
 * @return 0 when the count is valid, -1 when the command failed
 */
static int box_cmd_input_frames(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *name,
    int32_t frames)
{
    return box_cmd_input_count(ctx, result, name, frames, BOX_COMMAND_FRAMES_MAX);
}

/** Convert a key argument, failing the command when no key has that name.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param name The key name
 * @return The key, KeyNone when the command failed
 */
static Key box_cmd_input_key(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *name)
{
    Key key = box_input_key_from_name(name);
    if (!key) {
        box_command_error(ctx, result, "unknown key '%s'", name);
    }

    return key;
}

/** Compare two strings without regard to case.
 *
 * @param a The first string
 * @param b The second string
 * @return Whether the strings are equal ignoring case
 */
static bool box_cmd_input_iequal(
    const char *a,
    const char *b)
{
    while (a[0] && tolower((unsigned char)a[0]) == tolower((unsigned char)b[0])) {
        a ++;
        b ++;
    }

    return !a[0] && !b[0];
}

/** Convert a mouse button argument: left, middle, right or a key name of a button.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param name The button name
 * @return The button, KeyNone when the command failed
 */
static Key box_cmd_input_button(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *name)
{
    Key key = KeyNone;
    if (box_cmd_input_iequal(name, "left")) {
        key = KeyMouseLeft;
    } else if (box_cmd_input_iequal(name, "middle")) {
        key = KeyMouseMiddle;
    } else if (box_cmd_input_iequal(name, "right")) {
        key = KeyMouseRight;
    } else {
        key = box_input_key_from_name(name);
    }

    if (key != KeyMouseLeft && key != KeyMouseMiddle && key != KeyMouseRight) {
        box_command_error(ctx, result, "unknown button '%s'", name);
        key = KeyNone;
    }

    return key;
}

/** Create a query for the actions that are not prefabs or disabled.
 *
 * @param world The world
 * @return The query, which the caller deletes
 */
static ecs_query_t* box_cmd_input_actions(
    ecs_world_t *world)
{
    return ecs_query(world, {
        .terms = {{ .id = ecs_id(Action) }},
        .cache_kind = EcsQueryCacheNone
    });
}

/** Find the actions with a name.
 *
 * @param world The world
 * @param name The name
 * @param count Receives the number of actions with that name
 * @return The last action found, 0 when there is none
 */
static ecs_entity_t box_cmd_input_action_named(
    ecs_world_t *world,
    const char *name,
    int32_t *count)
{
    ecs_query_t *q = box_cmd_input_actions(world);
    ecs_iter_t it = ecs_query_iter(world, q);
    ecs_entity_t result = 0;
    int32_t i;
    count[0] = 0;
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            const char *n = ecs_get_name(world, it.entities[i]);
            if (n && !strcmp(n, name)) {
                result = it.entities[i];
                count[0] ++;
            }
        }
    }

    ecs_query_fini(q);

    return result;
}

/** Resolve an action argument: a path from the current entity or its parents, else a name.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param session The session entity
 * @param name The action path or name
 * @return The action, 0 when the command failed
 */
static ecs_entity_t box_cmd_input_action(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *name)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t cwd = box_terminal_cwd(world, session);
    ecs_entity_t e = ecs_lookup_path_w_sep(world, cwd, name, ".", NULL, true);
    ecs_entity_t action = 0;
    int32_t count = 0;
    if (e && ecs_has(world, e, Action)) {
        action = e;
        goto done;
    }

    action = box_cmd_input_action_named(world, name, &count);
    if (count > 1) {
        box_command_error(ctx, result, "ambiguous action '%s', %d match", name, count);
        action = 0;
    } else if (!count && e) {
        box_command_error(ctx, result, "'%s' is not an action", name);
    } else if (!count) {
        box_command_error(ctx, result, "no action '%s'", name);
    }

done:
    return action;
}

/** Fill a walk from its from, to, steps and dur arguments.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param argv The arguments
 * @param first Index of the from argument
 * @param walk Receives the walk
 * @return 0 on success, -1 when the command failed
 */
static int box_cmd_input_walk(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const ecs_value_t *argv,
    int32_t first,
    box_input_sweep_t *walk)
{
    int ret = -1;
    walk->steps = box_cmd_input_i32(argv, first + 2);
    walk->frames = box_cmd_input_i32(argv, first + 3);
    if (box_cmd_input_pos(ctx, result, "from", box_cmd_input_str(argv, first), walk->from) ||
        box_cmd_input_pos(ctx, result, "to", box_cmd_input_str(argv, first + 1), walk->to) ||
        box_cmd_input_count(ctx, result, "steps", walk->steps, BOX_COMMAND_STEPS_MAX) ||
        box_cmd_input_frames(ctx, result, "dur", walk->frames))
    {
        goto done;
    }

    ret = 0;
done:
    return ret;
}

/** The key.press command: tap a key for dur frames, one when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the key and the duration
 * @param result Receives the output
 */
static void box_cmd_key_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    int32_t frames = box_cmd_input_i32(argv, 2);
    Key key;
    if (box_cmd_input_ready(ctx, result) || box_cmd_input_frames(ctx, result, "dur", frames)) {
        goto done;
    }

    key = box_cmd_input_key(ctx, result, box_cmd_input_str(argv, 1));
    if (key) {
        box_input_key_press(ctx->world, key, frames);
    }

done:
    return;
}

/** The key.down and key.up commands: a key goes down or comes up.
 *
 * @param ctx The call context, ctx->ctx is non NULL for key.down
 * @param argc Number of arguments
 * @param argv The session and the key
 * @param result Receives the output
 */
static void box_cmd_key_down_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    Key key;
    if (box_cmd_input_ready(ctx, result)) {
        goto done;
    }

    key = box_cmd_input_key(ctx, result, box_cmd_input_str(argv, 1));
    if (key) {
        box_input_key(ctx->world, key, ctx->ctx != NULL);
    }

done:
    return;
}

/** The key.repeat command: count repeats of a held key, one when omitted, dur frames apart.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the key, the count and the spacing
 * @param result Receives the output
 */
static void box_cmd_key_repeat(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    int32_t count = box_cmd_input_i32(argv, 2);
    int32_t frames = box_cmd_input_i32(argv, 3);
    int32_t i;
    Key key;
    if (box_cmd_input_ready(ctx, result) ||
        box_cmd_input_count(ctx, result, "count", count, BOX_COMMAND_STEPS_MAX) ||
        box_cmd_input_frames(ctx, result, "dur", frames))
    {
        goto done;
    }

    key = box_cmd_input_key(ctx, result, box_cmd_input_str(argv, 1));
    for (i = 0; key && i < (count > 0 ? count : 1); i ++) {
        box_input_queue(ctx->world, &(InputEvent){
            .kind = InputKeyDown,
            .key = key,
            .repeat = true,
            .delay = frames > 0 ? frames : 1
        });
    }

done:
    return;
}

/** The key.type command: one key press per character, each held for dur frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the text and the duration
 * @param result Receives the output
 */
static void box_cmd_key_type(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    int32_t frames = box_cmd_input_i32(argv, 2);
    if (!box_cmd_input_ready(ctx, result) &&
        !box_cmd_input_frames(ctx, result, "dur", frames))
    {
        box_input_key_type(ctx->world, box_cmd_input_str(argv, 1), frames);
    }
}

/** The mouse.move command: pin the cursor at a position.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the position and whether it is normalized
 * @param result Receives the output
 */
static void box_cmd_mouse_move(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    vec2 pos;
    if (!box_cmd_input_ready(ctx, result) &&
        !box_cmd_input_pos(ctx, result, "pos", box_cmd_input_str(argv, 1), pos))
    {
        box_input_mouse_move(ctx->world, pos[0], pos[1], box_cmd_input_bool(argv, 2));
    }
}

/** The mouse.sweep command: walk the cursor from one position to another over frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, from, to, steps and dur
 * @param result Receives the output
 */
static void box_cmd_mouse_sweep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_input_sweep_t walk = {0};
    if (!box_cmd_input_ready(ctx, result) &&
        !box_cmd_input_walk(ctx, result, argv, 1, &walk))
    {
        box_input_mouse_sweep(ctx->world, &walk);
    }
}

/** The mouse.release command: unpin the cursor so the real pointer moves it again.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_mouse_release(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx, result)) {
        box_input_mouse_release(ctx->world);
    }
}

/** The mouse.button command: a mouse button goes down or comes up.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the button and whether it goes down
 * @param result Receives the output
 */
static void box_cmd_mouse_button(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    Key button;
    if (box_cmd_input_ready(ctx, result)) {
        goto done;
    }

    button = box_cmd_input_button(ctx, result, box_cmd_input_str(argv, 1));
    if (button) {
        box_input_key(ctx->world, button, box_cmd_input_bool(argv, 2));
    }

done:
    return;
}

/** The mouse.click command: click a button at a position, or where the cursor is.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the button, the position (- for none) and the duration
 * @param result Receives the output
 */
static void box_cmd_mouse_click(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *str = box_cmd_input_str(argv, 2);
    int32_t frames = box_cmd_input_i32(argv, 3);
    bool has_pos = str && strcmp(str, "-");
    vec2 pos = {0};
    Key button;
    if (box_cmd_input_ready(ctx, result) ||
        (has_pos && box_cmd_input_pos(ctx, result, "pos", str, pos)) ||
        box_cmd_input_frames(ctx, result, "dur", frames))
    {
        goto done;
    }

    button = box_cmd_input_button(ctx, result, box_cmd_input_str(argv, 1));
    if (button) {
        box_input_mouse_click(ctx->world, button, has_pos ? pos : NULL, false, frames);
    }

done:
    return;
}

/** The mouse.drag command: hold a button while the cursor walks from one position to another.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the button, from, to, steps and dur
 * @param result Receives the output
 */
static void box_cmd_mouse_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_input_sweep_t walk = {0};
    if (box_cmd_input_ready(ctx, result) || box_cmd_input_walk(ctx, result, argv, 2, &walk)) {
        goto done;
    }

    walk.button = box_cmd_input_button(ctx, result, box_cmd_input_str(argv, 1));
    if (walk.button) {
        box_input_mouse_drag(ctx->world, &walk);
    }

done:
    return;
}

/** The mouse.scroll command: move the wheel.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, dx and dy
 * @param result Receives the output
 */
static void box_cmd_mouse_scroll(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    if (!box_cmd_input_ready(ctx, result)) {
        box_input_mouse_scroll(ctx->world,
            (float)*(ecs_f64_t*)argv[1].ptr, (float)*(ecs_f64_t*)argv[2].ptr);
    }
}

/** The input.reset command: empty the queue and release every key, action and the cursor.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_input_reset(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx, result)) {
        box_input_reset(ctx->world);
    }
}

/** The input.focus command: gain or lose focus, losing it releases everything held.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and whether input is focused
 * @param result Receives the output
 */
static void box_cmd_input_focus(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    if (!box_cmd_input_ready(ctx, result)) {
        box_input_focus(ctx->world, box_cmd_input_bool(argv, 1));
    }
}

/** The input.wait command: return the frames until the queue is drained, 0 when it is.
 *
 * Replies are synchronous, so the command does not block; an agent repeats it until 0.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_input_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx, result)) {
        *(char**)result->ptr = flecs_asprintf("%d", box_input_pending_frames(ctx->world));
    }
}

/** The clipboard.set command: put the rest of the line on the clipboard.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the text
 * @param result Receives the output
 */
static void box_cmd_clipboard_set(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    if (!box_cmd_input_ready(ctx, result)) {
        box_input_clipboard_set(ctx->world, box_cmd_input_str(argv, 1));
    }
}

/** The clipboard.get command: print the text on the clipboard.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_clipboard_get(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx, result)) {
        *(char**)result->ptr = ecs_os_strdup(box_input_clipboard_get(ctx->world));
    }
}

/** The action.press command: hold an action for dur frames, one when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the action and the duration
 * @param result Receives the output
 */
static void box_cmd_action_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    int32_t frames = box_cmd_input_i32(argv, 2);
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx, result) || box_cmd_input_frames(ctx, result, "dur", frames)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, result, session, box_cmd_input_str(argv, 1));
    if (action) {
        box_input_action_press(ctx->world, action, frames);
    }

done:
    return;
}

/** The action.down and action.up commands: a synthetic hold of an action starts or ends.
 *
 * @param ctx The call context, ctx->ctx is non NULL for action.down
 * @param argc Number of arguments
 * @param argv The session and the action
 * @param result Receives the output
 */
static void box_cmd_action_down_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx, result)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, result, session, box_cmd_input_str(argv, 1));
    if (action) {
        box_input_action(ctx->world, action, ctx->ctx != NULL);
    }

done:
    return;
}

/** The action.drag command: hold an action while the cursor walks between two positions.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the action, from, to, steps and dur
 * @param result Receives the output
 */
static void box_cmd_action_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    box_input_sweep_t walk = {0};
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx, result) || box_cmd_input_walk(ctx, result, argv, 2, &walk)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, result, session, box_cmd_input_str(argv, 1));
    if (action) {
        box_input_action_drag(ctx->world, action, &walk);
    }

done:
    return;
}

/** Order two strings for qsort.
 *
 * @param a The first string
 * @param b The second string
 * @return The strcmp order of the strings
 */
static int box_cmd_input_strcmp(
    const void *a,
    const void *b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/** The action.list command: one line per action with its path, held state and description.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_action_list(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t lines = {0};
    ecs_query_t *q;
    ecs_iter_t it;
    int32_t i;
    if (box_cmd_input_ready(ctx, result)) {
        goto done;
    }

    q = box_cmd_input_actions(world);
    it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const Action *a = ecs_field(&it, Action, 0);
        for (i = 0; i < it.count; i ++) {
            const ActionState *s = ecs_get(world, it.entities[i], ActionState);
            char *path = ecs_get_path(world, it.entities[i]);
            bool desc = a[i].description && a[i].description[0];
            ecs_vec_append_t(NULL, &lines, char*)[0] = flecs_asprintf("%s%s%s%s", path,
                s && s->down ? " (down)" : "", desc ? ": " : "", desc ? a[i].description : "");
            ecs_os_free(path);
        }
    }

    ecs_query_fini(q);
    char **strs = ecs_vec_first_t(&lines, char*);
    if (strs) {
        qsort(strs, (size_t)ecs_vec_count(&lines), sizeof(char*), box_cmd_input_strcmp);
    }

    for (i = 0; i < ecs_vec_count(&lines); i ++) {
        if (i) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        ecs_strbuf_appendstr(&buf, strs[i]);
        ecs_os_free(strs[i]);
    }

    ecs_vec_fini_t(NULL, &lines, char*);
    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

/** Register the key commands.
 *
 * @param world The world
 */
static void box_cmd_input_keys(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "key.press",
        .brief = "Tap a key, held for dur frames (default 1)",
        .params = {
            { .name = "key", .type = ecs_id(ecs_string_t) },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_key_press
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "key.down",
        .brief = "Press a key until key.up",
        .params = {{ .name = "key", .type = ecs_id(ecs_string_t) }},
        .callback = box_cmd_key_down_up,
        .ctx = world
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "key.up",
        .brief = "Release a key",
        .params = {{ .name = "key", .type = ecs_id(ecs_string_t) }},
        .callback = box_cmd_key_down_up
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "key.repeat",
        .brief = "Repeat a held key count times (default 1), dur frames apart (default 1)",
        .params = {
            { .name = "key", .type = ecs_id(ecs_string_t) },
            { .name = "count", .type = ecs_id(ecs_i32_t), .optional = true },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_key_repeat
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "key.type",
        .brief = "Type text, one key press per character held for dur frames (default 1)",
        .params = {
            { .name = "text", .type = ecs_id(ecs_string_t) },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_key_type
    });
}

/** Register the mouse commands.
 *
 * @param world The world
 */
static void box_cmd_input_mouse(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.move",
        .brief = "Pin the cursor at [x, y], in 0..1 of the window when norm is true",
        .params = {
            { .name = "pos", .type = ecs_id(ecs_string_t) },
            { .name = "norm", .type = ecs_id(ecs_bool_t), .optional = true }
        },
        .callback = box_cmd_mouse_move
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.sweep",
        .brief = "Walk the cursor from [x, y] to [x, y] in steps (default 10) over dur frames",
        .params = {
            { .name = "from", .type = ecs_id(ecs_string_t) },
            { .name = "to", .type = ecs_id(ecs_string_t) },
            { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_mouse_sweep
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.release",
        .brief = "Unpin the cursor, the real pointer moves it again",
        .callback = box_cmd_mouse_release
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.button",
        .brief = "Press (true) or release (false) a button: left, middle or right",
        .params = {
            { .name = "button", .type = ecs_id(ecs_string_t) },
            { .name = "down", .type = ecs_id(ecs_bool_t) }
        },
        .callback = box_cmd_mouse_button
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.click",
        .brief = "Click a button at [x, y] (- or omitted: the cursor), held for dur frames",
        .params = {
            { .name = "button", .type = ecs_id(ecs_string_t) },
            { .name = "pos", .type = ecs_id(ecs_string_t), .optional = true },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_mouse_click
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.drag",
        .brief = "Hold a button while the cursor walks from [x, y] to [x, y]",
        .params = {
            { .name = "button", .type = ecs_id(ecs_string_t) },
            { .name = "from", .type = ecs_id(ecs_string_t) },
            { .name = "to", .type = ecs_id(ecs_string_t) },
            { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_mouse_drag
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mouse.scroll",
        .brief = "Move the wheel by dx, dy",
        .params = {
            { .name = "dx", .type = ecs_id(ecs_f64_t) },
            { .name = "dy", .type = ecs_id(ecs_f64_t) }
        },
        .callback = box_cmd_mouse_scroll
    });
}

/** Register the action commands.
 *
 * @param world The world
 */
static void box_cmd_input_action_cmds(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "action.press",
        .brief = "Hold an action for dur frames (default 1)",
        .params = {
            { .name = "action", .type = ecs_id(ecs_string_t) },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_action_press
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "action.down",
        .brief = "Hold an action until action.up",
        .params = {{ .name = "action", .type = ecs_id(ecs_string_t) }},
        .callback = box_cmd_action_down_up,
        .ctx = world
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "action.up",
        .brief = "Release a held action",
        .params = {{ .name = "action", .type = ecs_id(ecs_string_t) }},
        .callback = box_cmd_action_down_up
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "action.drag",
        .brief = "Hold an action while the cursor walks from [x, y] to [x, y]",
        .params = {
            { .name = "action", .type = ecs_id(ecs_string_t) },
            { .name = "from", .type = ecs_id(ecs_string_t) },
            { .name = "to", .type = ecs_id(ecs_string_t) },
            { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
            { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true }
        },
        .callback = box_cmd_action_drag
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "action.list",
        .brief = "List the actions: path, (down) when held, description",
        .callback = box_cmd_action_list
    });
}

void box_cmd_input_init(
    ecs_world_t *world)
{
    box_cmd_input_keys(world);
    box_cmd_input_mouse(world);

    box_command_register(world, &(box_command_desc_t){
        .name = "input.reset",
        .brief = "Empty the input queue and release every key, button, action and the cursor",
        .callback = box_cmd_input_reset
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "input.focus",
        .brief = "Gain (true) or lose (false) focus, platform input is ignored without it",
        .params = {{ .name = "focused", .type = ecs_id(ecs_bool_t) }},
        .callback = box_cmd_input_focus
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "input.wait",
        .brief = "Print the frames until the input queue is drained, 0 when it is; repeat until 0",
        .callback = box_cmd_input_wait
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "clipboard.set",
        .brief = "Put the rest of the line on the clipboard, which the paste shortcut pastes",
        .params = {{ .name = "text", .type = ecs_id(ecs_string_t), .rest = true }},
        .callback = box_cmd_clipboard_set
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "clipboard.get",
        .brief = "Print the text on the clipboard, of the platform or in memory",
        .callback = box_cmd_clipboard_get
    });

    box_cmd_input_action_cmds(world);
}
