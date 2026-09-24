/**
 * @file command/input_cmd.c
 * @brief The key, mouse, input and action commands that drive a running instance.
 */

#include "command_internal.h"
#include "../input/input_internal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/** Steps a sweep or a drag walks when the command does not give a count */
#define BOX_INPUT_CMD_STEPS (8)
#define BOX_INPUT_CMD_STEPS_MAX (4096)
#define BOX_INPUT_CMD_FRAMES_MAX (36000)

/** Masks one action of the action list shows */
#define BOX_INPUT_CMD_BINDINGS (8)

/** Parse a boolean argument written as true, false, 1, 0, on or off.
 *
 * @param text The argument
 * @param out Receives the value
 * @return 0 on success, -1 when the text is not a boolean
 */
static int box_input_cmd_bool(
    const char *text,
    bool *out)
{
    int result = 0;
    if (!strcmp(text, "true") || !strcmp(text, "1") || !strcmp(text, "on")) {
        out[0] = true;
    } else if (!strcmp(text, "false") || !strcmp(text, "0") ||
        !strcmp(text, "off"))
    {
        out[0] = false;
    } else {
        result = -1;
    }

    return result;
}

/** Parse a position argument written as x,y.
 *
 * @param text The argument
 * @param out Receives the position
 * @return 0 on success, -1 when the text is not a position
 */
static int box_input_cmd_pos(
    const char *text,
    vec2 *out)
{
    int result = -1;
    char *end = NULL;
    float x = (float)strtod(text, &end);
    if (end != text && end[0] == ',') {
        const char *rest = end + 1;
        float y = (float)strtod(rest, &end);
        if (end != rest && !end[0]) {
            out[0] = (vec2){ x, y };
            result = 0;
        }
    }

    return result;
}

/** Turn a normalized position into window points with the size of the surface.
 *
 * @param world The world
 * @param pos The position, in -1 to 1 with x right and y up
 * @return The position in window points
 */
static vec2 box_input_cmd_denorm(
    ecs_world_t *world,
    vec2 pos)
{
    const Surface *s = ecs_singleton_get(world, Surface);
    float width = 0, height = 0;
    if (s) {
        width = (float)(s->point_width ? s->point_width : s->width);
        height = (float)(s->point_height ? s->point_height : s->height);
    }

    return (vec2){ (pos.x + 1.0f) * width * 0.5f, (1.0f - pos.y) * height * 0.5f };
}

/** Resolve a position argument, which the norm flag reads as normalized.
 *
 * @param world The world
 * @param text The argument
 * @param norm The norm flag argument, NULL when it was omitted
 * @param out Receives the position in window points
 * @return NULL on success, a one line error the caller frees
 */
static char* box_input_cmd_point(
    ecs_world_t *world,
    const char *text,
    const char *norm,
    vec2 *out)
{
    char *result = NULL;
    bool normalized = false;
    if (box_input_cmd_pos(text, out)) {
        result = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid position '%s', expected x,y", text);
        goto done;
    }

    if (norm && norm[0] && box_input_cmd_bool(norm, &normalized)) {
        result = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid boolean '%s'", norm);
        goto done;
    }

    if (normalized) {
        out[0] = box_input_cmd_denorm(world, out[0]);
    }

done:
    return result;
}

/** Resolve a key argument to its code.
 *
 * @param name The key name
 * @param out Receives the key code
 * @return NULL on success, a one line error the caller frees
 */
static char* box_input_cmd_key(
    const char *name,
    int32_t *out)
{
    char *result = NULL;
    int32_t code = box_input_key_from_name(name);
    if (code <= 0) {
        result = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "unknown key '%s'", name);
    } else {
        out[0] = code;
    }

    return result;
}

/** Resolve a mouse button argument, which may be left, right or a key name.
 *
 * @param name The button name
 * @param out Receives the button code
 * @return NULL on success, a one line error the caller frees
 */
static char* box_input_cmd_button(
    const char *name,
    int32_t *out)
{
    char *result = NULL;
    if (!strcmp(name, "left")) {
        out[0] = BoxKeyMouseLeft;
    } else if (!strcmp(name, "right")) {
        out[0] = BoxKeyMouseRight;
    } else {
        result = box_input_cmd_key(name, out);
        if (!result && out[0] < BoxKeyMouseLeft) {
            result = flecs_asprintf(
                BOX_COMMAND_ERROR_PREFIX "'%s' is not a mouse button", name);
        }
    }

    return result;
}

