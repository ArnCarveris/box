/**
 * @file checkpoint/checkpoint_internal.h
 * @brief Private state and helpers of the box.checkpoint module.
 */

#ifndef BOX_CHECKPOINT_INTERNAL_H
#define BOX_CHECKPOINT_INTERNAL_H

#include <box.h>

/** Entities a report names before it summarizes the remaining ones */
#define BOX_CHECKPOINT_REPORT_MAX (20)

/** State of one checkpoint write */
typedef struct box_checkpoint_ctx_t {
    ecs_world_t *world;  /**< The world */
    const char *folder;  /**< Folder the script files are written to */
    ecs_map_t ids;       /**< Component id to whether its value is written */
    ecs_vec_t skipped;   /**< Paths of the entities that were not written */
    ecs_vec_t names;     /**< File names written to the folder */
    int32_t templates;   /**< Entities that a shared template body declares */
} box_checkpoint_ctx_t;

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
