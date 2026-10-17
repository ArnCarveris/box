/**
 * @file console/console.h
 * @brief Private state of the box.console module and the functions its sources share.
 */

#ifndef BOX_CONSOLE_INTERNAL_H
#define BOX_CONSOLE_INTERNAL_H

#include <box.h>

/** Lines of input history the console keeps */
#define BOX_CONSOLE_HISTORY_MAX (256)

/** Lines one wheel step scrolls */
#define BOX_CONSOLE_WHEEL_LINES (3)

/** Prompt in front of the input line and of the lines that ran */
#define BOX_CONSOLE_PROMPT "> "

/** What a line of the scrollback shows, which picks its color */
typedef enum box_console_kind_t {
    BoxConsoleOutput,   /**< Output of a command */
    BoxConsoleCommand,  /**< A line that ran, after the prompt */
    BoxConsoleError     /**< Output of a failed command */
} box_console_kind_t;

/** One line of the scrollback */
typedef struct box_console_line_t {
    char *text;                 /**< The line without its newline */
    box_console_kind_t kind;    /**< What the line shows */
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
    ecs_entity_t log;           /**< Layout column of the visible scrollback lines */
    ecs_entity_t input;         /**< Layout row of the input line */
    ecs_entity_t prompt;        /**< Text of the prompt */
    ecs_entity_t before;        /**< Text of the input line before the cursor */
    ecs_entity_t cursor;        /**< Rectangle of the cursor */
    ecs_entity_t after;         /**< Text of the input line after the cursor */
    ecs_vec_t lines;            /**< ecs_entity_t of the text of every visible line */
} box_console_ui_t;

/** State of the console of a world, owned by the module */
typedef struct box_console_t {
    ecs_entity_t module;        /**< The box.console module */
    ecs_entity_t scope;         /**< Root entity of the content, the view and the session */
    ecs_entity_t action;        /**< The toggle action */
    ecs_entity_t session;       /**< Terminal session, created by the first line that runs */
    ecs_entity_t view;          /**< View of the content while the console is open */
    box_console_ui_t ui;        /**< The content */
    ecs_vec_t lines;            /**< box_console_line_t of the scrollback */
    ecs_vec_t history;          /**< char* of the lines that ran, oldest first */
    int32_t history_at;         /**< Line of the history being edited, count for the draft */
    char *draft;                /**< Line that was edited before walking the history */
    char *line;                 /**< The input line, UTF-8 */
    int32_t length;             /**< Bytes of the input line */
    int32_t cursor;             /**< Byte of the input line the cursor sits before */
    ecs_vec_t typed;            /**< char of the text typed since the last update */
    bool pressed[BOX_KEY_COUNT];/**< Keys pressed since the last update */
    float wheel;                /**< Wheel movement since the last update */
    bool clear;                 /**< Ctrl+L was pressed since the last update */
    int32_t scroll;             /**< Scrollback lines scrolled up from the newest */
    bool shown;                 /**< The content is shown and the console takes the keyboard */
    bool enabled;               /**< The toggle action is enabled */
    bool relayout;              /**< Look or surface size changed since the last sync */
    bool refresh;               /**< Text changed since the last sync */
    ecs_entity_t surface;       /**< Surface the content fills, 0 while the world has none */
    float width;                /**< Surface width in points the content was laid out for */
    float height;               /**< Surface height in points the content was laid out for */
    ecs_query_t *bindings;      /**< Entities with a Binding */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
    ecs_query_t *views;         /**< Entities with a View */
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
