/**
 * @file ui/keys.c
 * @brief Keys and presses of the UI: the claim that keeps keys from the bindings, Tab, Enter and
 * Space on the focused entity, the to_focus hook that decides every move of the flecs focus,
 * ui.activation, and the hand off of keys, text and presses to a focused TextEdit.
 *
 * The claim runs in InputUpdate with the focus the last frame left; the keys and presses then
 * reach the hook in box.picking in order. flecs moves the focus of a press through to_focus
 * before it sends the press, the hook sends the keys to flecs and moves the focus for Tab.
 */

#include "ui.h"
#include <string.h>

/** Check whether the console is open, which takes every key from the UI.
 *
 * @param world The world
 * @return Whether box.console is imported and open
 */
static bool box_ui_console_open(
    const ecs_world_t *world)
{
    const Console *console = ecs_id(Console) ? ecs_singleton_get(world, Console) : NULL;

    return console && console->open;
}

/** Check whether a key goes to the UI only while the focus is visible.
 *
 * @param key The key
 * @return Whether it is a navigation or activation key
 */
static bool box_ui_nav_key(
    Key key)
{
    return key == KeyTab || key == KeyEnter || key == KeyKpEnter || key == KeySpace ||
        key == KeyEscape || key == KeyHome || key == KeyEnd || key == KeyPageUp ||
        key == KeyPageDown || (key >= KeyLeft && key <= KeyDown);
}

/** Move the text of a string of Input into a vector of the state and empty the string.
 *
 * @param text The string, which is emptied
 * @param out The char vector, terminated when it receives text
 */
static void box_ui_take_text(
    char *text,
    ecs_vec_t *out)
{
    int32_t size = text ? (int32_t)strlen(text) : 0;
    if (size) {
        memcpy(ecs_vec_grow_t(NULL, out, char, size + 1), text, (size_t)size + 1);
        text[0] = '\0';
    }
}

/** Take the keys and text of a frame from Input that the focused entity keeps from the bindings,
 * the claim of box.input.
 *
 * KeyCapture takes every keyboard key but those of the console toggle and the text; a visible
 * focus takes the navigation keys; with nothing focused a FocusScope takes Tab.
 *
 * @param world The world
 * @param in The Input singleton
 * @param ctx The module state
 */
static void box_ui_claim(
    ecs_world_t *world,
    Input *in,
    void *ctx)
{
    box_ui_state_t *st = ctx;
    bool keep[BOX_KEY_COUNT] = {0};
    bool capture, nav;
    int32_t k;
    ecs_vec_clear(&st->text);
    ecs_vec_clear(&st->paste);
    box_ui_focus_check(world, st);
    st->console = box_ui_console_open(world);
    if (st->console) {
        goto done;
    }

    capture = st->focused && box_ui_up(world, st->focused, KeyCapture);
    nav = st->focused ? st->visible : ecs_query_is_true(st->scopes);
    if (capture) {
        box_input_action_keys(world, ecs_lookup(world, "box.console.toggle"), keep);
    }

    for (k = KeyNone + 1; k < KeyMouseLeft; k ++) {
        bool take = capture ? !keep[k] :
            nav && (st->focused ? box_ui_nav_key((Key)k) : k == KeyTab);
        if (take) {
            in->keys[k] = (KeyState){0};
        }
    }

    if (capture) {
        box_ui_take_text(in->text, &st->text);
        box_ui_take_text(in->paste, &st->paste);
        ecs_os_free(in->paste);
        in->paste = NULL;
    }

done:
    return;
}

/** Get the trigger of a focused element that Enter and Space drive.
 *
 * @param world The world
 * @param e The element
 * @return The trigger, NULL when it has none, it uses no button or its action is not an Action
 */
static const ActionTrigger* box_ui_trigger(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const ActionTrigger *result = e && box_ui_can_focus(world, e) ?
        ecs_get(world, e, ActionTrigger) : NULL;
    if (result && (result->button == KeyNone || !ecs_is_valid(world, result->action) ||
        !ecs_is_alive(world, result->action) || !ecs_has(world, result->action, Action)))
    {
        result = NULL;
    }

    return result;
}

/** Get the cursor the action events of a keyboard trigger report.
 *
 * @param world The world
 * @param cursor Receives the cursor of Input
 */
static void box_ui_cursor(
    const ecs_world_t *world,
    vec2 cursor)
{
    const Input *in = ecs_singleton_get(world, Input);
    cursor[0] = in ? in->mouse.position[0] : 0;
    cursor[1] = in ? in->mouse.position[1] : 0;
}

