/**
 * @file ui/ui.h
 * @brief Private state of box.ui and the functions its sources share: the focus model, the key
 * claim, the event waiters and the temporary flecs workarounds of shim.c.
 */

#ifndef BOX_UI_INTERNAL_H
#define BOX_UI_INTERNAL_H

#include <box.h>
#include "../input/input.h"
#include "../picking/picking.h"

/** Most entities an event bubbles through or an ancestor walk visits */
#define BOX_UI_DEPTH_MAX (256)

/** Most rounds of resuming tasks after the events of one call */
#define BOX_UI_PROGRESS_MAX (16)

/** What a script task waits for */
typedef enum box_ui_wait_kind_t {
    BoxUiWaitFocus,    /**< on.focus */
    BoxUiWaitBlur,     /**< on.blur */
    BoxUiWaitKey,      /**< on.key */
    BoxUiWaitActivate, /**< on.activate */
    BoxUiWaitChange,   /**< on.change */
    BoxUiWaitSubmit,   /**< on.submit */
    BoxUiWaitCount     /**< Number of kinds */
} box_ui_wait_kind_t;

typedef struct box_ui_state_t box_ui_state_t;

/** Context of an async function that waits for one kind of event */
typedef struct box_ui_wait_ctx_t {
    box_ui_state_t *st;      /**< The module state */
    box_ui_wait_kind_t kind; /**< What the function waits for */
} box_ui_wait_ctx_t;

/** A script task waiting for an event on an entity */
typedef struct box_ui_waiter_t {
    ecs_entity_t target;         /**< Entity the task listens on */
    box_ui_wait_kind_t kind;     /**< What it waits for */
    ecs_script_future_t *future; /**< Future the event resolves */
} box_ui_waiter_t;

/** Private state of a TextEdit: the parts the engine built and what the last sync saw */
typedef struct TextEditState {
    ecs_entity_t content;   /**< The ClipChildren element the text scrolls in */
    ecs_entity_t text;      /**< The Text */
    ecs_entity_t selection; /**< The selection Rect */
    ecs_entity_t caret;     /**< The caret Rect */
    char *value;            /**< TextEdit.value the engine last wrote or took in */
    double blink;           /**< Frame clock seconds the caret blink last restarted */
    bool focused;           /**< The entity had the focus at the last sync */
    bool added_layout;      /**< The engine added the Layout of the entity */
    bool added_cursor;      /**< The engine added the Cursor of the entity */
} TextEditState;

extern ECS_COMPONENT_DECLARE(TextEditState);

/** One character of the value of a TextEdit, decoded for the word moves */
typedef struct box_ui_char_t {
    int32_t offset; /**< Byte offset of its first byte */
    int32_t kind;   /**< Its word class: 0 space, 1 word character, 2 other */
} box_ui_char_t;

/** Private state of the module, kept on the heap behind the UiState singleton */
struct box_ui_state_t {
    ecs_query_t *focusables;     /**< Entities with Focusable */
    ecs_query_t *scopes;         /**< Entities with FocusScope */
    ecs_vec_t waiters;           /**< box_ui_waiter_t of the on.* functions */
    ecs_vec_t order;             /**< ecs_entity_t scratch of a Tab walk */
    ecs_vec_t text;              /**< char of the text KeyCapture took this frame, terminated */
    ecs_vec_t paste;             /**< char of the paste KeyCapture took this frame, terminated */
    box_ui_wait_ctx_t waits[BoxUiWaitCount]; /**< Contexts of the waiting functions */
    ecs_entity_t focused;        /**< Entity with the focus, 0 for none */
    ecs_entity_t previous;       /**< Entity that had the focus before */
    bool visible;                /**< The focus came from the keyboard */
    bool keyboard;               /**< The last press or Tab was Tab, so ui.focus is visible */
    ecs_entity_t press;          /**< Target of the held left button press, 0 for none */
    ecs_entity_t press_handler;  /**< Handler of that press */
    ecs_entity_t space;          /**< Focused entity Space went down on, 0 for none */
    ecs_entity_t hold_source;    /**< Focused ActionTrigger element Space holds, 0 for none */
    ecs_entity_t hold_action;    /**< The action it holds */
    bool console;                /**< The console was open when the keys of the frame arrived */
    bool progress;               /**< Futures were resolved since the tasks last resumed */
    ecs_entity_t theme;          /**< The box.ui.theme entity */
    ecs_query_t *edits;          /**< Entities with a TextEdit */
    ecs_vec_t edit_list;         /**< ecs_entity_t scratch of the TextEdit update */
    ecs_vec_t edit_chars;        /**< box_ui_char_t scratch of a word move */
    ecs_entity_t edit_press;     /**< TextEdit a held left press selects in, 0 for none */
    int32_t edit_clicks;         /**< Clicks of that press: 1, 2 for words, 3 for everything */
    int32_t edit_word[2];        /**< Byte range of the word a double click selected */
    float edit_pointer;          /**< X of the pointer of the press in its content element */
    ecs_entity_t edit_click;     /**< TextEdit the last left press went to */
    double edit_click_time;      /**< Frame clock seconds of that press */
    vec2 edit_click_at;          /**< Window point of that press */
};

/** Singleton that owns the state of the module */
typedef struct UiState {
    box_ui_state_t *state; /**< The state */
} UiState;

