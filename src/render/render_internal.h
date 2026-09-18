/**
 * @file render/render_internal.h
 * @brief Private state of the box.render module: mesh registry, batches and dirty lists.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>
#include "../renderer/packet.h"
#include "../geometry/mesh_gen.h"

/** Static instance slots of one mesh */
typedef struct box_batch_t {
    ecs_vec_t entities;  /**< ecs_entity_t per slot */
    bool count_dirty;    /**< Instance count changed since the last packet */
} box_batch_t;

/** Where an entity's instance lives */
typedef struct RenderInstance {
    uint32_t mesh;  /**< Mesh handle */
    uint32_t slot;  /**< Static slot, unused when dynamic */
    bool dynamic;   /**< Instance is rebuilt every frame */
    bool dirty;     /**< Queued for extraction */
} RenderInstance;

/** Module wide registry, stored as a singleton */
typedef struct RenderState {
    ecs_vec_t batches;        /**< box_batch_t indexed by mesh handle */
    ecs_vec_t pending_meshes; /**< box_mesh_cmd_t waiting for the next packet */
    ecs_vec_t dirty;          /**< ecs_entity_t queued for static extraction */
    ecs_query_t *dynamic;     /**< Entities extracted every frame */
    ecs_query_t *cameras;     /**< Camera entities */
    ecs_query_t *suns;        /**< Directional light entities */
    ecs_query_t *surfaces;    /**< Surfaces with a renderer */
    ecs_query_t *instances;   /**< All instances, used by the debug validation */
    uint32_t mesh_count;      /**< Handles allocated so far */
} RenderState;

extern ECS_COMPONENT_DECLARE(RenderInstance);
extern ECS_COMPONENT_DECLARE(RenderState);

/** Direct pointer to the registry singleton, never a deferred copy.
 *
 * @param world The world
 * @return The registry
 */
RenderState* box_render_state(
    ecs_world_t *world);

/** Upload a mesh with the next packet and return its handle.
 *
 * @param world The world
 * @param data The mesh, its arrays are copied
 * @return The mesh handle
 */
uint32_t box_render_mesh_create(
    ecs_world_t *world,
    const box_mesh_data_t *data);

/** Queue an entity for static extraction.
 *
 * @param world The world
 * @param e The entity
 */
void box_render_mark_dirty(
    ecs_world_t *world,
    ecs_entity_t e);

/** Register the observers that track instance changes.
 *
 * @param world The world
 */
void box_render_instance_init(
    ecs_world_t *world);

/** Take an entity out of its static batch.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @param inst Its instance record
 */
void box_render_release_slot(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    RenderInstance *inst);

/** Fill the instance data of an entity from its components.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param transform Its world transform
 * @param out The instance to fill
 */
void box_render_fill_instance(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    box_instance_t *out);

/** Fill a packet with this frame's meshes, instances, camera and light.
 *
 * @param world The world
 * @param p The packet
 */
void box_render_extract(
    ecs_world_t *world,
    box_packet_t *p);

/** Assert that every instance record matches the batch slot tables, debug builds only.
 *
 * @param world The world
 */
void box_render_validate(
    ecs_world_t *world);

/** Register the Extract system.
 *
 * @param world The world
 */
void box_render_extract_init(
    ecs_world_t *world);

#endif