/** Queue a key or button down event, which its duration releases again.
 *
 * @param world The world
 * @param code The key or button code
 * @param frames Frames the key stays down, 0 to hold it
 * @param delay Frames before the event applies
 */
static void box_input_cmd_down(
    ecs_world_t *world,
    int32_t code,
    int32_t frames,
    int32_t delay)
{
    bool button = code >= BoxKeyMouseLeft;
    box_input_inject(world, &(InputEvent){
        .kind = button ? InputButtonDown : InputKeyDown,
        .code = code,
        .delay = delay,
        .duration = frames
    });
}

/** Queue a key or button up event.
 *
 * @param world The world
 * @param code The key or button code
 * @param delay Frames before the event applies
 */
static void box_input_cmd_up(
    ecs_world_t *world,
    int32_t code,
    int32_t delay)
{
    bool button = code >= BoxKeyMouseLeft;
    box_input_inject(world, &(InputEvent){
        .kind = button ? InputButtonUp : InputKeyUp,
        .code = code,
        .delay = delay
    });
}

/** Queue the cursor positions of a sweep and pin the cursor.
 *
 * @param world The world
 * @param from Where the sweep starts, in window points
 * @param to Where the sweep ends, in window points
 * @param steps Number of positions after the first
 * @param frames Frames the sweep is spread over
 */
static void box_input_cmd_sweep(
    ecs_world_t *world,
    vec2 from,
    vec2 to,
    int32_t steps,
    int32_t frames)
{
    box_input_cursor_pin(world, true);
    int32_t i;
    for (i = 0; i <= steps; i ++) {
        float t = (float)i / (float)steps;
        box_input_inject(world, &(InputEvent){
            .kind = InputMouseMove,
            .value = {
                from.x + (to.x - from.x) * t,
                from.y + (to.y - from.y) * t
            },
            .delay = (i * frames) / steps
        });
    }
}

/** Fill in the step and frame counts of a sweep, which may be omitted.
 *
 * @param steps The steps argument, 0 when it was omitted
 * @param frames The frames argument, 0 when it was omitted
 */
static void box_input_cmd_spread(
    int32_t *steps,
    int32_t *frames)
{
    if (steps[0] <= 0) {
        steps[0] = BOX_INPUT_CMD_STEPS;
    }

    if (steps[0] > BOX_INPUT_CMD_STEPS_MAX) {
        steps[0] = BOX_INPUT_CMD_STEPS_MAX;
    }

    if (frames[0] <= 0) {
        frames[0] = steps[0];
    }

    if (frames[0] > BOX_INPUT_CMD_FRAMES_MAX) {
        frames[0] = BOX_INPUT_CMD_FRAMES_MAX;
    }
}

/** The key.press command: tap a key for a number of frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_key_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    int32_t frames = *(ecs_i32_t*)argv[2].ptr, code = 0;
    char *out = box_input_cmd_key(name, &code);
    if (!out) {
        if (frames <= 0) {
            frames = 1;
        }

        box_input_cmd_down(ctx->world, code, frames, 0);
        out = flecs_asprintf("%s down for %d frame%s",
            name, frames, frames == 1 ? "" : "s");
    }

    *(char**)result->ptr = out;
}

/** The key.down command: hold a key until key.up.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_key_down(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    int32_t code = 0;
    char *out = box_input_cmd_key(name, &code);
    if (!out) {
        box_input_cmd_down(ctx->world, code, 0, 0);
        out = flecs_asprintf("%s down", name);
    }

    *(char**)result->ptr = out;
}

/** The key.up command: release a key.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_key_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    int32_t code = 0;
    char *out = box_input_cmd_key(name, &code);
    if (!out) {
        box_input_cmd_up(ctx->world, code, 0);
        out = flecs_asprintf("%s up", name);
    }

    *(char**)result->ptr = out;
}

/** Key code a character types, for the keys that have one.
 *
 * @param c The character
 * @return The key code, or 0 when no key types the character
 */
static int32_t box_input_cmd_char_key(
    char c)
{
    int32_t result = 0;
    if (isalpha((unsigned char)c)) {
        result = toupper((unsigned char)c);
    } else if (isdigit((unsigned char)c)) {
        result = BoxKeyNum0 + (c - '0');
    } else if (c == ' ') {
        result = BoxKeySpace;
    }

    return result;
}

