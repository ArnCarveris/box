/**
 * @file checkpoint/checkpoint_internal.h
 * @brief Private state and helpers of the box.checkpoint module.
 */

#ifndef BOX_CHECKPOINT_INTERNAL_H
#define BOX_CHECKPOINT_INTERNAL_H

#include <box.h>

/** Entities a report names before it summarizes the remaining ones */
#define BOX_CHECKPOINT_REPORT_MAX (20)

/** State of one checkpoint write or restore */
typedef struct box_checkpoint_ctx_t {
    ecs_world_t *world;  /**< The world */
    const char *folder;  /**< Folder the script files are written to */
    const char *prefix;  /**< Line prefix of the entities of the report */
    ecs_map_t ids;       /**< Component id to whether its value is written */
    ecs_vec_t skipped;   /**< Paths of the entities that were not written */
    ecs_vec_t names;     /**< File names of the folder */
    int32_t templates;   /**< Entities that a shared template body declares */
} box_checkpoint_ctx_t;

/** Prepare the state of a write or a restore.
 *
 * @param ctx The state
 * @param world The world
 * @param folder Folder of the checkpoint
 * @param prefix Line prefix of the entities of the report
 */
void box_checkpoint_ctx_init(
    box_checkpoint_ctx_t *ctx,
    ecs_world_t *world,
    const char *folder,
    const char *prefix);

/** Free the vectors and the map of a write or a restore.
 *
 * @param ctx The state
 */
void box_checkpoint_ctx_fini(
    box_checkpoint_ctx_t *ctx);

/** Turn the entities a write or a restore skipped into the lines of a report.
 *
 * @param ctx The state
 * @return The report, empty when nothing was skipped, the caller frees it
 */
char* box_checkpoint_report(
    box_checkpoint_ctx_t *ctx);

/** Test whether an entity sits under a module, which makes it framework state.
 *
 * A script the framework loads at import, like the widget library of box.ui,
 * is created under its module, and so is everything it declares; none of it
 * belongs in a checkpoint of the scene.
 *
 * @param world The world
 * @param e The entity
 * @return true when a parent of the entity carries the Module tag
 */
bool box_checkpoint_in_module(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Test whether a script is framework state.
 *
 * A script under a module is. So is a script whose file sits in the folder of
 * such a script: flecs creates the scripts a managed script includes at the
 * root, and an include cannot leave the folder of the script that includes it.
 *
 * @param world The world
 * @param e The script entity
 * @return true when the script belongs to the framework
 */
bool box_checkpoint_framework(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Collect the managed scripts of the world in the order a checkpoint uses.
 *
 * @param world The world
 * @param scripts Receives the script entities, initialized by the caller
 */
void box_checkpoint_managed(
    ecs_world_t *world,
    ecs_vec_t *scripts);

/** Build the name of the file a script is written to.
 *
 * The name is the file the script was loaded from without its folders, or the
 * name of the script entity. A name that another script of the same checkpoint
 * already took gets the position of the script appended to it.
 *
 * @param names File names the checkpoint took so far, which receives the name
 * @param world The world
 * @param script The script entity
 * @return The file name, the vector owns it
 */
const char* box_checkpoint_name(
    ecs_vec_t *names,
    ecs_world_t *world,
    ecs_entity_t script);

/** Create a folder and every folder above it.
 *
 * @param path The folder
 * @return 0 when the folder exists after the call
 */
int box_checkpoint_mkdir(
    const char *path);

/** Add the path of an entity to the report of a write.
 *
 * @param ctx The state of the write
 * @param e The entity
 */
void box_checkpoint_skip(
    box_checkpoint_ctx_t *ctx,
    ecs_entity_t e);

/** Test whether the value of a component belongs in a checkpoint.
 *
 * A component qualifies when it holds a value that reflection can serialize,
 * is not a flecs builtin and is not added to an entity by another component,
 * which is how the engine attaches its derived state.
 *
 * @param ctx The state of the write
 * @param id The component or pair id
 * @return true when the value of the component is written
 */
bool box_checkpoint_writable(
    box_checkpoint_ctx_t *ctx,
    ecs_id_t id);

/** Write every managed script of the world into the folder of the write.
 *
 * @param ctx The state of the write
 * @return Number of scripts written
 */
int32_t box_checkpoint_scripts(
    box_checkpoint_ctx_t *ctx);

/** Add the entities that no script owns to the report of the write.
 *
 * @param ctx The state of the write
 */
void box_checkpoint_unowned(
    box_checkpoint_ctx_t *ctx);

#endif
