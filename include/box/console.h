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
 * Output is colored by the type of the result of the command (ConsoleOutput), like the terminal
 * of the flecs explorer, while box_command_run and HTTP sessions keep returning plain text.
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
 * Output is colored with ConsoleOutput, other parts Console.input or Console.command.
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

/** Colors of the output of commands in the console, as Console.output.
 *
 * The console shows the result of a command by its type, like the terminal of the flecs
 * explorer: entity paths relative to the scope of the session with the parent part dimmed, tree
 * guides, query rows, grouped help, entity info, documentation, grep matches, script lines with
 * script syntax colors, diffs and values as JSON. Errors start with "error: " in Console.error,
 * the further lines of an error are dimmed. ANSI color codes in output become the ANSI colors.
 * Text without a color of its own is Console.text.
 */
ECS_STRUCT(ConsoleOutput, {
    rgb dim;        /**< Parent part of paths, tree guides, line numbers, error details */
    rgb entity;     /**< Entity names */
    rgb component;  /**< Components and ids */
    rgb key;        /**< Member names of values and labels of fields */
    rgb number;     /**< Numbers */
    rgb string;     /**< Strings */
    rgb boolean;    /**< true and false */
    rgb keyword;    /**< null and keywords of flecs script */
    rgb command;    /**< Command names in help */
    rgb param;      /**< Parameters of commands in help */
    rgb type;       /**< Types of parameters in help */
    rgb brief;      /**< Descriptions of commands and parameters in help */
    rgb group;      /**< Group headers of help */
    rgb match;      /**< Text a grep matched */
    rgb added;      /**< Added lines of a diff */
    rgb removed;    /**< Removed lines of a diff */
    rgb red;        /**< ANSI red, code 31 */
    rgb green;      /**< ANSI green, code 32 */
    rgb yellow;     /**< ANSI yellow, code 33 */
    rgb blue;       /**< ANSI blue, code 34 */
    rgb magenta;    /**< ANSI magenta, code 35 */
    rgb cyan;       /**< ANSI cyan, code 36 */
    rgb white;      /**< ANSI white, code 37 */
    rgb grey;       /**< ANSI grey, code 90 */
});

/** The console singleton: whether it is open and how it looks.
 *
 * The panel takes the width and height fractions of the surface from the top left and follows
 * its resizes, a fraction of zero or less is a panel of zero size, one or more fills the axis.
 * Lines wrap at its width minus the padding, their rows under the prompt or the indentation.
 *
 * While open, typed and pasted text (Input.paste) and key presses go to the input line and no
 * action but the toggle sees the keyboard or mouse buttons. Enter runs the line, Up and Down walk
 * the lines that ran, Page Up, Page Down and the wheel scroll rows, Escape or the toggle close it.
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
    rgb text;            /**< Color of output without a color of its own */
    rgb command;         /**< Color of a line that ran, echoed after the prompt */
    rgb error;           /**< Color of the error: label of a failed command */
    rgb input;           /**< Color of the input line and its cursor */
    ConsoleSyntax syntax;/**< Colors of the parts of command lines, see ConsoleSyntax */
    ConsoleOutput output;/**< Colors of the output of commands, see ConsoleOutput */
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
 * scrollback and returns an empty output. The output is the text the console shows, which for
 * structured results (trees, query rows, entity info, ...) is laid out for reading and differs
 * from the text of box_command_run. box.console.run(line) does the same in a script.
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
