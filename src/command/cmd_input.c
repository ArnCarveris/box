/**
 * @file command/cmd_input.c
 * @brief Input commands: key, mouse, input and action commands that feed the synthetic queue,
 * and the clipboard commands.
 *
 * The commands that queue input return the frames until the queue is drained, as input_wait.
 */

#include "command.h"
#include "../input/input.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Get the argument of a parameter as a string.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 * @param i Index of the argument
 * @return The string, NULL when the argument was omitted
 */
static const char* box_cmd_input_str(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t i)
{
    return i < argc ? *(char**)argv[i].ptr : NULL;
}

/** Get the argument of a parameter as an i32.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 * @param i Index of the argument
 * @return The value, 0 when the argument was omitted
 */
static int32_t box_cmd_input_i32(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t i)
{
    return i < argc ? *(ecs_i32_t*)argv[i].ptr : 0;
}

/** Get the argument of a parameter as a bool.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 * @param i Index of the argument
 * @return The value, false when the argument was omitted
 */
static bool box_cmd_input_bool(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t i)
{
    return i < argc ? *(ecs_bool_t*)argv[i].ptr : false;
}

/** Fail the command when the world has no box.input module.
 *
 * @param ctx The call context
 * @return 0 when box.input is imported, -1 otherwise
 */
static int box_cmd_input_ready(
    const ecs_function_ctx_t *ctx)
{
    int ret = 0;
    if (!ecs_lookup(ctx->world, "box.input.Input")) {
        ecs_err("box.input is not imported");
        ret = -1;
    }

    return ret;
}

/** Set the result of a command that queues input: the frames until the queue is drained.
 *
 * @param ctx The call context
 * @param result The result
 */
