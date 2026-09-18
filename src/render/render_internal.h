/**
 * @file render/render_internal.h
 * @brief Private state of the box.render module: mesh registry, batches and dirty lists.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>
#include "../renderer/packet.h"
#include "../renderer/cluster.h"
#include "../renderer/tiling.h"
#include "../renderer/bake.h"
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
    ecs_vec_t pending_textures; /**< box_texture_cmd_t waiting for the next packet */
    ecs_vec_t dead_textures;  /**< uint32_t texture handles waiting for the next packet */
    ecs_vec_t free_layers[BOX_TEXTURE_CLASSES]; /**< uint32_t free layers per size class */
    uint32_t layer_count[BOX_TEXTURE_CLASSES];  /**< Layers handed out per size class */
    bool textured;            /**< A texture handle was allocated, slots need resolving */
    bool tiled;               /**< A texture transform, tiling or wrap exists, uvs need deriving */
    ecs_vec_t dirty;          /**< ecs_entity_t queued for static extraction */
    ecs_query_t *dynamic;     /**< Entities extracted every frame */
    ecs_query_t *cameras;     /**< Camera entities */
    ecs_query_t *captures;    /**< Camera entities that request a capture */
    ecs_query_t *suns;        /**< Directional light entities */
    ecs_query_t *lights;      /**< Point and spot light entities */
    ecs_query_t *emitters;    /**< Static entities with an Emissive material */
    ecs_query_t *surfaces;    /**< Surfaces with a renderer */
    ecs_query_t *instances;   /**< All instances, used by the debug validation */
    uint32_t mesh_count;      /**< Handles allocated so far */
    EnvironmentLight env;     /**< Environment of the previous packet */
    uint32_t env_generation;  /**< Times the environment changed since startup */
    bool bake_enabled;        /**< Baking was enabled by the previous packet */
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

/** Upload a texture with the next packet and return its handle.
 *
 * The pixel array holds the whole RGBA8 mip chain of a square texture and is
 * handed to the renderer, which frees it after the upload. Passing a handle of
 * a matching size class replaces the contents of that handle instead of
 * allocating a new one.
 *
 * @param world The world
 * @param handle Handle to replace, 0 to allocate a new one
 * @param size Edge length in pixels, the size of a texture class
 * @param pixels The mip chain, ownership moves to the renderer
 * @param srgb Pixels are display encoded
 * @return The texture handle
 */
uint32_t box_render_texture_create(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t size,
    uint8_t *pixels,
    bool srgb);

/** Release a texture handle and queue its destroy command.
 *
 * @param world The world
 * @param handle The texture handle
 */
void box_render_texture_destroy(
    ecs_world_t *world,
    uint32_t handle);

/** Register the observer that frees the handle of a removed RenderTexture.
 *
 * @param world The world
 */
void box_render_texture_init(
    ecs_world_t *world);

/** Move the pending texture commands of the registry into a packet.
 *
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_textures(
    RenderState *state,
    box_packet_t *p);

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

/** Check whether an entity owns the static slot its record points at.
 *
 * @param state The registry
 * @param e The entity
 * @param inst Its instance record
 * @return True when the record names a live slot of the entity
 */
static inline bool box_render_has_slot(
    const RenderState *state,
    ecs_entity_t e,
    const RenderInstance *inst)
{
    bool result = false;
    if (inst->dynamic || inst->mesh >= (uint32_t)ecs_vec_count(&state->batches)) {
        goto done;
    }

    const box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)inst->mesh);
    if (inst->slot >= (uint32_t)ecs_vec_count(&batch->entities)) {
        goto done;
    }

    result = ecs_vec_get_t(&batch->entities, ecs_entity_t, (int32_t)inst->slot)[0] == e;
done:
    return result;
}

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
 * @param baked The instance reads a baked probe instead of the static lights
 * @param textured The world holds textures, so the PBR slots are resolved
 * @param tiled The world holds tiling components, so the uv transform is derived
 * @param out The instance to fill
 */
void box_render_fill_instance(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    bool baked,
    bool textured,
    bool tiled,
    box_instance_t *out);

/** Fill the uv transform and mode of an instance from its material components.
 *
 * TextureTransform is the base scale and offset, TextureTiling replaces the
 * scale with the tile count its projection derives from the world size of the
 * instance and TextureWrap selects the fold the fragment stage applies.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param m Its instance matrix, the world transform times the shape scale
 * @param tiled The world holds tiling components, so the components are read
 * @param out The instance to fill
 */
void box_render_fill_uv(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const mat4 *m,
    bool tiled,
    box_instance_t *out);

/** Check whether an entity or an ancestor carries a dynamic tag.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is extracted every frame
 */
bool box_render_is_dynamic(
    ecs_world_t *world,
    ecs_entity_t e);

/** Get the camera that extract renders when no camera requests a capture.
 *
 * The active camera is the first entity with a Camera and a WorldTransform.
 *
 * @param world The world
 * @return The active camera, 0 when the world has none
 */
ecs_entity_t box_render_active_camera(
    ecs_world_t *world);

/** Fill a packet with this frame's meshes, instances, camera and light.
 *
 * @param world The world
 * @param p The packet
 */
void box_render_extract(
    ecs_world_t *world,
    box_packet_t *p);

/** Fill the packet light array with the point and spot lights inside the frustum.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_lights(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Copy the bake settings into the packet and requeue every instance when they toggled.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_bake(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Fill the packet emitter array with the emissive static instances of the bake.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_emitters(
    ecs_world_t *world,
    RenderState *state,
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
