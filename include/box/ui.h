/**
 * @file box/ui.h
 * @brief UI: the keyboard focus, Tab navigation, keys kept from the bindings, widget events and
 * the text field.
 *
 * One Focusable entity holds the keyboard focus. A press or Tab moves it, KeyCapture keeps the
 * keys from the game's bindings, and widgets report changes to the scope that uses them.
 * TextEdit is a single line text field whose editing is done by the engine.
 */

#ifndef BOX_UI_H
#define BOX_UI_H

#include "types.h"
#include "input.h"
#include "material.h"
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
 * Tab passes over the entities that a ClipChildren ancestor clips away entirely, such as the
 * content of a collapsed section or a hidden tab page. It does not pass over the content that a
 * scrolling clip (a ClipChildren with a LayoutScroll) hides, which a scroll view scrolls into
 * view when it gets the focus.
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

/** Colors and metrics of the widgets, on the box.ui.theme entity.
 *
 * The widgets of box.widgets read box.ui.theme[UiTheme] reactively, so setting another value
 * restyles every widget in the next frame. A new UiTheme holds box.widgets.DarkTheme, the other
 * preset is box.widgets.LightTheme. Lengths are logical pixels.
 */
ECS_STRUCT(UiTheme, {
    Rgb background;         /**< Behind the panels, the window */
    Rgb surface;            /**< Panels and the face of controls */
    Rgb surface_alt;        /**< Insets: tracks, boxes and fields */
    Rgb border;             /**< Edges of panels and controls */
    Rgb text;               /**< Body text */
    Rgb text_muted;         /**< Secondary text: captions, hints */
    Rgb accent;             /**< Primary buttons, checked and filled parts */
    Rgb accent_text;        /**< Text and marks on the accent */
    Rgb hover;              /**< Face of a hovered control */
    Rgb pressed;            /**< Face of a pressed control */
    Rgb focus;              /**< The focus ring */
    Rgb danger;             /**< Destructive buttons and errors */
    Rgb selection;          /**< Behind selected text */
    Rgb caret;              /**< The text caret */
    float disabled_opacity; /**< Opacity of a disabled widget */
    float font_size;        /**< Body text */
    float small_font_size;  /**< Captions */
    float heading_font_size; /**< Headings and panel titles */
    float radius;           /**< Corner radius of controls */
    float padding;          /**< Space inside panels and controls */
    float spacing;          /**< Space between the children of a panel or a row */
    float control_height;   /**< Height of buttons, fields and rows of controls */
    float focus_width;      /**< Width of the focus ring */
    float caret_blink;      /**< Seconds of one on or off phase of the caret, 0 for steady */
});

/** A single line text field whose caret, selection, clipboard and scrolling the engine edits.
 *
 * value is the source of truth. An edit writes it with ecs_modified and reports on.change on the
 * entity; typed text and pastes replace the selection, control characters are dropped and
 * newlines and tabs become spaces. A value set from script or C keeps the caret, unreported.
 *
 * Adding it adds Focusable, KeyCapture, TextEditStatus, Cursor {CursorText} and a Layout unless
 * it has them, removing it removes them. Private children colored from UiTheme draw it and pass
 * picks to it; it is one line high and as wide as its LayoutItem makes it.
 */
ECS_STRUCT(TextEdit, {
    char *value;         /**< The text, UTF-8 */
    char *placeholder;   /**< Shown in UiTheme.text_muted while value is empty, also focused */
    int32_t max_length;  /**< Most characters, an insertion is cut at a character; 0 no limit */
    float size;          /**< Em size in logical pixels, default 16 */
    ecs_entity_t font;   /**< Entity with a Font, default box.text.DefaultFont */
    bool password;       /**< Every character is drawn as U+2022, copy and cut do nothing */
    bool read_only;      /**< The text can be selected and copied only */
});

/** Where the caret of a TextEdit is, written by the engine and read only for applications.
 *
 * Keys repeat: Left and Right move by character, by word with Ctrl (Alt on macOS); Home, End, Up
 * and Down to the ends (also Cmd+Left/Right on macOS); Shift extends. Backspace and Delete erase
 * (a word with Ctrl), Ctrl+A/C/X (Cmd on macOS) select all, copy, cut; Escape blurs, Enter submits.
 *
 * A press places the caret, a drag selects and scrolls past an edge, a double click selects a
 * word, a triple click all (within 0.5 s of the frame clock and 4 points), shift extends. The
 * caret blinks with UiTheme.caret_blink and restarts on edits; unfocused, caret and selection hide.
 */
ECS_STRUCT(TextEditStatus, {
    int32_t caret;  /**< Byte offset of the caret in value */
    int32_t anchor; /**< Byte offset of the other end of the selection, caret when none */
    float scroll;   /**< Logical pixels the text is shifted left to keep the caret in view */
    bool focused;   /**< The entity has the focus, so the caret and the selection show */
});

/** Register the box.ui module.
 *
 * Registers the async script functions flecs.script.on.focus, blur, key, activate, change and
 * submit, each taking a target and resolving on the target and its ancestors, and ui.focus,
 * ui.changed and ui.submit, which resolve at once (await ui.changed(this)).
 *
 * Creates box.ui.theme with a UiTheme and loads the widget library box.widgets as a managed
 * script from etc/ui/widgets.flecs, found as a Font.path is; without it there are no widgets.
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

/** Find where a word move of a TextEdit goes from a byte offset, as Ctrl+Left and Ctrl+Right do.
 *
 * Words are runs of letters and digits (ASCII and every code point above U+007F, characters as
 * box_utf8_next decodes them) and runs of other characters that are not spaces. A move skips the
 * spaces next to the offset, then one run: forward to its end, back to its start.
 *
 * Ctrl+Backspace and Ctrl+Delete erase what the move passes over. A double click selects the run
 * under the pointer, spaces included. In a password a word move goes to the start or the end.
 *
 * @param text UTF-8 text
 * @param offset Byte offset, moved to the start of the character it is in
 * @param forward Whether the move goes right
 * @return The byte offset the move ends at
 */
int32_t box_ui_word_move(
    const char *text,
    int32_t offset,
    bool forward);

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