void box_ui_hold_release(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    vec2 cursor;
    if (st->hold_source) {
        box_ui_cursor(world, cursor);
        box_input_trigger_hold(
            box_input_device(world), st->hold_source, st->hold_action, false, cursor);
    }

    st->hold_source = 0;
    st->hold_action = 0;
}

/** Drive the ActionTrigger of the focused element from Enter and Space: Enter taps, Space taps
 * when it comes up (Click) or goes down (Press), or holds the action while it is down (Hold).
 *
 * @param world The world
 * @param st The module state
 * @param e The focused element
 * @param key KeyEnter or KeySpace
 * @param down Whether the key went down
 */
static void box_ui_trigger_key(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    Key key,
    bool down)
{
    const ActionTrigger *t = box_ui_trigger(world, e);
    InputDevice *dev = box_input_device(world);
    vec2 cursor;
    box_ui_cursor(world, cursor);
    if (!t) {
        goto done;
    }

    if (key == KeyEnter) {
        box_input_trigger_tap(dev, e, t->action, cursor);
    } else if (t->mode == ActionTriggerHold && down) {
        box_ui_hold_release(world, st);
        st->hold_source = e;
        st->hold_action = t->action;
        box_input_trigger_hold(dev, e, t->action, true, cursor);
    } else if (t->mode == ActionTriggerHold) {
        box_ui_hold_release(world, st);
    } else if ((t->mode == ActionTriggerPress) == down) {
        box_input_trigger_tap(dev, e, t->action, cursor);
    }

done:
    return;
}

/** Drive the ActionTrigger of the focused entity from Enter going down and from Space, which it
 * follows from going down on the entity to coming up.
 *
 * These are the keys and the rule of the on.activate of flecs, sent with the same event. flecs
 * only resolves the script futures for it and tells C the number of listeners, not whether the
 * event activated the focus, so the trigger keeps this copy of the rule.
 *
 * @param world The world
 * @param st The module state
 * @param e The focused entity
 * @param evt The key event
 */
static void box_ui_trigger_keys(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    const box_ui_key_t *evt)
{
    Key key = evt->key == KeyKpEnter ? KeyEnter : evt->key;
    if (evt->repeat || (key != KeyEnter && key != KeySpace)) {
        goto done;
    }

    if (key == KeyEnter && evt->down) {
        box_ui_trigger_key(world, st, e, key, true);
    } else if (key == KeySpace && evt->down) {
        st->space = e;
        box_ui_trigger_key(world, st, e, key, true);
    } else if (key == KeySpace && st->space == e) {
        st->space = 0;
        box_ui_trigger_key(world, st, e, key, false);
    } else if (key == KeySpace && st->hold_source) {
        box_ui_hold_release(world, st);
    }

done:
    return;
}

/** Take a keyboard event: flecs sends on.key_down, on.key_up and on.activate to the focus and its
 * ancestors, then Tab moves the focus, a focused TextEdit edits, Enter and Space drive an
 * ActionTrigger unless KeyCapture keeps them. An open console takes every key from the scripts.
 *
 * The key of the event is the value of box_input_key_value, from the platform and from the
 * input queue alike: "Enter" for Enter and KpEnter, " " for Space, the names flecs activates on.
 *
 * @param world The world
 * @param pst The picking state
 * @param emit The keyboard event
 * @param ctx The module state
 */
static void box_ui_key(
    ecs_world_t *world,
    box_pick_state_t *pst,
    const box_input_emit_t *emit,
    void *ctx)
{
    box_ui_state_t *st = ctx;
    const EcsScriptKeyboardEvent *kb = &emit->keyboard;
    box_ui_key_t evt = {
        .key = emit->key,
        .down = kb->down,
        .repeat = kb->repeat,
        .ctrl = kb->ctrl,
        .shift = kb->shift,
        .alt = kb->alt,
        .meta = kb->meta
    };
    ecs_entity_t e;
    if (st->console || box_ui_console_open(world)) {
        goto done;
    }

    box_ui_focus_check(world, st);
    e = st->focused;
    ecs_script_keyboard_event(world, kb);

    if (evt.key == KeyTab && evt.down && !evt.ctrl && !evt.alt && !evt.meta) {
        st->keyboard = box_ui_focus_step(world, st, pst, evt.shift) || st->keyboard;
    } else if (e && st->focused == e && ecs_has(world, e, TextEdit)) {
        box_ui_text_edit_key(world, st, e, &evt);
    } else if (e && st->focused == e && !box_ui_up(world, e, KeyCapture)) {
        box_ui_trigger_keys(world, st, e, &evt);
    }

    box_ui_progress(world, st);
done:
    return;
}

