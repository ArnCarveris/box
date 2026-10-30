/**
 * @file box/console.h
 * @brief The in game console: an overlay that runs command lines in a flecs shell session.
 *
 * The toggle action (the grave key) opens and closes it. The console is UI content in its own
 * scene, drawn by its own view after every other view (at
 * BOX_VIEW_ORDER_CONSOLE), and takes the keyboard while open.
 *
 * The console runs cls itself, so it never reaches the session: cls empties the scrollback and
 * Ctrl+L does the same without touching the input line. help in the console lists cls too.
 * Command lines are colored in the console only (ConsoleSyntax), never in sessions or logs.
 */

#ifndef BOX_CONSOLE_H
#define BOX_CONSOLE_H

#include "types.h"

#ifdef BOX_CONSOLE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Path of the action that opens and closes the console, bound to the grave key */
#define BOX_CONSOLE_TOGGLE "box.console.toggle"

/** Name of the root entity of the console content, its view and its session */
#define BOX_CONSOLE_SCOPE "console"

/** Path of the shell session the console runs its lines in */
#define BOX_CONSOLE_SESSION "console.session"

/** Path of the scene root of the console content */
#define BOX_CONSOLE_UI "console.ui"

/** Path of the view of the console content while it is open */
#define BOX_CONSOLE_VIEW "console.view"

/** Colors of the parts of a command line in the console, as Console.syntax.
 *
 * Lines are split as the flecs shell splits them: the first token is the command name, the
 * others take the color of the type of their parameter. Entities resolve from the scope of the
 * session.
 *
 * The input line is colored as it is typed; a line that ran keeps the colors it had then.
 * Output keeps Console.text and Console.error, other parts Console.input or Console.command.
 */
ECS_STRUCT(ConsoleSyntax, {
    rgb name;       /**< Name of a known command or console command */
    rgb unknown;    /**< Name of no command */
    rgb entity;     /**< Entity argument that resolves from the scope of the session */
    rgb missing;    /**< Entity argument that does not resolve */
    rgb number;     /**< Argument of a numeric parameter, a number in a value expression */
    rgb string;     /**< Quoted argument or a string in a value expression */
    rgb boolean;    /**< Argument of a bool parameter, true or false in a value expression */
    rgb value;      /**< Value expression in brackets: {...}, [...] or (...) */
    rgb extra;      /**< Argument beyond the parameters of the command */
});

/** The console singleton: whether it is open and how it looks.
 *
 * The panel is a layout: a root that fills the surface in points holds the panel, which takes
 * the width and height fractions of the surface from the top left. It follows the surface when
 * it is resized. A fraction of zero or less is a panel of zero size, one or more fills the axis.
 *
 * While open, typed and pasted text (Input.paste) and key presses go to the input line and no
 * action but the toggle sees the keyboard or mouse buttons. Enter runs the line, Up and Down walk
 * the lines that ran, Page Up, Page Down and the wheel scroll, Escape or the toggle close it.
 *
 * Lines longer than the panel wrap at its width minus the padding, between words and inside a
 * word longer than a row. Rows a line wraps to start after the prompt of a line that ran or
 * of the input line, and under the leading indentation of an output line. Scrolling and the
 * scrollback count rows.
 */
ECS_STRUCT(Console, {
    bool open;           /**< Shown and taking the keyboard, the toggle action flips it */
    float width;         /**< Part of the surface width covered from the left, default 0.5 */
    float height;        /**< Part of the surface height covered from the top, default 1 */
    float font_size;     /**< Em size of the text in points, default 16 */
    float line_height;   /**< Points from one line to the next, default 20 */
    float padding;       /**< Points between the edges of the panel and the text, default 10 */
    int32_t scrollback;  /**< Rows of output the console keeps, default 1000 */
    rgba background;     /**< Color of the panel, default translucent dark blue */
    rgb border;          /**< Color of the line along the bottom edge */
    rgb text;            /**< Color of the output of a command */
    rgb command;         /**< Color of a line that ran, echoed after the prompt */
    rgb error;           /**< Color of the output of a failed command */
    rgb input;           /**< Color of the input line and its cursor */
    ConsoleSyntax syntax;/**< Colors of the parts of command lines, see ConsoleSyntax */
});

/** Register the box.console module.
 *
 * Creates the Console singleton and the toggle action. The action is enabled while the world
 * has a Surface, the console content, its view and its session are created when it opens.
 *
 * @param world The world
 */
void BoxConsoleImport(
    ecs_world_t *world);

/** Run a line in the console as if it was typed and entered.
 *
 * The line and its output are added to the scrollback and the input history, cls empties the
 * scrollback and returns an empty output. box.console.run(line) does the same in a script.
 *
 * @param world The world
 * @param line The command line
 * @param error Set to whether the command failed, NULL to ignore
 * @return The output, which the caller frees
 */
char* box_console_run(
    ecs_world_t *world,
    const char *line,
    bool *error);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
