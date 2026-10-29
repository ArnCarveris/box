/**
 * @file app/watch.h
 * @brief File stamps and the per-world registry of watched files that box.app polls.
 *
 * Modules register the files their loads read under an owner entity; about once per
 * FileWatch.interval a watch thread stats the distinct files, and the poll tells only the owners
 * of a file that changed.
 */

#ifndef BOX_APP_WATCH_H
#define BOX_APP_WATCH_H

#include <box.h>

/** Modification time and size of a file, to see whether it changed */
typedef struct box_file_stamp_t {
    int64_t sec;    /**< Seconds of the modification time, -1 when the file is missing */
    int64_t nsec;   /**< Nanoseconds of the modification time */
    int64_t size;   /**< Bytes of the file */
} box_file_stamp_t;

/** Tells an owner that a file it watches changed, on the main thread during the poll.
 *
 * The owner keeps its files watched; it registers the files of the load it requests when that
 * load completes.
 *
 * @param world The world
 * @param ctx The context the kind was registered with
 * @param owner The owner entity
 */
typedef void (*box_file_changed_t)(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner);

/** Take the stamp of a file.
 *
 * @param path The file
 * @param stamp The stamp, with sec -1 when the file is missing
 */
void box_file_stamp(
    const char *path,
    box_file_stamp_t *stamp);

/** Compare two stamps.
 *
 * @param a The first stamp
 * @param b The second stamp
 * @return Whether the stamps are equal
 */
bool box_file_stamp_equals(
    const box_file_stamp_t *a,
    const box_file_stamp_t *b);

/** Register a kind of owner, such as the textures or the shaders of a world.
 *
 * @param world The world
 * @param name Name of the kind, for traces
 * @param changed Called for an owner of the kind whose watched file changed
 * @param ctx Passed to changed, must outlive the polls of the world
 * @return Id of the kind, 0 when box.app is not imported
 */
int32_t box_file_watch_kind(
    ecs_world_t *world,
    const char *name,
    box_file_changed_t changed,
    void *ctx);

/** Watch a file for an owner, next to the files it already watches.
 *
 * @param world The world or a stage
 * @param kind Id of the kind of the owner
 * @param owner The owner entity
 * @param path The file as it was read, the path the locate found
 * @param stamp Stamp of the file taken before it was read; when it differs from the stamp of
 * the last check, the next pass starts without waiting for the interval and tells the owners
 * that saw another stamp
 */
void box_file_watch_add(
    ecs_world_t *world,
    int32_t kind,
    ecs_entity_t owner,
    const char *path,
    const box_file_stamp_t *stamp);

/** Stop watching the files of an owner.
 *
 * @param world The world or a stage
 * @param kind Id of the kind of the owner
 * @param owner The owner entity
 */
void box_file_watch_clear(
    ecs_world_t *world,
    int32_t kind,
    ecs_entity_t owner);

/** Register FileWatch, FileWatchStatus and the poll of box.app.
 *
 * @param world The world
 */
void box_file_watch_init(
    ecs_world_t *world);

#endif