extern ECS_COMPONENT_DECLARE(UiState);

/** Get the state of the module, NULL when it is not imported or the world is torn down.
 *
 * @param world The world or a stage
 * @return The state
 */
box_ui_state_t* box_ui_state(
    const ecs_world_t *world);

/** Check whether an entity can take the focus: alive, Focusable, not disabled, not a prefab.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it can take the focus
 */
bool box_ui_can_focus(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Check whether an entity or one of its ancestors has an id.
 *
 * @param world The world
 * @param e The entity
 * @param id The id
 * @return The first entity on the way up with the id, 0 when there is none
 */
ecs_entity_t box_ui_up(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id);

/** Move the focus, publishing Focused and FocusStatus and sending on.blur and on.focus.
 *
 * @param world The world
 * @param st The module state
 * @param e The entity, 0 clears the focus; it must be able to take the focus
 * @param visible Whether the focus comes from the keyboard
 */
void box_ui_focus_set(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    bool visible);

/** Clear the focus when the focused entity can no longer take it.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_focus_check(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Move the focus as Tab or Shift+Tab does.
 *
 * @param world The world
 * @param st The module state
 * @param pst The picking state, whose views give the first FocusScope
 * @param back Whether the focus moves back, as Shift+Tab does
 * @return Whether a FocusScope took the key
 */
bool box_ui_focus_step(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_pick_state_t *pst,
    bool back);

/** Register Focusable, the observer that clears the focus and the FocusStatus singleton.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_focus_import(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Install the key claim in box.input and the hook in box.picking.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_keys_import(
    ecs_world_t *world,
    box_ui_state_t *st);

/** End the Space hold of a focused ActionTrigger element.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_hold_release(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Resolve the waiters of an event on a target and every ancestor of it.
 *
 * @param world The world
 * @param st The module state
 * @param kind What the event is
 * @param target The target
 * @param type Type of the value
 * @param value The value the waiters resolve with
 * @return Number of resolved waiters
 */
int32_t box_ui_deliver(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_wait_kind_t kind,
    ecs_entity_t target,
    ecs_entity_t type,
    const void *value);

/** Resume the tasks whose futures were resolved, unless called from inside a task.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_progress(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Register an async function that waits for one kind of event on an entity.
 *
 * @param world The world
 * @param st The module state
 * @param parent The scope of the function, flecs.script.on
 * @param name Name of the function
 * @param kind What it waits for
 * @param type What it resolves with
 */
void box_ui_wait_function(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t parent,
    const char *name,
    box_ui_wait_kind_t kind,
    ecs_entity_t type);

/** Register on.change, on.submit, ui.changed and ui.submit.
 *
 * @param world The world
 * @param st The module state
 * @param on The flecs.script.on scope
 * @param ui The flecs.script.ui scope
 */
void box_ui_events_import(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t on,
    ecs_entity_t ui);

/** Register the temporary script functions on.focus, on.blur, on.key, on.activate and ui.focus.
 *
 * @param world The world
 * @param st The module state
 * @param on The flecs.script.on scope
 * @param ui The flecs.script.ui scope
 */
void box_ui_shim_import(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t on,
    ecs_entity_t ui);

/** Send on.blur to the entity that lost the focus and on.focus to the one that gained it.
 *
 * @param world The world
 * @param st The module state
 * @param from Entity that lost the focus, 0 for none
 * @param from_visible Whether its focus came from the keyboard
 * @param to Entity that gained the focus, 0 for none
 * @param to_visible Whether the new focus comes from the keyboard
 */
void box_ui_shim_focus(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t from,
    bool from_visible,
    ecs_entity_t to,
    bool to_visible);

/** Send on.key for a key event to the focused entity.
 *
 * @param world The world
 * @param st The module state
 * @param focused The focused entity
 * @param evt The key event
 */
void box_ui_shim_key(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t focused,
    const UiKeyEvent *evt);

/** Send on.activate for an activation of an entity.
 *
 * @param world The world
 * @param st The module state
 * @param evt The activation, whose target is the activated entity
 */
void box_ui_shim_activate(
    ecs_world_t *world,
    box_ui_state_t *st,
    const UiActivateEvent *evt);

/** Register TextEdit, TextEditStatus, their hooks, observer and update system.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_text_edit_import(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Insert the text and the paste KeyCapture took this frame into the focused TextEdit.
 *
 * @param world The world
 * @param st The module state
 */
void box_ui_text_edit_input(
    ecs_world_t *world,
    box_ui_state_t *st);

/** Edit the focused TextEdit for a key event.
 *
 * @param world The world
 * @param st The module state
 * @param e The focused entity
 * @param evt The key event
 */
void box_ui_text_edit_key(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    const UiKeyEvent *evt);

/** Place the caret or select in a TextEdit for a pointer event after the press focused it.
 *
 * @param world The world
 * @param st The module state
 * @param src The event
 * @param pick The pick it was delivered by
 * @param pressed The buttons that went down
 * @param released The buttons that came up
 */
void box_ui_text_edit_pointer(
    ecs_world_t *world,
    box_ui_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released);

/** Register UiTheme, the box.ui.theme entity and ui.sibling, and load the widget library.
 *
 * @param world The world
 * @param ui The flecs.script.ui scope
 */
void box_ui_widgets_import(
    ecs_world_t *world,
    ecs_entity_t ui);

#endif