/** The key.type command: press one key per character, spaced by frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_key_type(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *text = *(char**)argv[1].ptr;
    int32_t spacing = *(ecs_i32_t*)argv[2].ptr;
    if (spacing <= 0) {
        spacing = 1;
    }

    int32_t i, count = (int32_t)strlen(text);
    for (i = 0; i < count; i ++) {
        char c = text[i];
        box_input_inject(ctx->world, &(InputEvent){
            .kind = InputChar, .code = c, .delay = i * spacing });
        int32_t code = box_input_cmd_char_key(c);
        if (code) {
            box_input_cmd_down(ctx->world, code, 1, i * spacing);
        }
    }

    *(char**)result->ptr = flecs_asprintf(
        "typed %d character%s over %d frame%s", count, count == 1 ? "" : "s",
        count * spacing, count * spacing == 1 ? "" : "s");
}

/** The mouse.move command: put the cursor at a position and pin it there.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_move(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *pos = *(char**)argv[1].ptr;
    const char *norm = *(char**)argv[2].ptr;
    vec2 point = {0, 0};
    char *out = box_input_cmd_point(ctx->world, pos, norm, &point);
    if (!out) {
        box_input_cursor_pin(ctx->world, true);
        box_input_inject(ctx->world, &(InputEvent){
            .kind = InputMouseMove, .value = point });
        out = flecs_asprintf("cursor %g,%g", (double)point.x, (double)point.y);
    }

    *(char**)result->ptr = out;
}

/** The mouse.sweep command: walk the cursor from one position to another.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_sweep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    vec2 from = {0, 0}, to = {0, 0};
    int32_t steps = *(ecs_i32_t*)argv[3].ptr, frames = *(ecs_i32_t*)argv[4].ptr;
    char *out = box_input_cmd_point(
        ctx->world, *(char**)argv[1].ptr, NULL, &from);
    if (!out) {
        out = box_input_cmd_point(ctx->world, *(char**)argv[2].ptr, NULL, &to);
    }

    if (!out) {
        box_input_cmd_spread(&steps, &frames);
        box_input_cmd_sweep(ctx->world, from, to, steps, frames);
        out = flecs_asprintf("sweep %g,%g to %g,%g in %d steps over %d frames",
            (double)from.x, (double)from.y, (double)to.x, (double)to.y,
            steps, frames);
    }

    *(char**)result->ptr = out;
}

/** The mouse.release command: let the real pointer move the cursor again.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_release(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    box_input_cursor_pin(ctx->world, false);
    *(char**)result->ptr = ecs_os_strdup("cursor released");
}

/** The mouse.button command: press or release a mouse button.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_button(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    const char *state = *(char**)argv[2].ptr;
    int32_t code = 0;
    bool down = false;
    char *out = box_input_cmd_button(name, &code);
    if (!out && box_input_cmd_bool(state, &down)) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid boolean '%s'", state);
    }

    if (!out) {
        if (down) {
            box_input_cmd_down(ctx->world, code, 0, 0);
        } else {
            box_input_cmd_up(ctx->world, code, 0);
        }

        out = flecs_asprintf("%s %s", name, down ? "down" : "up");
    }

    *(char**)result->ptr = out;
}

/** The mouse.click command: tap a button, optionally at a position.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_click(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    const char *pos = *(char**)argv[2].ptr;
    int32_t frames = *(ecs_i32_t*)argv[3].ptr, code = 0;
    vec2 point = {0, 0};
    char *out = box_input_cmd_button(name, &code);
    if (!out && pos && pos[0]) {
        out = box_input_cmd_point(ctx->world, pos, NULL, &point);
    }

    if (!out) {
        if (frames <= 0) {
            frames = 1;
        }

        if (pos && pos[0]) {
            box_input_cursor_pin(ctx->world, true);
            box_input_inject(ctx->world, &(InputEvent){
                .kind = InputMouseMove, .value = point });
        }

        box_input_cmd_down(ctx->world, code, frames, 0);
        out = flecs_asprintf("%s click for %d frame%s", name, frames,
            frames == 1 ? "" : "s");
    }

    *(char**)result->ptr = out;
}

/** The mouse.drag command: press a button, sweep the cursor and release it.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    int32_t steps = *(ecs_i32_t*)argv[4].ptr, frames = *(ecs_i32_t*)argv[5].ptr;
    int32_t code = 0;
    vec2 from = {0, 0}, to = {0, 0};
    char *out = box_input_cmd_button(name, &code);
    if (!out) {
        out = box_input_cmd_point(
            ctx->world, *(char**)argv[2].ptr, NULL, &from);
    }

    if (!out) {
        out = box_input_cmd_point(ctx->world, *(char**)argv[3].ptr, NULL, &to);
    }

    if (!out) {
        box_input_cmd_spread(&steps, &frames);
        box_input_cmd_sweep(ctx->world, from, to, steps, frames);
        box_input_cmd_down(ctx->world, code, 0, 0);
        box_input_cmd_up(ctx->world, code, frames);
        out = flecs_asprintf("%s drag %g,%g to %g,%g in %d steps over %d frames",
            name, (double)from.x, (double)from.y, (double)to.x, (double)to.y,
            steps, frames);
    }

    *(char**)result->ptr = out;
}

/** The mouse.scroll command: queue a wheel offset.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_mouse_scroll(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    double dx = *(ecs_f64_t*)argv[1].ptr, dy = *(ecs_f64_t*)argv[2].ptr;
    box_input_inject(ctx->world, &(InputEvent){
        .kind = InputScroll, .value = { (float)dx, (float)dy } });

    *(char**)result->ptr = flecs_asprintf("scroll %g,%g", dx, dy);
}

/** The input.reset command: empty the queues and clear the key state.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_reset(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    box_input_reset(ctx->world);
    box_input_cursor_pin(ctx->world, false);
    *(char**)result->ptr = ecs_os_strdup("input reset");
}

/** The input.focus command: queue a focus change of the window.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_focus(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *text = *(char**)argv[1].ptr;
    bool focused = false;
    char *out = NULL;
    if (box_input_cmd_bool(text, &focused)) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid boolean '%s'", text);
    } else {
        box_input_inject(ctx->world, &(InputEvent){
            .kind = InputFocus, .code = focused });
        out = flecs_asprintf("focus %s", focused ? "true" : "false");
    }

    *(char**)result->ptr = out;
}

/** The input.wait command: report the events that are still queued.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    int32_t pending = box_input_pending(ctx->world);
    char *out = ecs_os_strdup("queue drained");
    if (pending) {
        ecs_os_free(out);
        out = flecs_asprintf("%d event%s pending", pending,
            pending == 1 ? "" : "s");
    }

    *(char**)result->ptr = out;
}

/** The action.press command: hold an action for a number of frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_action_press(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t action = *(ecs_entity_t*)argv[1].ptr;
    int32_t frames = *(ecs_i32_t*)argv[2].ptr;
    char *out = NULL;
    if (!ecs_has(ctx->world, action, InputAction)) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' is not an action",
            ecs_get_name(ctx->world, action));
    } else {
        if (frames <= 0) {
            frames = 1;
        }

        box_input_hold(ctx->world, action, session, frames);
        out = flecs_asprintf("%s down for %d frame%s",
            ecs_get_name(ctx->world, action), frames, frames == 1 ? "" : "s");
    }

    *(char**)result->ptr = out;
}

/** The action.down command: hold an action until action.up.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_action_down(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t action = *(ecs_entity_t*)argv[1].ptr;
    char *out = NULL;
    if (!ecs_has(ctx->world, action, InputAction)) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' is not an action",
            ecs_get_name(ctx->world, action));
    } else {
        box_input_hold(ctx->world, action, session, 0);
        out = flecs_asprintf("%s down", ecs_get_name(ctx->world, action));
    }

    *(char**)result->ptr = out;
}

/** The action.up command: release an action a command holds.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_action_up(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t action = *(ecs_entity_t*)argv[1].ptr;
    const char *name = ecs_get_name(ctx->world, action);
    char *out = NULL;
    if (!ecs_has(ctx->world, action, InputAction)) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "'%s' is not an action", name);
    } else if (!box_input_release(ctx->world, action)) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "'%s' is not held by a command", name);
    } else {
        out = flecs_asprintf("%s up", name);
    }

    *(char**)result->ptr = out;
}

/** The action.drag command: hold an action while the cursor sweeps.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_action_drag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t action = *(ecs_entity_t*)argv[1].ptr;
    int32_t steps = *(ecs_i32_t*)argv[4].ptr, frames = *(ecs_i32_t*)argv[5].ptr;
    vec2 from = {0, 0}, to = {0, 0};
    char *out = NULL;
    if (!ecs_has(ctx->world, action, InputAction)) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' is not an action",
            ecs_get_name(ctx->world, action));
    } else {
        out = box_input_cmd_point(
            ctx->world, *(char**)argv[2].ptr, NULL, &from);
    }

    if (!out) {
        out = box_input_cmd_point(ctx->world, *(char**)argv[3].ptr, NULL, &to);
    }

    if (!out) {
        box_input_cmd_spread(&steps, &frames);
        box_input_cmd_sweep(ctx->world, from, to, steps, frames);
        box_input_hold(ctx->world, action, session, frames + 1);
        out = flecs_asprintf("%s drag %g,%g to %g,%g in %d steps over %d frames",
            ecs_get_name(ctx->world, action), (double)from.x, (double)from.y,
            (double)to.x, (double)to.y, steps, frames);
    }

    *(char**)result->ptr = out;
}

/** One action of the action list, with the path it is sorted by */
typedef struct box_input_cmd_action_t {
    char *path;          /**< Path of the action */
    ecs_entity_t action; /**< The action entity */
} box_input_cmd_action_t;

