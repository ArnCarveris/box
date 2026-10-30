/**
 * @file console/console.h
 * @brief Private state of the box.console module and the functions its sources share.
 */

#ifndef BOX_CONSOLE_INTERNAL_H
#define BOX_CONSOLE_INTERNAL_H

#include <box.h>

/** Lines of input history the console keeps */
#define BOX_CONSOLE_HISTORY_MAX (256)

/** Rows one wheel step scrolls */
#define BOX_CONSOLE_WHEEL_LINES (3)

/** Scope of the prompt while the session is at the root */
#define BOX_CONSOLE_ROOT "~"

/** Symbol of the prompt after the scope, with the space before and after it */
#define BOX_CONSOLE_SYMBOL " $ "

/** Echoed after a line that Ctrl+C abandoned */
#define BOX_CONSOLE_INTERRUPT "^C"

/** Name of the command the hint line points to */
#define BOX_CONSOLE_HINT_HELP "help"

/** Line that starts the scrollback the first time the console opens */
#define BOX_CONSOLE_HINT "Flecs Shell  type " BOX_CONSOLE_HINT_HELP \
    " for a list of possible commands"

/** What a line of the scrollback shows, which picks its color */
typedef enum box_console_kind_t {
    BoxConsoleOutput,   /**< Output of a command */
    BoxConsoleCommand,  /**< A line that ran, after the prompt */
    BoxConsoleError     /**< Output of a failed command */
} box_console_kind_t;

/** What a part of a command line is, which picks its color from Console.syntax */
typedef enum box_console_syntax_t {
    BoxSyntaxName,      /**< Name of a known command or console command */
    BoxSyntaxUnknown,   /**< Name of no command */
    BoxSyntaxEntity,    /**< Entity argument that resolves */
    BoxSyntaxMissing,   /**< Entity argument that does not resolve */
    BoxSyntaxNumber,    /**< Number argument or number in a value expression */
    BoxSyntaxString,    /**< Quoted argument or string in a value expression */
    BoxSyntaxBoolean,   /**< Bool argument or true or false in a value expression */
    BoxSyntaxValue,     /**< Value expression in brackets */
    BoxSyntaxExtra,     /**< Argument beyond the parameters of the command */
    BoxSyntaxScope,     /**< Scope of the session in the prompt */
    BoxSyntaxSymbol,    /**< The $ of the prompt */
    BoxSyntaxGhost      /**< Suggested rest of the input line from the history */
} box_console_syntax_t;

/** A colored byte range of a command line */
typedef struct box_console_span_t {
    int32_t start;              /**< First byte */
    int32_t end;                /**< Byte after the range */
    box_console_syntax_t kind;  /**< What the range is */
} box_console_span_t;

/** One line of the scrollback */
typedef struct box_console_line_t {
    char *text;                 /**< The line without its newline */
    box_console_kind_t kind;    /**< What the line shows */
    ecs_vec_t spans;            /**< box_console_span_t of a line that ran, in bytes of text */
    int32_t hang;               /**< Byte its wrapped rows start under: the end of the prompt of
                                 *   a line that ran, the end of the indentation of others */
    int32_t rows;               /**< Rows the line wraps to at box_console_t.wrap, 0 until
                                 *   measured */
} box_console_line_t;

/** Entities of the console content, created the first time it opens */
typedef struct box_console_ui_t {
    ecs_entity_t root;          /**< Scene root */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    ecs_entity_t stack;         /**< Layout column that holds the panel and the space below it */
    ecs_entity_t panel;         /**< Panel rectangle, takes its share of the surface */
    ecs_entity_t below;         /**< Space under the panel */
    ecs_entity_t beside;        /**< Space right of the panel */
    ecs_entity_t edge;          /**< Line along the right edge of the panel */
    ecs_entity_t content;       /**< Layout column of the scrollback and the input line */
    ecs_entity_t log;           /**< Layout column of the visible scrollback lines, clipped */
    ecs_entity_t input;         /**< Element of the input line and its cursor */
    ecs_entity_t line;          /**< Text of the prompt and the input line, wrapped */
    ecs_entity_t cursor;        /**< Rectangle of the cursor, placed on the caret of the line */
    ecs_vec_t lines;            /**< ecs_entity_t of the text of every visible line, and of an
                                 *   empty row for every row the scrollback does not fill */
} box_console_ui_t;

