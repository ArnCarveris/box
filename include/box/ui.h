/**
 * @file box/ui.h
 * @brief UI: the keyboard focus, Tab navigation, keys kept from the bindings and widget events.
 *
 * One Focusable entity holds the keyboard focus. A press or Tab moves it, KeyCapture keeps the
 * keys from the game's bindings, and widgets report changes to the scope that uses them.
 */

#ifndef BOX_UI_H
#define BOX_UI_H

#include "types.h"
#include "input.h"
#include "picking.h"

#ifdef BOX_UI_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Marks an element that can take the keyboard focus; adding it makes it Interactive.
 *
 * A press of any mouse button focuses the first Focusable entity on the bubbling path of the
 * press, Tab the next one in its FocusScope. Disabled entities and prefabs never take it, and
 * disabling or deleting the focused entity or removing its Focusable clears the focus.
 */
extern ECS_TAG_DECLARE(Focusable);

/** Added to the one entity with the keyboard focus, on no entity while nothing has it */
extern ECS_TAG_DECLARE(Focused);

/** Makes Tab and Shift+Tab move the focus between the Focusable entities below it.
 *
 * Tab focuses the next Focusable entity inside the nearest FocusScope ancestor of the focused
 * entity in 2D draw order, wrapping around; with nothing focused, the first one of the first
 * FocusScope in the scenes of the views of the focused surface. Without any, Tab is a plain key.
 */
extern ECS_TAG_DECLARE(FocusScope);

/** While the focused entity or an ancestor of it has it, key presses and typed text go to the UI
 * only: Bindings and Input.keys see the keys up and Input.text stays empty.
 *
 * The keys of the console toggle still reach it, and an open console takes the keys from any
 * widget. box_ui_text returns the text it took.
 */
extern ECS_TAG_DECLARE(KeyCapture);

/** Where the keyboard focus is, a singleton written by the engine and read only for applications.
 *
 * While the focus is visible, Tab, Enter, Space, the arrow keys, Home, End, PageUp, PageDown and
 * Escape go to the UI only. A focus from the pointer leaves every key to the bindings too.
 */
ECS_STRUCT(FocusStatus, {
    ecs_entity_t focused;  /**< Entity with the focus, 0 when nothing has it */
    ecs_entity_t previous; /**< Entity that had the focus before the last change, 0 for none */
    bool visible;          /**< The focus came from the keyboard, so widgets show a focus ring */
});

/** What on.focus and on.blur resolve with */
ECS_STRUCT(UiFocusEvent, {
    ecs_entity_t target; /**< Entity that gained or lost the focus */
    bool visible;        /**< The focus that was gained or lost came from the keyboard */
});

/** What on.key resolves with, for each key event while the target or a descendant is focused */
ECS_STRUCT(UiKeyEvent, {
    Key key;     /**< The key */
    bool down;   /**< The key went down or repeated, false when it came up */
    bool repeat; /**< The platform repeated the held key */
    bool ctrl;   /**< A control key is held */
    bool shift;  /**< A shift key is held */
    bool alt;    /**< An alt key is held */
    bool meta;   /**< A super key is held */
});

/** What on.activate resolves with */
ECS_STRUCT(UiActivateEvent, {
    ecs_entity_t target; /**< Entity that was activated */
    bool keyboard;       /**< Enter or Space activated it, false for a click */
    float local_x;       /**< X of the click release in the space of target, 0 for keys */
    float local_y;       /**< Y of the click release in the space of target, 0 for keys */
});

/** What on.change and on.submit resolve with */
ECS_STRUCT(UiChangeEvent, {
    ecs_entity_t widget; /**< Widget that reported the change or the submit */
});

/** Register the box.ui module.
 *
 * Registers the async script functions flecs.script.on.focus, blur, key, activate, change and
 * submit, each taking a target and resolving on the target and its ancestors, and ui.focus,
 * ui.changed and ui.submit, which resolve at once (await ui.changed(this)).
 *
 * @param world The world
 */
void BoxUiImport(
    ecs_world_t *world);

/** Move the keyboard focus to an entity from C, as ui.focus does from an async block.
 *
 * The focus is visible when the last press or Tab that moved the focus was Tab. Resumes the async
 * blocks that on.focus and on.blur resolved.
 *
 * @param world The world
 * @param entity The entity, 0 clears the focus; an entity that cannot take it changes nothing
 * @return Whether the focus is on entity (or cleared for 0) when the call returns
 */
bool box_ui_focus(
    ecs_world_t *world,
    ecs_entity_t entity);

/** Get the text that KeyCapture took from Input.text this frame.
 *
 * @param world The world
 * @return UTF-8 text, empty when it took none, valid until the next frame
 */
const char* box_ui_text(
    const ecs_world_t *world);

/** Get the text that KeyCapture took from Input.paste this frame.
 *
 * @param world The world
 * @return UTF-8 text, empty when it took none, valid until the next frame
 */
const char* box_ui_paste(
    const ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