static void box_cmd_input_pending(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result)
{
    *(ecs_i32_t*)result->ptr = box_input_pending_frames(ctx->world);
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
 * @param name Name of the parameter
 * @param str The argument
 * @param out Receives the position
 * @return 0 on success, -1 when the command failed
 */
static int box_cmd_input_pos(
    const char *name,
    const char *str,
    vec2 out)
{
    int ret = box_cmd_input_parse_pos(str, out);
    if (ret) {
        ecs_err("%s: invalid position '%s'", name, str);
    }

    return ret;
}

/** Fail the command when a frame or step count is negative or above its maximum.
 *
 * @param name Name of the parameter
 * @param frames The count
 * @param max The largest valid count
 * @return 0 when the count is valid, -1 when the command failed
 */
static int box_cmd_input_count(
    const char *name,
    int32_t frames,
    int32_t max)
{
    int ret = 0;
    if (frames < 0) {
        ecs_err("%s: negative frame count %d", name, frames);
        ret = -1;
    } else if (frames > max) {
        ecs_err("%s: %d is out of range, 0 to %d", name, frames, max);
        ret = -1;
    }

    return ret;
}

/** Fail the command when a frame count is negative or above BOX_COMMAND_FRAMES_MAX.
 *
 * @param name Name of the parameter
 * @param frames The frame count
 * @return 0 when the count is valid, -1 when the command failed
 */
static int box_cmd_input_frames(
    const char *name,
    int32_t frames)
{
    return box_cmd_input_count(name, frames, BOX_COMMAND_FRAMES_MAX);
}

/** Convert a key argument, failing the command when no key has that name.
 *
 * @param name The key name
 * @return The key, KeyNone when the command failed
 */
static Key box_cmd_input_key(
    const char *name)
{
    Key key = box_input_key_from_name(name);
    if (!key) {
        ecs_err("unknown key '%s'", name);
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
 * @param name The button name
 * @return The button, KeyNone when the command failed
 */
static Key box_cmd_input_button(
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
        ecs_err("unknown button '%s'", name);
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

/** Resolve an action argument: a path from the session scope or its parents, else a name.
 *
 * @param ctx The call context
 * @param name The action path or name
 * @return The action, 0 when the command failed
 */
static ecs_entity_t box_cmd_input_action(
    const ecs_function_ctx_t *ctx,
    const char *name)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t cwd = box_command_scope(world);
    ecs_entity_t e = ecs_lookup_path_w_sep(world, cwd, name, ".", NULL, true);
    ecs_entity_t action = 0;
    int32_t count = 0;
    if (e && ecs_has(world, e, Action)) {
        action = e;
        goto done;
    }

    action = box_cmd_input_action_named(world, name, &count);
    if (count > 1) {
        ecs_err("ambiguous action '%s', %d match", name, count);
        action = 0;
    } else if (!count && e) {
        ecs_err("'%s' is not an action", name);
    } else if (!count) {
        ecs_err("no action '%s'", name);
    }

done:
    return action;
}

/** Fill a walk from its from, to, steps and dur arguments.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 * @param first Index of the from argument
 * @param walk Receives the walk
 * @return 0 on success, -1 when the command failed
 */
static int box_cmd_input_walk(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t first,
    box_input_sweep_t *walk)
{
    int ret = -1;
    walk->steps = box_cmd_input_i32(argc, argv, first + 2);
    walk->frames = box_cmd_input_i32(argc, argv, first + 3);
    if (box_cmd_input_pos("from", box_cmd_input_str(argc, argv, first), walk->from) ||
        box_cmd_input_pos("to", box_cmd_input_str(argc, argv, first + 1), walk->to) ||
        box_cmd_input_count("steps", walk->steps, BOX_COMMAND_STEPS_MAX) ||
        box_cmd_input_frames("dur", walk->frames))
    {
        goto done;
    }

    ret = 0;
done:
    return ret;
}

/** The key_press command: tap a key for dur frames, one when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The key and the duration
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_key_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t frames = box_cmd_input_i32(argc, argv, 1);
    Key key;
    if (box_cmd_input_ready(ctx) || box_cmd_input_frames("dur", frames)) {
        goto done;
    }

    key = box_cmd_input_key(box_cmd_input_str(argc, argv, 0));
    if (key) {
        box_input_key_press(ctx->world, key, frames);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The key_down and key_up commands: a key goes down or comes up.
 *
 * @param ctx The call context, ctx->ctx is non NULL for key_down
 * @param argc Number of arguments
 * @param argv The key
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_key_down_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    Key key;
    if (box_cmd_input_ready(ctx)) {
        goto done;
    }

    key = box_cmd_input_key(box_cmd_input_str(argc, argv, 0));
    if (key) {
        box_input_key(ctx->world, key, ctx->ctx != NULL);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The key_repeat command: count repeats of a held key, one when omitted, dur frames apart.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The key, the count and the spacing
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_key_repeat(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t count = box_cmd_input_i32(argc, argv, 1);
    int32_t frames = box_cmd_input_i32(argc, argv, 2);
    int32_t i;
    Key key;
    if (box_cmd_input_ready(ctx) ||
        box_cmd_input_count("count", count, BOX_COMMAND_STEPS_MAX) ||
        box_cmd_input_frames("dur", frames))
    {
        goto done;
    }

    key = box_cmd_input_key(box_cmd_input_str(argc, argv, 0));
    for (i = 0; key && i < (count > 0 ? count : 1); i ++) {
        box_input_queue(ctx->world, &(InputEvent){
            .kind = InputKeyDown,
            .key = key,
            .repeat = true,
            .delay = frames > 0 ? frames : 1
        });
    }

    if (key) {
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The key_type command: one key press per character, each held for dur frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The text and the duration
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_key_type(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t frames = box_cmd_input_i32(argc, argv, 1);
    if (!box_cmd_input_ready(ctx) &&
        !box_cmd_input_frames("dur", frames))
    {
        box_input_key_type(ctx->world, box_cmd_input_str(argc, argv, 0), frames);
        box_cmd_input_pending(ctx, result);
    }
}

/** The mouse_move command: pin the cursor at a position.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The position and whether it is normalized
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_move(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    vec2 pos;
    if (!box_cmd_input_ready(ctx) &&
        !box_cmd_input_pos("pos", box_cmd_input_str(argc, argv, 0), pos))
    {
        box_input_mouse_move(ctx->world, pos[0], pos[1], box_cmd_input_bool(argc, argv, 1));
        box_cmd_input_pending(ctx, result);
    }
}

/** The mouse_sweep command: walk the cursor from one position to another over frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv from, to, steps and dur
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_sweep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_input_sweep_t walk = {0};
    if (!box_cmd_input_ready(ctx) &&
        !box_cmd_input_walk(argc, argv, 0, &walk))
    {
        box_input_mouse_sweep(ctx->world, &walk);
        box_cmd_input_pending(ctx, result);
    }
}

/** The mouse_release command: unpin the cursor so the real pointer moves it again.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_release(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx)) {
        box_input_mouse_release(ctx->world);
        box_cmd_input_pending(ctx, result);
    }
}

/** The mouse_button command: a mouse button goes down or comes up.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The button and whether it goes down
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_button(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    Key button;
    if (box_cmd_input_ready(ctx)) {
        goto done;
    }

    button = box_cmd_input_button(box_cmd_input_str(argc, argv, 0));
    if (button) {
        box_input_key(ctx->world, button, box_cmd_input_bool(argc, argv, 1));
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The mouse_click command: click a button at a position, or where the cursor is.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The button, the position (- for none) and the duration
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_click(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *str = box_cmd_input_str(argc, argv, 1);
    int32_t frames = box_cmd_input_i32(argc, argv, 2);
    bool has_pos = str && strcmp(str, "-");
    vec2 pos = {0};
    Key button;
    if (box_cmd_input_ready(ctx) ||
        (has_pos && box_cmd_input_pos("pos", str, pos)) ||
        box_cmd_input_frames("dur", frames))
    {
        goto done;
    }

    button = box_cmd_input_button(box_cmd_input_str(argc, argv, 0));
    if (button) {
        box_input_mouse_click(ctx->world, button, has_pos ? pos : NULL, false, frames);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The mouse_drag command: hold a button while the cursor walks from one position to another.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The button, from, to, steps and dur
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_input_sweep_t walk = {0};
    if (box_cmd_input_ready(ctx) || box_cmd_input_walk(argc, argv, 1, &walk)) {
        goto done;
    }

    walk.button = box_cmd_input_button(box_cmd_input_str(argc, argv, 0));
    if (walk.button) {
        box_input_mouse_drag(ctx->world, &walk);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The mouse_scroll command: move the wheel.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv dx and dy
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_mouse_scroll(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    double dx = *(ecs_f64_t*)argv[0].ptr, dy = *(ecs_f64_t*)argv[1].ptr;
    if (!isfinite(dx) || !isfinite(dy)) {
        ecs_err("%s: %g is not a finite number", isfinite(dx) ? "dy" : "dx",
            isfinite(dx) ? dy : dx);
    } else if (!box_cmd_input_ready(ctx)) {
        box_input_mouse_scroll(ctx->world, (float)dx, (float)dy);
        box_cmd_input_pending(ctx, result);
    }
}

/** The input_reset command: empty the queue and release every key, action and the cursor.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_input_reset(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx)) {
        box_input_reset(ctx->world);
        box_cmd_input_pending(ctx, result);
    }
}

/** The input_focus command: gain or lose focus, losing it releases everything held.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and whether input is focused
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_input_focus(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (!box_cmd_input_ready(ctx)) {
        box_input_focus(ctx->world, box_cmd_input_bool(argc, argv, 0));
        box_cmd_input_pending(ctx, result);
    }
}

/** The input_wait command: return the frames until the queue is drained, 0 when it is.
 *
 * Replies are synchronous, so the command does not block; an agent repeats it until 0.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_input_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx)) {
        box_cmd_input_pending(ctx, result);
    }
}

/** The clipboard_set command: put text on the clipboard.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The text
 * @param result Unused
 */
static void box_cmd_clipboard_set(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)result;
    if (!box_cmd_input_ready(ctx)) {
        box_input_clipboard_set(ctx->world, box_cmd_input_str(argc, argv, 0));
    }
}

/** The clipboard_get command: return the text on the clipboard.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the text
 */
static void box_cmd_clipboard_get(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (!box_cmd_input_ready(ctx)) {
        *(char**)result->ptr = ecs_os_strdup(box_input_clipboard_get(ctx->world));
    }
}

/** The action_press command: hold an action for dur frames, one when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The action and the duration
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_action_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t frames = box_cmd_input_i32(argc, argv, 1);
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx) || box_cmd_input_frames("dur", frames)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, box_cmd_input_str(argc, argv, 0));
    if (action) {
        box_input_action_press(ctx->world, action, frames);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The action_down and action_up commands: a synthetic hold of an action starts or ends.
 *
 * @param ctx The call context, ctx->ctx is non NULL for action_down
 * @param argc Number of arguments
 * @param argv The action
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_action_down_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, box_cmd_input_str(argc, argv, 0));
    if (action) {
        box_input_action(ctx->world, action, ctx->ctx != NULL);
        box_cmd_input_pending(ctx, result);
    }

done:
    return;
}

/** The action_drag command: hold an action while the cursor walks between two positions.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The action, from, to, steps and dur
 * @param result Receives the frames until the input queue is drained
 */
static void box_cmd_action_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_input_sweep_t walk = {0};
    ecs_entity_t action;
    if (box_cmd_input_ready(ctx) || box_cmd_input_walk(argc, argv, 1, &walk)) {
        goto done;
    }

    action = box_cmd_input_action(ctx, box_cmd_input_str(argc, argv, 0));
    if (action) {
        box_input_action_drag(ctx->world, action, &walk);
        box_cmd_input_pending(ctx, result);
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

/** The action_list command: one line per action with its path, held state and description.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the lines
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
    if (box_cmd_input_ready(ctx)) {
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

/** Parameter of a key name */
#define BOX_CMD_INPUT_KEY { .name = "key", .type = ecs_id(ecs_string_t), .brief = "Key name" }

/** Parameter of a mouse button */
#define BOX_CMD_INPUT_BUTTON \
    { .name = "button", .type = ecs_id(ecs_string_t), .brief = "left, middle or right" }

/** Parameter of an action */
#define BOX_CMD_INPUT_ACTION \
    { .name = "action", .type = ecs_id(ecs_string_t), .brief = "Action path or name" }

/** Optional parameter of a duration in frames */
#define BOX_CMD_INPUT_DUR { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true,\
    .brief = "Frames (default: 1)" }

/** Parameters of a cursor walk: from, to, steps and dur */
#define BOX_CMD_INPUT_WALK \
    { .name = "from", .type = ecs_id(ecs_string_t), .brief = "Start position [x, y]" },\
    { .name = "to", .type = ecs_id(ecs_string_t), .brief = "End position [x, y]" },\
    { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true,\
      .brief = "Cursor positions (default: 10)" },\
    { .name = "dur", .type = ecs_id(ecs_i32_t), .optional = true,\
      .brief = "Frames the walk takes" }

/** Register a command that queues input and returns the frames until the queue is drained.
 *
 * @param world The world
 * @param brief One line description
 * @param desc The function
 */
static void box_cmd_input_register(
    ecs_world_t *world,
    const char *brief,
    ecs_function_desc_t desc)
{
    desc.return_type = ecs_id(ecs_i32_t);
    box_command_register(world, brief, desc);
}

/** Register the key commands.
 *
 * @param world The world
 */
static void box_cmd_input_keys(
    ecs_world_t *world)
{
    box_cmd_input_register(world, "Tap a key, held for dur frames (default 1)",
        (ecs_function_desc_t){
            .name = "key_press",
            .params = { BOX_CMD_INPUT_KEY, BOX_CMD_INPUT_DUR },
            .callback = box_cmd_key_press
        });

    box_cmd_input_register(world, "Press a key until key_up",
        (ecs_function_desc_t){
            .name = "key_down",
            .params = { BOX_CMD_INPUT_KEY },
            .callback = box_cmd_key_down_up,
            .ctx = world
        });

    box_cmd_input_register(world, "Release a key",
        (ecs_function_desc_t){
            .name = "key_up",
            .params = { BOX_CMD_INPUT_KEY },
            .callback = box_cmd_key_down_up
        });

    box_cmd_input_register(world,
        "Repeat a held key count times (default 1), dur frames apart (default 1)",
        (ecs_function_desc_t){
            .name = "key_repeat",
            .params = {
                BOX_CMD_INPUT_KEY,
                { .name = "count", .type = ecs_id(ecs_i32_t), .optional = true,
                  .brief = "Repeats (default: 1)" },
                BOX_CMD_INPUT_DUR
            },
            .callback = box_cmd_key_repeat
        });

    box_cmd_input_register(world,
        "Type text, one key press per character held for dur frames (default 1)",
        (ecs_function_desc_t){
            .name = "key_type",
            .params = {
                { .name = "text", .type = ecs_id(ecs_string_t), .brief = "Text to type" },
                BOX_CMD_INPUT_DUR
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
    box_cmd_input_register(world,
        "Pin the cursor at [x, y], in 0..1 of the window when norm is true",
        (ecs_function_desc_t){
            .name = "mouse_move",
            .params = {
                { .name = "pos", .type = ecs_id(ecs_string_t), .brief = "Position [x, y]" },
                { .name = "norm", .type = ecs_id(ecs_bool_t), .optional = true,
                  .brief = "Position is in 0..1 of the window (default: false)" }
            },
            .callback = box_cmd_mouse_move
        });

    box_cmd_input_register(world,
        "Walk the cursor from [x, y] to [x, y] in steps (default 10) over dur frames",
        (ecs_function_desc_t){
            .name = "mouse_sweep",
            .params = { BOX_CMD_INPUT_WALK },
            .callback = box_cmd_mouse_sweep
        });

    box_cmd_input_register(world, "Unpin the cursor, the real pointer moves it again",
        (ecs_function_desc_t){
            .name = "mouse_release",
            .callback = box_cmd_mouse_release
        });

    box_cmd_input_register(world,
        "Press (true) or release (false) a button: left, middle or right",
        (ecs_function_desc_t){
            .name = "mouse_button",
            .params = {
                BOX_CMD_INPUT_BUTTON,
                { .name = "down", .type = ecs_id(ecs_bool_t),
                  .brief = "Press (true) or release (false)" }
            },
            .callback = box_cmd_mouse_button
        });

    box_cmd_input_register(world,
        "Click a button at [x, y] (- or omitted: the cursor), held for dur frames",
        (ecs_function_desc_t){
            .name = "mouse_click",
            .params = {
                BOX_CMD_INPUT_BUTTON,
                { .name = "pos", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "Position [x, y] (default: the cursor)" },
                BOX_CMD_INPUT_DUR
            },
            .callback = box_cmd_mouse_click
        });

    box_cmd_input_register(world, "Hold a button while the cursor walks from [x, y] to [x, y]",
        (ecs_function_desc_t){
            .name = "mouse_drag",
            .params = { BOX_CMD_INPUT_BUTTON, BOX_CMD_INPUT_WALK },
            .callback = box_cmd_mouse_drag
        });

    box_cmd_input_register(world, "Move the wheel by dx, dy",
        (ecs_function_desc_t){
            .name = "mouse_scroll",
            .params = {
                { .name = "dx", .type = ecs_id(ecs_f64_t), .brief = "Horizontal offset" },
                { .name = "dy", .type = ecs_id(ecs_f64_t), .brief = "Vertical offset" }
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
    box_cmd_input_register(world, "Hold an action for dur frames (default 1)",
        (ecs_function_desc_t){
            .name = "action_press",
            .params = { BOX_CMD_INPUT_ACTION, BOX_CMD_INPUT_DUR },
            .callback = box_cmd_action_press
        });

    box_cmd_input_register(world, "Hold an action until action_up",
        (ecs_function_desc_t){
            .name = "action_down",
            .params = { BOX_CMD_INPUT_ACTION },
            .callback = box_cmd_action_down_up,
            .ctx = world
        });

    box_cmd_input_register(world, "Release a held action",
        (ecs_function_desc_t){
            .name = "action_up",
            .params = { BOX_CMD_INPUT_ACTION },
            .callback = box_cmd_action_down_up
        });

    box_cmd_input_register(world, "Hold an action while the cursor walks from [x, y] to [x, y]",
        (ecs_function_desc_t){
            .name = "action_drag",
            .params = { BOX_CMD_INPUT_ACTION, BOX_CMD_INPUT_WALK },
            .callback = box_cmd_action_drag
        });

    box_command_register(world, "List the actions: path, (down) when held, description",
        (ecs_function_desc_t){
            .name = "action_list",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_action_list
        });
}

void box_cmd_input_init(
    ecs_world_t *world)
{
    box_cmd_input_keys(world);
    box_cmd_input_mouse(world);

    box_cmd_input_register(world,
        "Empty the input queue and release every key, button, action and the cursor",
        (ecs_function_desc_t){
            .name = "input_reset",
            .callback = box_cmd_input_reset
        });

    box_cmd_input_register(world,
        "Gain (true) or lose (false) focus, platform input is ignored without it",
        (ecs_function_desc_t){
            .name = "input_focus",
            .params = {
                { .name = "focused", .type = ecs_id(ecs_bool_t),
                  .brief = "Gain (true) or lose (false) focus" }
            },
            .callback = box_cmd_input_focus
        });

    box_cmd_input_register(world,
        "Return the frames until the input queue is drained, 0 when it is; repeat until 0",
        (ecs_function_desc_t){
            .name = "input_wait",
            .callback = box_cmd_input_wait
        });

    box_command_register(world,
        "Put text on the clipboard, which the paste shortcut pastes",
        (ecs_function_desc_t){
            .name = "clipboard_set",
            .params = {
                { .name = "text", .type = ecs_id(ecs_string_t), .brief = "Text to put" }
            },
            .callback = box_cmd_clipboard_set
        });

    box_command_register(world,
        "Return the text on the clipboard, of the platform or in memory",
        (ecs_function_desc_t){
            .name = "clipboard_get",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_clipboard_get
        });

    box_cmd_input_action_cmds(world);
}
