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

/** Name of the help group of the console commands */
#define BOX_CONSOLE_HELP_GROUP "console"

/** Candidates Tab completion collects at most, so a large world stays cheap */
#define BOX_CONSOLE_COMPLETE_MAX (256)

/** Candidates the list under the prompt shows before a dimmed "... N more" */
#define BOX_CONSOLE_COMPLETE_VISIBLE (24)

/** Seconds the prompt flashes when Tab finds nothing to complete */
#define BOX_CONSOLE_BELL (0.2f)

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
    BoxSyntaxGhost,     /**< Suggested rest of the input line from the history */
    BoxOutputText,      /**< Output in Console.text, the message of an error */
    BoxOutputError,     /**< The error: label of an error, Console.error */
    BoxOutputDim,       /**< Parent part of a path, tree guides, line numbers, error details */
    BoxOutputEntity,    /**< Entity name */
    BoxOutputComponent, /**< Component or id */
    BoxOutputKey,       /**< Member name of a value, label of a field */
    BoxOutputNumber,    /**< Number */
    BoxOutputString,    /**< String */
    BoxOutputBoolean,   /**< true or false */
    BoxOutputKeyword,   /**< null, keyword of flecs script */
    BoxOutputCommand,   /**< Command name in help */
    BoxOutputParam,     /**< Parameter in help */
    BoxOutputType,      /**< Type of a parameter in help */
    BoxOutputBrief,     /**< Description in help */
    BoxOutputGroup,     /**< Group header of help */
    BoxOutputMatch,     /**< Text a grep matched */
    BoxOutputAdded,     /**< Added line of a diff */
    BoxOutputRemoved,   /**< Removed line of a diff */
    BoxOutputRed,       /**< ANSI red */
    BoxOutputGreen,     /**< ANSI green */
    BoxOutputYellow,    /**< ANSI yellow */
    BoxOutputBlue,      /**< ANSI blue */
    BoxOutputMagenta,   /**< ANSI magenta */
    BoxOutputCyan,      /**< ANSI cyan */
    BoxOutputWhite,     /**< ANSI white */
    BoxOutputGrey,      /**< ANSI grey */
    BoxSpanNone         /**< No color of its own, never stored in a span */
} box_console_syntax_t;

/** A colored byte range of a command line or of output */
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

/** A candidate of Tab completion */
typedef struct box_console_candidate_t {
    char *text;                 /**< Text that replaces the word, with its parent part */
    char *label;                /**< Text the list shows, the name without the parent part */
    char *brief;                /**< Description of a command, NULL for none */
    box_console_syntax_t kind;  /**< Color of the label: command, entity, module or component */
    ecs_entity_t entity;        /**< The entity of an entity candidate, 0 for others */
} box_console_candidate_t;

/** State of Tab completion: the candidates of the word under the cursor and the list of them */
typedef struct box_console_complete_t {
    ecs_vec_t candidates;       /**< box_console_candidate_t, sorted by text */
    bool capped;                /**< More candidates matched than BOX_CONSOLE_COMPLETE_MAX */
    bool open;                  /**< The list is shown under the prompt */
    int32_t active;             /**< Candidate Tab cycled to and inserted, -1 for none */
    int32_t start;              /**< First byte of the input line the candidate replaces */
    int32_t end;                /**< Byte after the text the candidate replaces */
    float bell;                 /**< Seconds the prompt still flashes, 0 for no bell */
} box_console_complete_t;

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
    ecs_entity_t menu;          /**< Text of the candidate list under the input line, wrapped */
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
    bool shifted[BOX_KEY_COUNT];/**< Keys pressed with Shift held since the last update */
    box_console_complete_t complete; /**< Tab completion of the input line */
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

/** Get a console command the console runs itself.
 *
 * @param index The command, from 0
 * @param brief Receives its description, NULL to ignore
 * @return Its name, NULL past the last command
 */
const char* box_console_command_at(
    int32_t index,
    const char **brief);

/** Complete the word under the cursor, as Tab does: insert the one candidate, the common prefix
 * of several, or open the list of them when the prefix adds nothing; ring the bell for none.
 * While the list is open Tab, or Shift+Tab with back, cycles through it.
 *
 * @param world The world
 * @param con The console state
 * @param back Whether Shift was held, which cycles backward
 */
void box_console_complete_tab(
    ecs_world_t *world,
    box_console_t *con,
    bool back);

/** Accept the candidate Tab cycled to, as Enter does while the list is open: it stays in the
 * line followed by a space, or a . for an entity with children, and the list closes.
 *
 * @param world The world
 * @param con The console state
 * @return Whether a candidate was accepted, false lets Enter run the line
 */
bool box_console_complete_accept(
    ecs_world_t *world,
    box_console_t *con);

/** Close the candidate list and forget the candidates, keeping the input line.
 *
 * @param con The console state
 */
void box_console_complete_close(
    box_console_t *con);

