/**
 * @file box/script.h
 * @brief Incremental instantiation of script templates.
 *
 * Script templates are marked manual so new instances stay pending. A PreUpdate
 * system expands the pending instances nearest to the active camera within a
 * count and time budget; without a camera it keeps the script order.
 */

#ifndef BOX_SCRIPT_H
#define BOX_SCRIPT_H

#include "types.h"

#ifdef BOX_SCRIPT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Budget of the incremental instantiation, read at the start of every frame */
ECS_STRUCT(ScriptSettings, {
    int32_t templates_per_frame; /**< Instances expanded per frame, 0 for no limit */
    double budget_ms;            /**< Milliseconds per frame, 0 for no limit */
    double reorder_distance;     /**< Camera movement that reorders the queue */
    int32_t reorder_interval;    /**< Frames after which the queue is reordered anyway */
    bool nearest_first;          /**< Expand the instances nearest to the camera first */
    bool enabled;                /**< Load templates over frames, off by default */
});

/** Instantiation counters, written by the engine after every frame */
ECS_STRUCT(ScriptStats, {
    int32_t pending;        /**< Instances waiting for their template body */
    int32_t instantiated;   /**< Instances expanded by the last frame */
    double worst_frame_ms;  /**< Longest a frame spent expanding instances */
});

/** Register the box.script module.
 *
 * @param world The world
 */
void BoxScriptImport(
    ecs_world_t *world);

/** Load a flecs script file, located the way box locates its other files.
 *
 * @param world The world
 * @param path Path of the script
 * @return The script entity, 0 when the file is not found or fails to run
 */
ecs_entity_t box_script_load(
    ecs_world_t *world,
    const char *path);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