/** Find the first Focusable entity on the bubbling path of a pick, from its target to its handler.
 * The path ends at an entity that the scripts of the press deleted.
 *
 * @param world The world
 * @param pick The pick
 * @return The entity, 0 when the path has none
 */
static ecs_entity_t box_ui_press_focus(
    const ecs_world_t *world,
    const box_pick_t *pick)
{
    ecs_entity_t e = pick->target, result = 0;
    int32_t depth = 0;
    while (e && ecs_is_alive(world, e) && !result && depth < BOX_UI_DEPTH_MAX) {
        result = box_ui_can_focus(world, e) ? e : 0;
        e = e == pick->handler ? 0 : ecs_get_parent(world, e);
        depth ++;
    }

    return result;
}

/** Find the entity box.ui focuses for an entity: the entity or the first Focusable ancestor up
 * to the first Interactive entity on the way, as a press on it would.
 *
 * @param world The world
 * @param e The entity, 0 for none
 * @param refused What to return when the path has no entity that can take the focus
 * @return The entity, refused when there is none
 */
static ecs_entity_t box_ui_focus_rule(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t refused)
{
    ecs_entity_t result = 0;
    int32_t depth = 0;
    while (e && ecs_is_alive(world, e) && !result && depth < BOX_UI_DEPTH_MAX) {
        result = box_ui_can_focus(world, e) ? e : 0;
        e = ecs_has_id(world, e, Interactive) ? 0 : ecs_get_parent(world, e);
        depth ++;
    }

    return result ? result : refused;
}

/** Decide a move of the flecs focus and publish it in Focused and FocusStatus, the to_focus hook
 * that box.picking sets for flecs.script.input.
 *
 * A press focuses the first Focusable entity on the bubbling path of its pick or clears the
 * focus, without a visible focus. box_ui_focus_set has decided already.
 *
 * Anything else, focus() from a script or ecs_script_set_focus from the application, gets the
 * entity or the Focusable ancestor a press on it would focus, else keeps the focus; 0 clears it.
 *
 * @param world The world
 * @param pst The picking state
 * @param entity The entity flecs would focus, 0 for none
 * @param current The entity with the focus, 0 for none
 * @param source What moves the focus
 * @param pick The pick of the press, NULL when the press does not come from box.picking
 * @param ctx The module state
 * @return The entity that gets the focus
 */
static ecs_entity_t box_ui_to_focus(
    ecs_world_t *world,
    box_pick_state_t *pst,
    ecs_entity_t entity,
    ecs_entity_t current,
    ecs_script_focus_source_t source,
    const box_pick_t *pick,
    void *ctx)
{
    box_ui_state_t *st = ctx;
    bool press = source == EcsScriptFocusPress, visible = st->keyboard;
    ecs_entity_t result;
    (void)pst;
    if (press) {
        st->keyboard = false;
        visible = false;
        result = pick ? box_ui_press_focus(world, pick) : box_ui_focus_rule(world, entity, 0);
    } else if (source == EcsScriptFocusSet && st->setting) {
        result = entity;
        visible = st->set_visible;
    } else {
        result = entity ? box_ui_focus_rule(world, entity, current) : 0;
    }

    st->decided = true;
    if (result != st->focused || press) {
        box_ui_focus_apply(world, st, result, visible);
    }

    return result;
}

/** Check whether an entity is an entity or one of its ancestors.
 *
 * @param world The world
 * @param ancestor The entity that may be an ancestor
 * @param e The entity
 * @return Whether ancestor is e or above it
 */
static bool box_ui_is_up(
    const ecs_world_t *world,
    ecs_entity_t ancestor,
    ecs_entity_t e)
{
    int32_t depth = 0;
    bool result = false;
    while (e && ecs_is_alive(world, e) && !result && depth < BOX_UI_DEPTH_MAX) {
        result = e == ancestor;
        e = ecs_get_parent(world, e);
        depth ++;
    }

    return result;
}

/** Follow a pointer event: a press of any button makes the focus it moved (or kept, on the
 * focused entity) not visible and a left press and drag select in a TextEdit. box_ui_to_focus
 * moved the focus before the scripts saw the press.
 *
 * @param world The world
 * @param pst The picking state
 * @param src The event
 * @param pick The pick it was delivered by
 * @param pressed The buttons that went down
 * @param released The buttons that came up
 * @param ctx The module state
 */