/** Replace a range of the input line with text and put the cursor after it.
 *
 * @param con The console state
 * @param from First byte of the range
 * @param to Byte after the range
 * @param text The text
 */
void box_console_replace(
    box_console_t *con,
    int32_t from,
    int32_t to,
    const char *text);

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

/** Find the entity an entity argument names, as the shell resolves it.
 *
 * A path that starts with . or .. is relative to the scope and separated by /, other names
 * resolve from the scope and its parents, then from the root. $ resolves to nothing here.
 *
 * @param world The world
 * @param scope The scope of the session, 0 for the root
 * @param text The argument
 * @param found Set to whether the text names an entity, true for . at the root
 * @return The entity, 0 for the root or when it names none
 */
ecs_entity_t box_console_syntax_resolve(
    const ecs_world_t *world,
    ecs_entity_t scope,
    const char *text,
    bool *found);

/** The word under the cursor that Tab completes */
typedef struct box_console_word_t {
    int32_t start;              /**< First byte of the word */
    int32_t end;                /**< The cursor, the byte after the part of the word it ends */
    int32_t index;              /**< Token of the line, 0 for the command name */
    char *command;              /**< Command name of the line, NULL for the first token */
} box_console_word_t;

/** Find the word under the cursor like completionContext of the explorer terminal: a bare word
 * that ends at the cursor, or the empty word after whitespace, not in a string or a value.
 *
 * @param line The command line
 * @param cursor Byte the cursor sits before
 * @param out Receives the word, its command freed by the caller
 * @return Whether there is a word to complete
 */
bool box_console_syntax_word_at(
    const char *line,
    int32_t cursor,
    box_console_word_t *out);

/** Find the command of a name as the shell does: the first function with that name in the
 * scopes of EcsScriptShell.command_paths.
 *
 * @param world The world
 * @param name The command name
 * @return The function, 0 when no command has that name
 */
ecs_entity_t box_console_syntax_command(
    const ecs_world_t *world,
    const char *name);

/** Copy a token of a command line as the shell passes it, without its quotes or backticks.
 *
 * @param line The command line
 * @param index The token, 0 for the command name
 * @return The text, which the caller frees, NULL when the line has fewer tokens
 */
char* box_console_syntax_token(
    const char *line,
    int32_t index);

/** Text of console output being built and the colored ranges of its bytes */
typedef struct box_console_out_t {
    ecs_strbuf_t buf;           /**< The text */
    ecs_vec_t spans;            /**< box_console_span_t of the text, later ones over earlier ones */
} box_console_out_t;

/** Start an empty output.
 *
 * @param out The output
 */
void box_console_out_init(
    box_console_out_t *out);

/** Free the text and the spans of an output.
 *
 * @param out The output
 */
void box_console_out_fini(
    box_console_out_t *out);

/** Get the bytes written to an output.
 *
 * @param out The output
 * @return The length of the text
 */
int32_t box_console_out_len(
    box_console_out_t *out);

/** Append text to an output in a color.
 *
 * @param out The output
 * @param kind What the text is, BoxSpanNone for no color of its own
 * @param text The text
 * @param len Bytes of the text, -1 for all of it
 */
void box_console_out_add(
    box_console_out_t *out,
    box_console_syntax_t kind,
    const char *text,
    int32_t len);

/** Color a range of the text of an output.
 *
 * @param out The output
 * @param kind What the range is, BoxSpanNone adds nothing
 * @param start First byte
 * @param end Byte after the range
 */
void box_console_out_mark(
    box_console_out_t *out,
    box_console_syntax_t kind,
    int32_t start,
    int32_t end);

/** Take the text of an output, the spans stay with it.
 *
 * @param out The output
 * @return The text, which the caller frees
 */
char* box_console_out_text(
    box_console_out_t *out);

/** Append an error: the error: label, the first line of the message and its further lines
 * dimmed. ANSI color codes in the message become colors.
 *
 * @param out The output
 * @param msg The message, with or without the BOX_COMMAND_ERROR prefix
 */
void box_console_out_error(
    box_console_out_t *out,
    const char *msg);

/** Append text in which ANSI color codes (ESC[0;31m, or [[0;31m) become colors.
 *
 * @param out The output
 * @param base What text outside of a color code is, BoxSpanNone for no color of its own
 * @param text The text
 * @param len Bytes of the text, -1 for all of it
 */
void box_console_out_ansi(
    box_console_out_t *out,
    box_console_syntax_t base,
    const char *text,
    int32_t len);

/** Run a line in a shell session and append its result as the console shows it: laid out and
 * colored by the type of the result, like the terminal widgets of the flecs explorer, or the
 * errors the command logged.
 *
 * @param world The world
 * @param con The console state
 * @param session The session
 * @param line The command line
 * @param out The output
 * @param error Set to whether the command logged an error
 */
void box_console_out_run(
    ecs_world_t *world,
    box_console_t *con,
    ecs_entity_t session,
    const char *line,
    box_console_out_t *out,
    bool *error);

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