/** Compare two listed actions by path.
 *
 * @param a The first action
 * @param b The second action
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_input_cmd_compare(
    const void *a,
    const void *b)
{
    const box_input_cmd_action_t *a1 = a;
    const box_input_cmd_action_t *a2 = b;

    return strcmp(a1->path, a2->path);
}

/** Append one action with its key masks and its description to a buffer.
 *
 * @param world The world
 * @param action The action entity
 * @param path Path of the action
 * @param buf The buffer
 */
static void box_input_cmd_action_line(
    ecs_world_t *world,
    ecs_entity_t action,
    const char *path,
    ecs_strbuf_t *buf)
{
    uint64_t masks[BOX_INPUT_CMD_BINDINGS];
    int32_t i, count = box_input_bindings(
        world, action, masks, BOX_INPUT_CMD_BINDINGS);
    ecs_strbuf_append(buf, "%s\t", path);

    for (i = 0; i < count; i ++) {
        char *name = box_input_mask_name(masks[i]);
        ecs_strbuf_append(buf, "%s%s", i ? "," : "", name);
        ecs_os_free(name);
    }

    const InputAction *ptr = ecs_get(world, action, InputAction);
    ecs_strbuf_append(buf, "%s\t%s\n", count ? "" : "-",
        ptr->description ? ptr->description : "");
}

