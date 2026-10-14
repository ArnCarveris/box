/**
 * @file render/draw.h
 * @brief Instanced batches of the forward pass, shared by the change tracking and the extract.
 *
 * Shapes live in batches keyed by scene, mesh, pipeline and whether they are dynamic; a
 * batch keeps one slot per entity, swap-removed so its instances stay contiguous.
 */

#ifndef BOX_RENDER_DRAW_H
#define BOX_RENDER_DRAW_H

#include "render.h"
#include "../geometry/mesh_cache.h"

/** Batch and slot of a shape, added with every ShapeMesh */
typedef struct DrawSlot {
    int32_t batch;  /**< Index of the batch, -1 when the entity is not drawn */
    int32_t slot;   /**< Slot of the entity in the batch */
    bool dirty;     /**< The entity is in the list of changed entities */
} DrawSlot;

/** Component id of DrawSlot */
extern ECS_COMPONENT_DECLARE(DrawSlot);

/** What a batch is keyed by */
typedef struct box_draw_key_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    uint32_t mesh;          /**< Mesh handle */
    uint32_t pipeline;      /**< Shader handle of the pipeline */
    bool dynamic;           /**< Instances are uploaded every frame */
} box_draw_key_t;

/** A batch: the entities of its slots, their instance data and what the GPU still needs */
typedef struct box_draw_batch_t {
    box_draw_key_t key;     /**< Key of the batch */
    bool alive;             /**< The batch is in use, else it is on the free list */
    int32_t next;           /**< Next batch with the same hash, -1 for none */
    uint32_t buffer;        /**< Handle of the instance buffer, 0 before it is created */
    uint32_t capacity;      /**< Instances the instance buffer holds */
    bool full;              /**< Upload every instance instead of the changed slots */
    ecs_vec_t entities;     /**< ecs_entity_t per slot */
    ecs_vec_t instances;    /**< box_render_instance_t per slot */
    ecs_vec_t changed;      /**< int32_t, slots of a static batch the GPU does not have */
} box_draw_batch_t;

/** Main thread state of the instanced batches */
typedef struct box_render_draws_t {
    ecs_query_t *dynamic_transform; /**< Shapes with DynamicTransform */
    ecs_query_t *dynamic_material;  /**< Shapes with DynamicMaterial and no DynamicTransform */
    ecs_vec_t batches;          /**< box_draw_batch_t by index */
    ecs_vec_t free_batches;     /**< int32_t, indices of batches not in use */
    ecs_map_t lookup;           /**< Key hash to the index of the first batch plus one */
    ecs_vec_t dirty;            /**< ecs_entity_t, entities whose instance may have changed */
    ecs_vec_t moved;            /**< ecs_entity_t, roots of subtrees that changed */
    ecs_vec_t walk;             /**< ecs_entity_t, stack of a subtree walk */
    ecs_map_t meshes;           /**< Mesh handles the backend received */
    ecs_vec_t released;         /**< uint32_t, meshes to release this frame */
    ecs_vec_t free_buffers;     /**< uint32_t, instance buffer handles to reuse */
    uint32_t next_buffer;       /**< Next instance buffer handle */
    uint32_t changes_size;      /**< Bytes of the change buffer, 0 before it is created */
    const void *backend;        /**< Context of the backend the GPU state was sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    box_render_draws_stats_t stats; /**< Counters of the last extract */
    int32_t refs;               /**< Observers and systems that use the state */
} box_render_draws_t;

/** Get a batch by index.
 *
 * @param d The state
 * @param index The index
 * @return The batch
 */
box_draw_batch_t* box_draws_batch(
    box_render_draws_t *d,
    int32_t index);

/** Find the batch of a key, creating it when there is none.
 *
 * @param d The state
 * @param key The key
 * @return Index of the batch
 */
int32_t box_draws_batch_ensure(
    box_render_draws_t *d,
    const box_draw_key_t *key);

/** Add a slot to a batch.
 *
 * @param d The state
 * @param index Index of the batch
 * @param e The entity of the slot
 * @return The slot
 */
int32_t box_draws_slot_add(
    box_render_draws_t *d,
    int32_t index,
    ecs_entity_t e);

/** Remove a slot from a batch, moving the last slot into it.
 *
 * @param world The world
 * @param d The state
 * @param index Index of the batch
 * @param slot The slot
 */
void box_draws_slot_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    int32_t index,
    int32_t slot);

/** Remember that a slot of a static batch changed.
 *
 * @param b The batch
 * @param slot The slot
 */
void box_draws_slot_changed(
    box_draw_batch_t *b,
    int32_t slot);

/** Write the instance data of a shape: its model matrix, normal matrix and color.
 *
 * @param out The instance
 * @param transform World transform of the entity, NULL for the identity
 * @param shape Mesh and instance scale of the entity
 * @param color Base color of the entity, NULL for the default color
 */
void box_draws_instance(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape,
    const Rgb *color);

/** Send the batches of the frame: meshes, instance buffers, uploads and scatters.
 *
 * @param world The world
 * @param d The state
 * @param p The packet
 */
void box_draws_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p);

/** Release every batch and the GPU state of the batches.
 *
 * @param d The state
 */
void box_draws_batches_fini(
    box_render_draws_t *d);

/** Forget what an earlier backend received, so meshes and buffers are sent again.
 *
 * @param d The state
 * @param cache The mesh cache
 */
void box_draws_reset_backend(
    box_render_draws_t *d,
    box_mesh_cache_t *cache);

#endif