/** State of the console of a world, owned by the module */
typedef struct box_console_t {
    ecs_entity_t module;        /**< The box.console module */
    ecs_entity_t scope;         /**< Root entity of the content, the view and the session */
    ecs_entity_t action;        /**< The toggle action */
    ecs_entity_t session;       /**< Shell session, created by the first line that runs */
    ecs_entity_t view;          /**< View of the content while the console is open */
    box_console_ui_t ui;        /**< The content */
    ecs_vec_t lines;            /**< box_console_line_t of the scrollback */
    ecs_vec_t history;          /**< char* of the lines that ran, oldest first */
    int32_t history_at;         /**< Line of the history being edited, count for the draft */
    char *draft;                /**< Line that was typed before walking the history, the lines
                                 *   the walk shows start with it */
    char *line;                 /**< The input line, UTF-8 */
    int32_t length;             /**< Bytes of the input line */
    int32_t cursor;             /**< Byte of the input line the cursor sits before */
    ecs_vec_t spans;            /**< box_console_span_t of the input line */
    char *colored;              /**< The input line the spans were found for, NULL for none */
    ecs_vec_t typed;            /**< char of the text typed since the last update */
    bool pressed[BOX_KEY_COUNT];/**< Keys pressed since the last update */
    float wheel;                /**< Wheel movement since the last update */
    bool chord[BOX_KEY_COUNT];  /**< Keys pressed with Ctrl held since the last update */
    bool greeted;               /**< The hint line was added, the console opened before */
    int32_t scroll;             /**< Scrollback rows scrolled up from the newest */
    int32_t rows;               /**< Scrollback rows that fit in the panel above the input */
    float wrap;                 /**< Width the rows of the scrollback lines were measured at */
    float wrap_size;            /**< Font size the rows of the scrollback were measured at */
    bool pending;               /**< A line could not be measured while its font loads */
    ecs_entity_t font;          /**< Font of the console text, box.text.DefaultFont */
    bool shown;                 /**< The content is shown and the console takes the keyboard */
    bool enabled;               /**< The toggle action is enabled */
    bool relayout;              /**< Look or surface size changed since the last sync */
    bool refresh;               /**< Text changed since the last sync */
    ecs_entity_t surface;       /**< Surface the content fills, 0 while the world has none */
    float width;                /**< Surface width in points the content was laid out for */
    float height;               /**< Surface height in points the content was laid out for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
} box_console_t;

/** Get the root entity of the content, the view and the session, creating it when needed.
 *
 * It is not under the module, so it is deleted with the entities of the application.
 *
 * @param world The world
 * @param con The console state
 * @return The root entity
 */
ecs_entity_t box_console_scope(
    ecs_world_t *world,
    box_console_t *con);

/** Insert the printable part of text at the cursor.
 *
 * @param con The console state
 * @param text The text
 * @param size Bytes of the text
 */
void box_console_insert_text(
    box_console_t *con,
    const char *text,
    int32_t size);

/** Insert typed text at the cursor and apply the keys pressed since the last update.
 *
 * @param world The world
 * @param con The console state
 * @return Whether Escape was pressed, which closes the console
 */
bool box_console_edit(
    ecs_world_t *world,
    box_console_t *con);

/** Run a line in the session of the console and add it and its output to the scrollback.
 *
 * A console command (cls) runs in the console and never reaches the session, help also lists
 * the console commands.
 *
 * @param world The world
 * @param con The console state
 * @param line The command line
 * @param error Set to whether the command failed, NULL to ignore
 * @return The output, which the caller frees
 */
char* box_console_exec(
    ecs_world_t *world,
    box_console_t *con,
    const char *line,
    bool *error);

/** Test whether a word is the name of a console command.
 *
 * @param word The word
 * @param len Length of the word
 * @return Whether the console runs the command itself
 */
bool box_console_is_command(
    const char *word,
    int32_t len);

/** Find the colored parts of a command line, from the tokens the shell splits it into and the
 * parameters of its command. Entity arguments resolve from the scope of the console session.
 *
 * @param world The world
 * @param con The console state
 * @param line The command line
 * @param spans Receives the box_console_span_t of the line, later ones over earlier ones
 */
void box_console_syntax(
    ecs_world_t *world,
    box_console_t *con,
    const char *line,
    ecs_vec_t *spans);

/** Get the scope of the console session.
 *
 * @param world The world
 * @param con The console state
 * @return The scope, 0 for the root or before the session exists
 */
ecs_entity_t box_console_session_scope(
    const ecs_world_t *world,
    const box_console_t *con);

/** Get the prompt of the console session: its scope, ~ at the root, and $.
 *
 * @param world The world
 * @param con The console state
 * @param spans Receives the box_console_span_t of the scope and the $, NULL to ignore
 * @return The prompt, which the caller frees
 */
char* box_console_prompt(
    const ecs_world_t *world,
    const box_console_t *con,
    ecs_vec_t *spans);

/** Get the suggestion of the history for the input line: the rest of the newest line that ran
 * that starts with the input and is longer, while the cursor is at the end of a line that is
 * not empty and the history is not being walked.
 *
 * @param con The console state
 * @return The rest of the history line, NULL for no suggestion
 */
const char* box_console_ghost(
    const box_console_t *con);

/** Add the hint line in front of the scrollback, the first time only, and drop the oldest lines
 * while the scrollback has more rows than the console keeps.
 *
 * @param world The world
 * @param con The console state
 */
void box_console_greet(
    ecs_world_t *world,
    box_console_t *con);

/** Empty the scrollback and reset the scroll position, keeping the input line and history.
 *
 * @param con The console state
 */
void box_console_clear(
    box_console_t *con);

/** Private singleton that points to the console state of a world */
typedef struct ConsoleHost {
    box_console_t *con; /**< The state, freed when the world is deleted */
} ConsoleHost;

extern ECS_COMPONENT_DECLARE(ConsoleHost);

/** Free the scrollback, the history and the input line.
 *
 * @param con The console state
 */
void box_console_edit_fini(
    box_console_t *con);

/** Create the scene, camera and elements of the console content.
 *
 * @param world The world
 * @param con The console state
 */
void box_console_ui_build(
    ecs_world_t *world,
    box_console_t *con);

/** Get the rows a scrollback line wraps to at the width of the panel, measuring it when its
 * rows are not known for that width yet.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 * @param l The line
 * @return The rows, 1 while the font of the console loads
 */
int32_t box_console_line_rows(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console,
    box_console_line_t *l);

/** Get the rows the whole scrollback wraps to at the width of the panel.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 * @return The rows
 */
int32_t box_console_scrollback_rows(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console);

/** Write the look, the size, the visible scrollback and the input line into the content.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 */
void box_console_ui_sync(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console);

#endif