/** The action.list command: list every action with its bindings.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_input_cmd_action_list(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(InputAction), .inout = EcsIn }}
    });

    ecs_vec_t actions;
    ecs_vec_init_t(NULL, &actions, box_input_cmd_action_t, 0);
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_input_cmd_action_t *entry = ecs_vec_append_t(
                NULL, &actions, box_input_cmd_action_t);
            entry->path = ecs_get_path(world, it.entities[i]);
            entry->action = it.entities[i];
        }
    }

    ecs_query_fini(q);

    int32_t i, count = ecs_vec_count(&actions);
    qsort(ecs_vec_first(&actions), (size_t)count,
        sizeof(box_input_cmd_action_t), box_input_cmd_compare);

    for (i = 0; i < count; i ++) {
        box_input_cmd_action_t *entry = ecs_vec_get_t(
            &actions, box_input_cmd_action_t, i);
        box_input_cmd_action_line(world, entry->action, entry->path, &buf);
        ecs_os_free(entry->path);
    }

    ecs_vec_fini_t(NULL, &actions, box_input_cmd_action_t);
    *(char**)result->ptr = ecs_strbuf_get(&buf);
}

/** Register the device layer commands.
 *
 * @param world The world
 */
static void box_input_cmd_device_init(
    ecs_world_t *world)
{
    box_command_param_t key[] = {
        { .name = "key", .type = ecs_id(ecs_string_t) },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "key.press",
        "Tap a key for a number of frames", key, 2, box_input_cmd_key_press);
    box_command_register(world, "key.down",
        "Hold a key until key.up", key, 1, box_input_cmd_key_down);
    box_command_register(world, "key.up",
        "Release a key", key, 1, box_input_cmd_key_up);

    box_command_param_t text[] = {
        { .name = "text", .type = ecs_id(ecs_string_t) },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "key.type",
        "Type text, one key press per character", text, 2,
        box_input_cmd_key_type);

    box_command_param_t move[] = {
        { .name = "pos", .type = ecs_id(ecs_string_t) },
        { .name = "norm", .type = ecs_id(ecs_string_t), .optional = true }
    };

    box_command_register(world, "mouse.move",
        "Put the cursor at x,y and pin it there", move, 2,
        box_input_cmd_mouse_move);

    box_command_param_t sweep[] = {
        { .name = "from", .type = ecs_id(ecs_string_t) },
        { .name = "to", .type = ecs_id(ecs_string_t) },
        { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "mouse.sweep",
        "Walk the cursor from one position to another", sweep, 4,
        box_input_cmd_mouse_sweep);
    box_command_register(world, "mouse.release",
        "Let the real pointer move the cursor again", NULL, 0,
        box_input_cmd_mouse_release);
}

/** Register the mouse button and the input state commands.
 *
 * @param world The world
 */
static void box_input_cmd_mouse_init(
    ecs_world_t *world)
{
    box_command_param_t button[] = {
        { .name = "button", .type = ecs_id(ecs_string_t) },
        { .name = "down", .type = ecs_id(ecs_string_t) }
    };

    box_command_register(world, "mouse.button",
        "Press or release a mouse button", button, 2,
        box_input_cmd_mouse_button);

    box_command_param_t click[] = {
        { .name = "button", .type = ecs_id(ecs_string_t) },
        { .name = "pos", .type = ecs_id(ecs_string_t), .optional = true },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "mouse.click",
        "Tap a mouse button, at a position when one is given", click, 3,
        box_input_cmd_mouse_click);

    box_command_param_t drag[] = {
        { .name = "button", .type = ecs_id(ecs_string_t) },
        { .name = "from", .type = ecs_id(ecs_string_t) },
        { .name = "to", .type = ecs_id(ecs_string_t) },
        { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "mouse.drag",
        "Hold a button while the cursor sweeps", drag, 5,
        box_input_cmd_mouse_drag);

    box_command_param_t scroll[] = {
        { .name = "dx", .type = ecs_id(ecs_f64_t) },
        { .name = "dy", .type = ecs_id(ecs_f64_t) }
    };

    box_command_register(world, "mouse.scroll",
        "Queue a wheel offset", scroll, 2, box_input_cmd_mouse_scroll);

    box_command_param_t focus[] = {
        { .name = "focused", .type = ecs_id(ecs_string_t) }
    };

    box_command_register(world, "input.reset",
        "Empty the queues and clear the key state", NULL, 0,
        box_input_cmd_reset);
    box_command_register(world, "input.focus",
        "Queue a focus change of the window", focus, 1, box_input_cmd_focus);
    box_command_register(world, "input.wait",
        "Report the events that are still queued", NULL, 0,
        box_input_cmd_wait);
}

void box_input_cmd_init(
    ecs_world_t *world)
{
    box_input_cmd_device_init(world);
    box_input_cmd_mouse_init(world);

    box_command_param_t press[] = {
        { .name = "action", .type = ecs_id(ecs_entity_t) },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "action.press",
        "Hold an action for a number of frames", press, 2,
        box_input_cmd_action_press);
    box_command_register(world, "action.down",
        "Hold an action until action.up", press, 1, box_input_cmd_action_down);
    box_command_register(world, "action.up",
        "Release an action a command holds", press, 1, box_input_cmd_action_up);

    box_command_param_t drag[] = {
        { .name = "action", .type = ecs_id(ecs_entity_t) },
        { .name = "from", .type = ecs_id(ecs_string_t) },
        { .name = "to", .type = ecs_id(ecs_string_t) },
        { .name = "steps", .type = ecs_id(ecs_i32_t), .optional = true },
        { .name = "frames", .type = ecs_id(ecs_i32_t), .optional = true }
    };

    box_command_register(world, "action.drag",
        "Hold an action while the cursor sweeps", drag, 5,
        box_input_cmd_action_drag);
    box_command_register(world, "action.list",
        "List every action with its key masks", NULL, 0,
        box_input_cmd_action_list);
}