static void box_ui_pointer(
    ecs_world_t *world,
    box_pick_state_t *pst,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released,
    void *ctx)
{
    box_ui_state_t *st = ctx;
    (void)pst;
    if (pressed) {
        ecs_entity_t e = box_ui_press_focus(world, pick);
        st->keyboard = false;
        if (e && e == st->focused) {
            box_ui_focus_apply(world, st, e, false);
        }
    }

    box_ui_text_edit_pointer(world, st, src, pick, pressed, released);
    box_ui_progress(world, st);
}

/** Check the focus and the Space hold before the events of a frame, and insert the text and the
 * paste of the frame into a focused TextEdit.
 *
 * @param world The world
 * @param pst The picking state
 * @param ctx The module state
 */
static void box_ui_begin(
    ecs_world_t *world,
    box_pick_state_t *pst,
    void *ctx)
{
    box_ui_state_t *st = ctx;
    const ActionTrigger *t = st->hold_source ? box_ui_trigger(world, st->hold_source) : NULL;
    (void)pst;
    box_ui_focus_check(world, st);
    if (st->hold_source && (!t || t->mode != ActionTriggerHold ||
        t->action != st->hold_action || st->hold_source != st->focused))
    {
        box_ui_hold_release(world, st);
    }

    box_ui_text_edit_input(world, st);
    box_ui_progress(world, st);
}

bool box_ui_focus(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    box_ui_state_t *st = box_ui_state(world);
    bool result = false;
    if (st && (!entity || box_ui_can_focus(world, entity))) {
        box_ui_focus_set(world, st, entity, st->keyboard);
        box_ui_progress(world, st);
        result = st->focused == entity;
    }

    return result;
}

/** Find the entity that handles the pointer events of an entity, as box.picking does: the first
 * Interactive entity from it up, else the first InputBlocker.
 *
 * @param world The world
 * @param e The entity
 * @return The handler, 0 when there is none
 */
static ecs_entity_t box_ui_handler(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = box_ui_up(world, e, Interactive);

    return result ? result : box_ui_up(world, e, InputBlocker);
}

/** Decide whether box.ui counts an on.activate of flecs and for which entity.
 *
 * A key activates the focus unless KeyCapture keeps Enter and Space for it, as a TextEdit does.
 * A click activates the handler of the press when the left button came up over it or a child.
 *
 * flecs activates on the release of any button on the pressed entity, which box.picking makes
 * the release target wherever the cursor is, and reports the hit under the handler, as a label.
 *
 * @param world The world
 * @param evt The event
 * @return The activated entity, 0 when box.ui does not count the event as an activation
 */
static ecs_entity_t box_ui_activated(
    ecs_world_t *world,
    const EcsScriptActivateEvent *evt)
{
    const PickingState *ps = ecs_id(PickingState) ?
        ecs_singleton_get(world, PickingState) : NULL;
    ecs_entity_t e = evt->target, handler, result = 0;
    const EcsScriptMouseEvent *last;
    box_pick_t at;
    if (!e || !ecs_is_alive(world, e)) {
        goto done;
    }

    if (evt->keyboard) {
        result = box_ui_up(world, e, KeyCapture) ? 0 : e;
        goto done;
    }

    handler = box_ui_handler(world, e);
    if (!handler || !ps || !ps->state || !ps->state->has_sent) {
        goto done;
    }

    last = &ps->state->sent;
    if (last->button != 0) {
        goto done;
    }

    box_pick_at(world, ps->state, (vec2){ last->screen_x, last->screen_y }, &at);
    if (at.target && box_ui_is_up(world, handler, at.target)) {
        result = handler;
    }

done:
    return result;
}

/** Tell which entity an on.activate of flecs activates for box.ui: ui.activation(event).
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The EcsScriptActivateEvent
 * @param result Receives the activated entity, 0 when box.ui does not count the event
 */
static void box_ui_activation(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ctx->world);
    (void)argc;
    *(ecs_entity_t*)result->ptr = box_ui_activated(world, argv[0].ptr);
}

void box_ui_activation_import(
    ecs_world_t *world,
    ecs_entity_t ui)
{
    ecs_function(world, {
        .name = "activation",
        .parent = ui,
        .return_type = ecs_id(ecs_entity_t),
        .params = {{ "event", ecs_id(EcsScriptActivateEvent) }},
        .callback = box_ui_activation
    });
}

void box_ui_keys_import(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    box_input_claim_set(world, box_ui_claim, st);
    box_pick_hook_set(world, &(box_pick_hook_t){
        .begin = box_ui_begin,
        .key = box_ui_key,
        .pointer = box_ui_pointer,
        .focus = box_ui_to_focus,
        .ctx = st
    });
}
