/**
 * @file box/task.h
 * @brief The work box does in the background: the InProgressTask singleton and the API through
 * which modules report it.
 *
 * Every module reports its own work, such as the shaders it compiles or the fonts it loads, as
 * tasks it starts, advances and ends. InProgressTask is their aggregate, which box.task writes
 * at the end of every frame: tasks of the same name combine into one entry.
 *
 * A task is blocking when the scene cannot be shown as intended until it finishes, such as a
 * pipeline the first frame draws with or a texture the scene uses, and not blocking when it
 * refines what is already shown, such as a probe bake or a sound that does not play yet.
 */

#ifndef BOX_TASK_H
#define BOX_TASK_H

#include "types.h"

#ifdef BOX_TASK_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Entries of InProgressTask, the size of InProgressTask.tasks; further tasks are dropped */
#define BOX_TASK_MAX (16)

/** Name of the task of the shader modules and pipelines whose compile has not finished */
#define BOX_TASK_SHADERS "Compiling shaders"

/** Name of the task of the irradiance probes left to bake */
#define BOX_TASK_PROBES "Baking light probes"

/** Name of the task of the reflection probes left to capture */
#define BOX_TASK_REFLECTIONS "Capturing reflections"

/** Name of the task of the font loads and glyph fields in flight */
#define BOX_TASK_FONTS "Loading fonts"

/** Name of the task of the texture files in flight */
#define BOX_TASK_TEXTURES "Loading textures"

/** Name of the task of the TextureGen generations in flight */
#define BOX_TASK_TEXTURE_GEN "Generating textures"

/** Name of the task of the EnvironmentImage loads in flight */
#define BOX_TASK_ENVIRONMENT "Loading environment"

/** Name of the task of the sound decodes in flight */
#define BOX_TASK_SOUNDS "Decoding sounds"

/** A task in progress: the work of every report of the same name */
ECS_STRUCT(InProgressTaskItem, {
    char *name;         /**< What the task is, short and stable, such as "Compiling shaders";
                         *   owned by box.task, NULL for an unused entry */
    bool blocking;      /**< The scene cannot be shown as intended until the task finishes:
                         *   any report of the name blocks */
    int32_t done;       /**< Units of the task that finished, such as pipelines or glyphs */
    int32_t total;      /**< Units of the task, 0 when unknown: when a report does not know its
                         *   total */
});

/** The tasks box is working on, written by box.task at the end of every frame, read only for
 * applications.
 *
 * The blocking tasks come first, each group in the order its tasks started. Entries from count
 * on are zero.
 */
ECS_STRUCT(InProgressTask, {
    int32_t count;      /**< Tasks in progress, at most BOX_TASK_MAX */
    int32_t blocking;   /**< Tasks among them that are blocking */
    InProgressTaskItem tasks[16]; /**< The tasks, BOX_TASK_MAX entries */
});

/** A report of a task, the handle box_task_start returns, 0 for none */
typedef uint32_t box_task_id_t;

/** State of a report that follows a count of remaining work, for box_task_remaining */
typedef struct box_task_track_t {
    box_task_id_t id;   /**< The report while work remains, 0 otherwise */
    int32_t total;      /**< Units the report counted since it started */
    int32_t remaining;  /**< Units that remained at the previous call */
} box_task_track_t;

/** Start a report of a task. Reports of the same name combine into one entry of
 * InProgressTask.
 *
 * May be called from any thread. Does nothing without box.task.
 *
 * @param world The world or a stage
 * @param name What the task is, copied
 * @param blocking The scene cannot be shown as intended until the task finishes
 * @param total Units of work, 0 when unknown
 * @return The report, 0 without box.task
 */
box_task_id_t box_task_start(
    ecs_world_t *world,
    const char *name,
    bool blocking,
    int32_t total);

/** Add finished units to a report.
 *
 * @param world The world or a stage
 * @param task The report, 0 does nothing
 * @param done Units that finished
 */
void box_task_advance(
    ecs_world_t *world,
    box_task_id_t task,
    int32_t done);

/** Set the progress of a report.
 *
 * @param world The world or a stage
 * @param task The report, 0 does nothing
 * @param done Units that finished
 * @param total Units of work, 0 when unknown
 */
void box_task_set(
    ecs_world_t *world,
    box_task_id_t task,
    int32_t done,
    int32_t total);

/** End a report: its work finished or was dropped.
 *
 * @param world The world or a stage
 * @param task The report, 0 or an ended one does nothing
 */
void box_task_end(
    ecs_world_t *world,
    box_task_id_t task);

/** Report the work a module counts as remaining, once per frame: starts a report when work
 * remains, ends it when none does.
 *
 * The total grows by every increase of remaining, and done is the total less remaining.
 *
 * @param world The world or a stage
 * @param track State of the report, zero before the first call
 * @param name What the task is
 * @param blocking The scene cannot be shown as intended until the task finishes
 * @param remaining Units of work that did not finish
 */
void box_task_remaining(
    ecs_world_t *world,
    box_task_track_t *track,
    const char *name,
    bool blocking,
    int32_t remaining);

/** Find a task of InProgressTask by name.
 *
 * @param tasks The tasks
 * @param name The name
 * @return The entry, NULL when no task of the name is in progress
 */
const InProgressTaskItem* box_task_find(
    const InProgressTask *tasks,
    const char *name);

/** Write the aggregate of the reports to InProgressTask now instead of at the end of the
 * frame. Call on the main thread, outside a frame or in an immediate system.
 *
 * @param world The world
 */
void box_task_publish(
    ecs_world_t *world);

/** Register the box.task module.
 *
 * Creates the InProgressTask singleton, empty, and the system that writes it.
 *
 * @param world The world
 */
void BoxTaskImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
