/**
 * @file box/checkpoint.h
 * @brief Script based checkpoints of a running instance.
 *
 * A checkpoint walks the managed scripts of the world, writes the current
 * values of the entities every script owns back into a copy of its source and
 * stores those copies in a numbered folder. The folder lives under the process
 * folder of the instance, which the engine removes when the instance stops.
 * Entities that no script statement declares are reported instead of dropped.
 */

#ifndef BOX_CHECKPOINT_H
#define BOX_CHECKPOINT_H

#include "types.h"

#ifdef BOX_CHECKPOINT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Line the report starts with for an entity that a checkpoint cannot write */
#define BOX_CHECKPOINT_SKIPPED "not checkpointed: "

/** Line the report of a restore starts with for an entity it left alone */
#define BOX_CHECKPOINT_KEPT "not restored: "

/** Line a failed restore starts with, followed by the file that failed */
#define BOX_CHECKPOINT_FAILED "restore failed: "

/** Marks a component whose value the engine derives, which a checkpoint skips.
 *
 * The tag is added to the component, not to the entities that carry it. The
 * module marks the derived components of the box modules when they are
 * registered; a module of an application marks its own.
 */
extern ECS_TAG_DECLARE(NoCheckpoint);

/** Checkpoint settings and counters of an instance, a singleton */
ECS_STRUCT(Checkpoint, {
    char *dir;      /**< Folder the checkpoints go to, empty for the process folder */
    char *last;     /**< Folder of the last checkpoint, empty when none was written */
    int32_t count;  /**< Checkpoints written by the instance so far */
});

/** Register the box.checkpoint module.
 *
 * @param world The world
 */
void BoxCheckpointImport(
    ecs_world_t *world);

/** Folder that holds the checkpoints of the instance.
 *
 * The folder is `Checkpoint.dir` when it is set and `checkpoints` in the
 * process folder when it is not. The folder is not created.
 *
 * @param world The world
 * @return The folder, the caller frees it
 */
char* box_checkpoint_dir(
    ecs_world_t *world);

/** Write a checkpoint of every managed script of the world.
 *
 * Every script is written to `<name>.flecs` in a folder of the checkpoint
 * folder, with the current values of the components of the entities it owns
 * substituted into its source. Comments, layout and expressions of statements
 * that did not change are preserved.
 *
 * @param world The world
 * @param name Name of the checkpoint folder, NULL for the next number
 * @param report Receives the written files and the entities that were skipped,
 *   one per line, NULL to ignore
 * @return The checkpoint folder, the caller frees it, NULL when it failed
 */
char* box_checkpoint_write(
    ecs_world_t *world,
    const char *name,
    char **report);

/** Reload every script of a checkpoint folder.
 *
 * Every managed script whose file the folder holds is reloaded from that file,
 * which deletes the entities the script owns and creates them again from the
 * text of the checkpoint, so entity handles do not survive a restore. Entities
 * that no script owns are left alone and reported.
 *
 * The files are all read and parsed before the first script is reloaded, so a
 * checkpoint that does not parse leaves the world untouched; a file that fails
 * to evaluate leaves the scripts before it reloaded and names the file that
 * failed in the report.
 *
 * @param world The world
 * @param name Name of the checkpoint folder, NULL for the last checkpoint
 * @param keep Entity whose script is left loaded, 0 for none. A restore from a
 *   terminal session passes the session, which keeps the script that declares
 *   it from deleting it while it runs the command
 * @param report Receives the entities that were left alone, one per line, or
 *   the reason the restore failed, NULL to ignore
 * @return The checkpoint folder, the caller frees it, NULL when it failed
 */
char* box_checkpoint_restore(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t keep,
    char **report);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
