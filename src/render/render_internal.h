/**
 * @file render/render_internal.h
 * @brief Private state of the box.render module: mesh registry, batches and dirty lists.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>
#include <box/sky.h>
#include "../renderer/packet.h"
#include "../renderer/cluster.h"
#include "../renderer/frustum.h"
#include "../renderer/tiling.h"
#include "../renderer/bake.h"
#include "../geometry/mesh_gen.h"
#include "../view/view_internal.h"
#include "../transform/transform_internal.h"
#include "../transition/transition_internal.h"

/** The LOD hierarchy the traversal owns, private to extract_lod.c */
typedef struct box_lod_state_t box_lod_state_t;

/** The generated proxies of the traversal, declared in render/lod_cache.h */
typedef struct box_lod_cache_t box_lod_cache_t;

/** Contiguous run of the static slots of a mesh that share a layer mask */
typedef struct box_slot_group_t {
    uint32_t mask;   /**< Layers the entities of the run belong to, 0 for every view */
    uint32_t first;  /**< First slot of the run */
    uint32_t count;  /**< Slots in the run */
} box_slot_group_t;

/** Static instance slots of one mesh */
typedef struct box_batch_t {
    ecs_vec_t entities;  /**< ecs_entity_t per slot */
    ecs_vec_t groups;    /**< box_slot_group_t, empty while no slot joined a layer */
    bool count_dirty;    /**< Instance count changed since the last packet */
    bool groups_dirty;   /**< Group ranges changed since the last packet */
    bool removed;
} box_batch_t;

/** Where an entity's instance lives */
typedef struct RenderInstance {
    uint32_t mesh;   /**< Mesh handle */
    float depth;     /**< Offset along the world z axis, from the UI draw order */
    uint32_t slot;   /**< Static slot, unused when dynamic or sorted */
    uint32_t sorted; /**< Index in the sorted draw list, unused when it is not in it */
    uint32_t layer_mask; /**< Layers the entity belongs to, 0 when it is in none */
    bool dynamic;    /**< Instance is rebuilt every frame */
    bool dirty;      /**< Queued for extraction */
    bool transparent; /**< Instance is drawn by the sorted pass, not by a batch */
    bool lod_hidden; /**< The LOD traversal replaced the entity by a proxy */
    bool hidden;     /**< The entity or an ancestor carries Hidden */
} RenderInstance;

/** Analytic 2D shape of an entity, written by the shape modules.
 *
 * The radii and widths are lengths in the local space of the rectangle the
 * entity draws; extract turns them into the ratios the fragment stage reads,
 * because only extract knows the world size of the instance.
 */
typedef struct RenderShape2D {
    float radii[4];  /**< Corner radii, top left, top right, bottom right, bottom left */
    float widths[4]; /**< Edge widths, top, right, bottom and left */
    rgba color;      /**< Border color, a zero alpha takes the Rgb of the entity */
} RenderShape2D;

/** World space rectangle the fragments of an entity are clipped to.
 *
 * The rectangle is axis aligned in the plane a UI tree is laid out in and
 * travels with the instance of the entity, so the fragment stage drops what
 * falls outside it on every draw the entity takes part in. An entity without
 * the component is not clipped at all.
 */
typedef struct RenderClip {
    float min[2]; /**< Left and bottom edge of the rectangle */
    float max[2]; /**< Right and top edge of the rectangle */
} RenderClip;

/** Draws the 2D shape of an entity as the soft fill its edge widths inset.
 *
 * The tag lives beside RenderShape2D instead of in it because the modules that
 * build a shape fill that struct field by field, so a flag inside it would be
 * whatever their stack held.
 */
extern ECS_TAG_DECLARE(RenderSoftShape);

/** Subtree a quad composites, which a view rendered into a render target.
 *
 * The quad is an ordinary sorted draw that shows the color of the target
 * instead of a lit material, weighted by the opacity of the instance, so a
 * subtree that was rendered once reaches the frame as one image.
 */
typedef struct RenderGroup {
    ecs_entity_t target; /**< RenderTarget entity whose color the quad shows */
} RenderGroup;

/** An entity whose own opacity a group quad applies for it.
 *
 * The entity draws into the target of its group, which a quad then composites
 * with that opacity, so the draw itself has to stay at full strength or the
 * opacity reaches the frame twice.
 */
extern ECS_TAG_DECLARE(RenderGrouped);

/** One registered material layer, kept in the order the layers are applied in */
typedef struct box_layer_t {
    ecs_entity_t entity; /**< The layer entity */
    int32_t order;       /**< Application order the list was sorted by */
} box_layer_t;

/** One registered light modulator, the index of an entry being its modulator bit */
typedef struct box_modulator_t {
    ecs_entity_t entity; /**< Entity holding the LightModulator, the light or a child */
    ecs_entity_t light;  /**< Light the modulator scales */
    ecs_entity_t shader; /**< Shader entity of the plugin */
    ecs_entity_t params; /**< Parameter component next to it, 0 when it has none */
} box_modulator_t;

/** One view of the frame, in the order the views are rendered */
typedef struct box_view_src_t {
    ecs_entity_t entity;
    ecs_entity_t camera;  /**< Camera the view renders */
    int32_t priority;     /**< Order in the frame, lower renders first */
} box_view_src_t;

/** Environment one view was extracted with, kept to number the generations */
typedef struct box_view_env_t {
    EnvironmentLight env; /**< Values of the previous packet */
    uint32_t generation;  /**< Generation those values were given, 0 when unused */
} box_view_env_t;

/** Depth and index of one entry of the sorted draw list */
typedef struct box_sort_key_t {
    uint32_t key;   /**< Depth as an unsigned value that sorts back to front */
    uint32_t index; /**< Index in the sorted draw list */
    float depth;    /**< View space depth of the instance origin */
} box_sort_key_t;

/** One page of the texture pager: the layers of one array texture of a class */
typedef struct box_texture_page_t {
    ecs_vec_t free_layers; /**< uint32_t layers of the page waiting for reuse */
    uint32_t cls;          /**< Size class the page holds */
    uint32_t count;        /**< Layers of the page handed out so far */
    bool used;             /**< The page was opened for a size class */
} box_texture_page_t;

/** Module wide registry, stored as a singleton */
typedef struct RenderState {
    ecs_world_t *world;       /**< World the registry and its cached queries belong to */
    ecs_vec_t batches;        /**< box_batch_t indexed by mesh handle */
    ecs_vec_t pending_meshes; /**< box_mesh_cmd_t waiting for the next packet */
    ecs_vec_t dead_meshes;    /**< uint32_t mesh handles waiting for the next packet */
    ecs_vec_t pending_textures; /**< box_texture_cmd_t waiting for the next packet */
    ecs_vec_t dead_textures;  /**< uint32_t texture handles waiting for the next packet */
    box_texture_page_t pages[BOX_TEXTURE_PAGES]; /**< Pages of the texture pager */
    bool pages_full;          /**< The pager ran out of pages and logged it */
    bool textured;            /**< A texture handle was allocated, slots need resolving */
    bool tiled;               /**< A texture transform, tiling or wrap exists, uvs need deriving */
    bool blended;             /**< A blend or back face mode exists, so instances read them */
    bool translucent;         /**< An opacity exists, so instances resolve their alpha */
    bool layered;             /**< A material enables a layer, so instances resolve their mask */
    bool shadowless;          /**< A NoShadow tag exists, so instances resolve their caster flag */
    bool hide_used;           /**< A Hidden tag exists, so instances resolve their visibility */
    bool layer_data;
    bool shaped;              /**< A 2D shape exists, so instances resolve their shape words */
    bool clipped;             /**< A clip rectangle exists, so instances resolve their clip */
    bool transitioned;        /**< A transition exists, so instances resolve displayed values */
    bool layers_dirty;        /**< The registration list changed and must be sent again */
    bool layers_reported;     /**< The layer limit was already logged */
    bool modulators_dirty;    /**< A light modulator changed, so the mesh shader recomposes */
    bool modulators_reported; /**< The light modulator limit was already logged */
    bool modulated;           /**< A light modulator exists, so lights resolve their mask */
    ecs_vec_t modulators;     /**< box_modulator_t, the index of an entry being its bit */
    ecs_query_t *modulator_query; /**< Entities holding a LightModulator */
    ecs_entity_t modulator_query_entity; /**< Entity flecs bound to modulator_query's cache */
    ecs_vec_t layers;         /**< box_layer_t in application order, index is the layer bit */
    ecs_vec_t view_layers;    /**< ecs_entity_t per render layer, the index being its bit */
    uint32_t ui_mask;         /**< Layer bits of the UI trees, hidden by every other view */
    bool depth_used;          /**< An instance carries a depth offset, so extract adds it */
    bool layer_used;          /**< An entity or a light joined a render layer */
    bool layer_full;          /**< The render layer limit was already logged */
    ecs_vec_t dirty;          /**< ecs_entity_t queued for static extraction */
    ecs_vec_t transition_slow; /**< ecs_entity_t whose transitions the game side must resolve */
    ecs_vec_t transparent;    /**< ecs_entity_t drawn by the sorted pass, extracted per frame */
    ecs_vec_t sort;           /**< box_sort_key_t of the sorted pass, reused every frame */
    ecs_vec_t sort_tmp;       /**< Scratch of the radix sort, reused every frame */
    ecs_vec_t view_list;      /**< box_view_src_t of the views of this frame */
    ecs_vec_t view_env;       /**< box_view_env_t of the previous packet, per view index */
    ecs_vec_t view_clip;      /**< mat4 world to clip of the previous packet, per view index */
    ecs_vec_t dead_targets;   /**< uint32_t render target handles waiting for the next packet */
    uint32_t target_count;    /**< Render target handles handed out so far */
    int32_t view_count;       /**< View entities the world holds */
    int32_t target_live;      /**< RenderTarget entities the world holds */
    ecs_query_t *views;       /**< View entities */
    ecs_entity_t views_entity; /**< Entity flecs bound to views' cache */
    ecs_query_t *targets;     /**< Render target entities */
    ecs_entity_t targets_entity; /**< Entity flecs bound to targets' cache */
    ecs_query_t *dynamic;     /**< Entities extracted every frame */
    ecs_entity_t dynamic_entity; /**< Entity flecs bound to dynamic's cache */
    ecs_query_t *cameras;     /**< Camera entities */
    ecs_query_t *captures;    /**< Camera entities that request a capture */
    ecs_query_t *suns;        /**< Directional light entities */
    ecs_query_t *lights;      /**< Point and spot light entities */
    ecs_entity_t lights_entity; /**< Entity flecs bound to lights' cache */
    ecs_query_t *emitters;    /**< Static entities with an Emissive material */
    ecs_entity_t emitters_entity; /**< Entity flecs bound to emitters' cache */
    ecs_query_t *layer_query; /**< Entities holding a MaterialLayer registration */
    ecs_entity_t layer_query_entity; /**< Entity flecs bound to layer_query's cache */
    ecs_query_t *surfaces;    /**< Surfaces with a renderer */
    ecs_query_t *instances;   /**< All instances, used by the debug validation */
    ecs_query_t *lod_roots;   /**< Collision roots the LOD hierarchy is built from */
    ecs_entity_t lod_roots_entity; /**< Entity flecs bound to lod_roots' cache */
    box_lod_state_t *lod;     /**< The LOD hierarchy and what the last walk decided */
    uint32_t mesh_count;      /**< Handles allocated so far */
    EnvironmentLight env;     /**< Environment of the previous packet */
    uint32_t env_generation;  /**< Times the environment changed since startup */
    bool bake_enabled;        /**< Baking was enabled by the previous packet */
} RenderState;

extern ECS_COMPONENT_DECLARE(RenderInstance);
extern ECS_COMPONENT_DECLARE(RenderState);
extern ECS_COMPONENT_DECLARE(RenderShape2D);
extern ECS_COMPONENT_DECLARE(RenderClip);
extern ECS_COMPONENT_DECLARE(RenderGroup);

/** Direct pointer to the registry singleton, never a deferred copy.
 *
 * @param world The world
 * @return The registry
 */
RenderState* box_render_state(
    ecs_world_t *world);

/** Create the state of the LOD traversal.
 *
 * @return The state
 */
box_lod_state_t* box_render_lod_new(void);

/** Release the state of the LOD traversal.
 *
 * @param lod The state, NULL for none
 */
void box_render_lod_free(
    box_lod_state_t *lod);

/** Give the traversal the query it finds the roots of the hierarchy with.
 *
 * The component belongs to box.collision, which box.render does not import, so
 * whichever of the two modules is registered last hands the query over.
 *
 * @param world The world
 * @param root_bounds The RootBounds component, 0 when it is not registered
 */
void box_render_lod_roots(
    ecs_world_t *world,
    ecs_entity_t root_bounds);

/** Walk the LOD hierarchy once per view and apply what the walks decided.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet of the frame
 */
void box_render_lod_update(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Append the proxies of the collapsed nodes as dynamic batches.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet of the frame
 */
void box_render_lod_proxies(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** The proxy cache of the traversal, which the statistics and the tests read.
 *
 * @param state The registry
 * @return The cache, NULL when the traversal has no state
 */
box_lod_cache_t* box_render_lod_cache(
    RenderState *state);

/** Render the views of the impostors the frame asked for into their atlases.
 *
 * The capture views are added at the end of the frame, so the billboard of an
 * impostor shows what the previous frame captured and the node keeps its box
 * until then; nothing of the capture is on the path of the frame it belongs to.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet of the frame
 */
void box_render_lod_capture(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Append the billboards of the impostors to the sorted list of a view.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet of the frame
 * @param v The view the billboards are drawn in
 */
void box_render_lod_billboards(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    const box_view_t *v);

/** Tell the traversal that the look or the place of an entity changed.
 *
 * Every generated proxy that holds the entity is thrown away, which is the
 * proxy of the entity itself and of every node above it; nothing else in the
 * hierarchy is touched.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 */
void box_render_lod_touch(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e);

/** Fill the sun of a view from the first directional light, or a default one.
 *
 * @param world The world
 * @param state The registry
 * @param v The view to fill
 * @return True when the world holds a light the view is lit by
 */
bool box_render_view_sun(
    ecs_world_t *world,
    RenderState *state,
    box_view_t *v);

/** Fill a scratch view with the target, the camera and the LOD settings it has.
 *
 * The extract builds the views of the packet after the LOD traversal has run,
 * so the traversal asks for the same three parts of a view on its own.
 *
 * @param world The world
 * @param src The view of the frame
 * @param p The packet, for the size a view without a target draws at
 * @param out Receives the view
 */
void box_render_view_probe(
    ecs_world_t *world,
    const box_view_src_t *src,
    const box_packet_t *p,
    box_view_t *out);

/** Upload a mesh with the next packet and return its handle.
 *
 * @param world The world
 * @param data The mesh, its arrays are copied
 * @return The mesh handle
 */
uint32_t box_render_mesh_create(
    ecs_world_t *world,
    const box_mesh_data_t *data);

/** Release the renderer resources of a mesh handle with the next packet.
 *
 * The handle itself is retired: it is never handed out again, so an instance
 * that still names it draws nothing instead of another mesh.
 *
 * @param world The world
 * @param handle The mesh handle
 */
void box_render_mesh_destroy(
    ecs_world_t *world,
    uint32_t handle);

/** Initialize the mesh command lists of the registry.
 *
 * @param state The registry
 */
void box_render_mesh_init(
    RenderState *state);

/** Release the mesh command lists of the registry and the arrays they own.
 *
 * @param state The registry
 */
void box_render_mesh_fini(
    RenderState *state);

/** Move the pending mesh commands of the registry into a packet.
 *
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_meshes(
    RenderState *state,
    box_packet_t *p);

/** Upload a texture with the next packet and return its handle.
 *
 * The pixel array holds the whole RGBA8 mip chain of a square texture and is
 * handed to the renderer, which frees it after the upload. Passing a handle of
 * a matching size class replaces the contents of that handle instead of
 * allocating a new one. A world that ran out of pages logs one error, frees the
 * pixels and hands back handle 0, which renders the material without a texture.
 *
 * @param world The world
 * @param handle Handle to replace, 0 to allocate a new one
 * @param size Edge length in pixels, the size of a texture class
 * @param pixels The mip chain, ownership moves to the renderer
 * @param srgb Pixels are display encoded
 * @return The texture handle, 0 when the pager is out of pages
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

/** Material layer registrations in application order, rebuilt when they changed.
 *
 * @param world The world
 * @param count Receives the number of registrations
 * @return The registrations, NULL when there are none
 */
const box_layer_t* box_render_layers(
    ecs_world_t *world,
    int32_t *count);

/** Light modulator registrations, rebuilt when one of them changed.
 *
 * @param world The world
 * @param count Receives the number of registrations
 * @return The registrations, NULL when there are none
 */
const box_modulator_t* box_render_modulators(
    ecs_world_t *world,
    int32_t *count);

/** Queue an entity for static extraction.
 *
 * @param world The world
 * @param e The entity
 */
void box_render_mark_dirty(
    ecs_world_t *world,
    ecs_entity_t e);

/** Queue an entity and every descendant with a mesh for extraction.
 *
 * @param world The world
 * @param e The entity
 */
void box_render_mark_subtree(
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

/** Give an entity the static slot of its layer mask, appending one when it has none.
 *
 * While no entity has joined a layer the batch has no groups and a new slot is
 * appended at the end, which is what a scene without layers has always done.
 * Once groups exist the slot lands in the run of its mask, which costs one slot
 * move per run between that run and the end of the batch; every moved entity is
 * requeued so the packet carries its instance data at its new slot.
 *
 * @param world The world
 * @param batch The batch of its mesh
 * @param e The entity
 * @param inst Its instance record
 */
void box_render_take_slot(
    ecs_world_t *world,
    box_batch_t *batch,
    ecs_entity_t e,
    RenderInstance *inst);

/** Move an entity that already owns a slot into the run of its current mask.
 *
 * @param world The world
 * @param state The registry
 * @param batch The batch of its mesh
 * @param e The entity
 * @param inst Its instance record
 */
void box_render_regroup_slot(
    ecs_world_t *world,
    RenderState *state,
    box_batch_t *batch,
    ecs_entity_t e,
    RenderInstance *inst);

/** Send the slot runs of every batch that regrouped since the last packet.
 *
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_groups(
    RenderState *state,
    box_packet_t *p);

/** Bit of a layer entity in a membership mask, assigning one on first use.
 *
 * @param state The registry
 * @param layer The layer entity
 * @return The bit of the layer, 0 when the layer limit is reached
 */
uint32_t box_render_layer_bit(
    RenderState *state,
    ecs_entity_t layer);

/** Layers an entity or a light belongs to, read through its prefabs.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @return One bit per layer the entity joined, 0 when it joined none
 */
uint32_t box_render_entity_layers(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e);

/** Layer mask of one of the layer lists of a view.
 *
 * @param state The registry
 * @param list The list, which ends at its first zero
 * @return The bits of the listed layers, BOX_VIEW_LAYER_ALL when the list is empty
 */
uint32_t box_render_view_layers(
    const RenderState *state,
    const ecs_entity_t *list);

/** Check whether a view that lists layers shows the entities of a membership mask.
 *
 * @param mask The membership mask, 0 for an entity that joined no layer
 * @param view_mask The layer mask of the view
 * @return True when the view draws it
 */
static inline bool box_render_layer_visible(
    uint32_t mask,
    uint32_t view_mask)
{
    return box_layer_shown(mask, view_mask, false);
}

/** Register the observer that tracks layer membership.
 *
 * @param world The world
 */
void box_render_view_layers_init(
    ecs_world_t *world);

/** Queue every instance of the world for static extraction.
 *
 * @param world The world
 * @param state The registry
 */
void box_render_mark_all(
    ecs_world_t *world,
    RenderState *state);

/** World space bounding sphere of a mesh under an instance matrix.
 *
 * The local bounds of the mesh are transformed into a center, and the radius is
 * the circumradius of the transformed box, which never underestimates it.
 *
 * @param mesh The render mesh, its bounds are the unscaled ones
 * @param m The instance matrix, the world transform times the shape scale
 * @param out Center xyz and radius
 */
void box_render_instance_bounds(
    const RenderMesh *mesh,
    const mat4 *m,
    float *out);

/** Fill the instance data of an entity from its components.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param transform Its world transform
 * The instance comes out as a shadow caster; a world that holds a NoShadow tag
 * or a blend mode clears the flag again through box_instance_set_caster.
 *
 * @param baked The instance reads a baked probe instead of the static lights
 * @param textured The world holds textures, so the PBR slots are resolved
 * @param tiled The world holds tiling components, so the uv transform is derived
 * @param shaped The world holds 2D shapes, so the shape words are derived
 * @param clipped The world holds clip rectangles, so the clip words are derived
 * @param layers The registry when a material enables a layer, NULL when none does
 * @param transition The transitions of the entity, NULL when it has none
 * @param time The time the transitions are displayed at
 * @param depth Offset along the world z axis, zero outside a UI tree
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
    bool shaped,
    bool layer_data,
    bool clipped,
    const RenderState *layers,
    const TransitionState *transition,
    double time,
    float depth,
    box_instance_t *out);

/** Give an entity the world space rectangle its fragments are clipped to.
 *
 * @param world The world
 * @param e The entity
 * @param min Left and bottom edge of the rectangle
 * @param max Right and top edge of the rectangle
 */
void box_render_set_clip(
    ecs_world_t *world,
    ecs_entity_t e,
    const float *min,
    const float *max);

/** Drop the clip rectangle of an entity, which is drawn whole again.
 *
 * @param world The world
 * @param e The entity
 */
void box_render_clear_clip(
    ecs_world_t *world,
    ecs_entity_t e);

/** Get the transitions of an entity when the world has any.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @return The transitions of the entity, NULL when it has none
 */
const TransitionState* box_render_transition(
    const ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e);

/** Write the transitions of a static instance onto the packet.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param slot Its static instance slot
 * @param transition Its transitions
 * @param time The time of the frame
 * @param depth Offset along the world z axis of the instance
 * @param p The packet
 * @return The command, BOX_UPDATE_STILL when none is needed and BOX_UPDATE_SLOW
 *   when the renderer cannot evaluate the transitions of the instance
 */
uint32_t box_render_extract_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    uint32_t slot,
    const TransitionState *transition,
    double time,
    float depth,
    box_packet_t *p);

/** Clear the bookkeeping of the transitions the renderer reported as finished.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet the renderer wrote the report into
 */
void box_render_extract_finished(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Give the instance of an entity an offset along the world z axis.
 *
 * The offset is what draws a UI node in front of its parent: it moves the
 * instance toward the camera of its view without changing the transform the
 * rest of the engine reads, and it re-extracts the entity when it changes.
 *
 * @param world The world
 * @param e The entity, an entity without an instance is ignored
 * @param depth The offset, negative values move toward a UI camera
 */
void box_render_set_depth(
    ecs_world_t *world,
    ecs_entity_t e,
    float depth);

/** Register a layer as the layer of a UI tree.
 *
 * Every view that does not list the layer hides its members, which is what
 * keeps a UI tree out of the views of the 3D scene.
 *
 * @param world The world
 * @param layer The layer entity
 */
void box_render_add_ui_layer(
    ecs_world_t *world,
    ecs_entity_t layer);

/** Check whether a layer is the layer of a UI tree.
 *
 * @param world The world
 * @param layer The layer entity
 * @return True when the layer was registered as a UI layer
 */
bool box_render_is_ui_layer(
    ecs_world_t *world,
    ecs_entity_t layer);

/** Check whether an entity and its ancestors let it cast a shadow.
 *
 * @param world The world
 * @param e The entity
 * @return True when neither the entity nor an ancestor carries NoShadow
 */
bool box_render_casts_shadow(
    ecs_world_t *world,
    ecs_entity_t e);

/** Check whether an entity is hidden by a Hidden tag on itself or an ancestor.
 *
 * A world that never added the tag answers false without walking anything,
 * which is what keeps the switch free for a scene that does not use it.
 *
 * @param world The world
 * @param e The entity
 * @return True when the entity or an ancestor carries Hidden
 */
bool box_render_is_hidden(
    const ecs_world_t *world,
    ecs_entity_t e);

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

/** Check whether an entity belongs to the sorted draw list.
 *
 * @param state The registry
 * @param e The entity
 * @param inst Its instance record
 * @return True when the record names the entry of the entity
 */
static inline bool box_render_in_list(
    const RenderState *state,
    ecs_entity_t e,
    const RenderInstance *inst)
{
    bool result = false;
    if (!inst->transparent || inst->sorted >= (uint32_t)ecs_vec_count(&state->transparent)) {
        goto done;
    }

    result = ecs_vec_get_t(&state->transparent, ecs_entity_t, (int32_t)inst->sorted)[0] == e;
done:
    return result;
}

/** Blend mode of an entity, read through its prefab when it inherits one.
 *
 * @param world The world
 * @param e The entity
 * @return One of the BOX_BLEND constants
 */
uint32_t box_render_blend_mode(
    ecs_world_t *world,
    ecs_entity_t e);

/** Back face mode of an entity, read through its prefab when it inherits one.
 *
 * @param world The world
 * @param e The entity
 * @return One of the BOX_BACK constants
 */
uint32_t box_render_back_mode(
    ecs_world_t *world,
    ecs_entity_t e);

/** Check whether an entity leaves the opaque batches for the sorted pass.
 *
 * An instance is sorted when its blend mode is not opaque, when its opacity is
 * below one, or when it shows its back faces, which the batched draws cannot
 * do. The registry flags say which of
 * those components the world holds at all, so a scene without them pays
 * nothing per changed entity.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @return True when the sorted pass draws the entity
 */
bool box_render_is_transparent(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e);

/** Add an entity to the sorted draw list or take it out.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @param inst Its instance record
 * @param transparent True to put the entity in the list
 */
void box_render_set_transparent(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    RenderInstance *inst,
    bool transparent);

/** Fill the sorted draws of one view and order them back to front.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @param v The view the draws are sorted for
 */
void box_render_extract_transparent(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    box_view_t *v);

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

/** Render target handle the composite quad of an entity shows.
 *
 * @param world The world
 * @param e The entity
 * @return The handle, 0 when the entity composites no subtree
 */
uint32_t box_render_group_handle(
    ecs_world_t *world,
    ecs_entity_t e);

/** Append the dynamic batches a view draws, grouped by mesh.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @param view_mask Layers the view shows, BOX_VIEW_LAYER_ALL for every layer
 * @param caster_mask Layers that cast its shadows, BOX_VIEW_LAYER_ALL for all
 * @param strict The view draws the members of its layers only
 */
void box_render_extract_dynamic(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    uint32_t view_mask,
    uint32_t caster_mask,
    bool strict);

/** Fill the light array of one view with the lights inside its frustum.
 *
 * The lights of every view live in the same packet array; the view names the
 * range that is its own. Only the first view collects the unculled static
 * lights of the bake, which is shared by every view.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @param v The view to fill
 * @param bake The view also collects the unculled static lights of the bake
 */
void box_render_extract_lights(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    ecs_entity_t e,
    box_view_t *v,
    bool bake);

/** Component of a view entity, or its singleton when the view does not carry it.
 *
 * @param world The world
 * @param id The component
 * @return The component, NULL when neither the view nor the world has it
 */
const void* box_render_view_setting(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id);

/** Collect the views of the frame and the render targets they draw into.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @return The camera that requested a capture, 0 when no camera did
 */
ecs_entity_t box_render_collect_views(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** The view entity the settings shared by every view are read from.
 *
 * @param state The registry
 * @return The first view of the frame, 0 when the world has no views
 */
ecs_entity_t box_render_primary_view(
    RenderState *state);

/** Fill the view array of the packet, one entry per view that renders.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @param capture The camera that requested a capture, 0 when no camera did
 */
void box_render_extract_views(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    ecs_entity_t capture);

/** Shadow settings of a view, the view first, the singleton after, defaults last.
 *
 * Every field is clamped to the range it is valid in, so the caller can use the
 * result without testing it again.
 *
 * @param world The world
 * @param out The settings to fill
 * @return The filled settings
 */
const ShadowSettings* box_render_shadow_settings(
    ecs_world_t *world,
    ecs_entity_t e,
    ShadowSettings *out);

/** Split a depth range into the far distances of every cascade.
 *
 * A lambda of zero splits the range uniformly, a lambda of one logarithmically.
 *
 * @param near_ View depth the first cascade starts at
 * @param distance View depth the last cascade ends at
 * @param lambda Blend of the logarithmic and the uniform split
 * @param cascades Cascades to split into
 * @param out The far distance of every cascade
 */
void box_render_shadow_splits(
    float near_,
    float distance,
    float lambda,
    int32_t cascades,
    float *out);

/** World space corners of the camera frustum slice between two view depths.
 *
 * @param v The view
 * @param near_ View depth the slice starts at
 * @param far_ View depth the slice ends at
 * @param out The eight corners, the near four first
 */
void box_render_shadow_corners(
    const box_view_t *v,
    float near_,
    float far_,
    vec3 *out);

/** Fill the cascades, splits and biases of the shadow map of one view.
 *
 * A view without shadows or a world without a directional light leaves the
 * cascade count at zero, which is what a frame without a shadow pass reads.
 *
 * @param world The world
 * @param sun The world holds a directional light
 * @param v The view to fill
 */
void box_render_extract_shadows(
    ecs_world_t *world,
    ecs_entity_t e,
    bool sun,
    box_view_t *v);

/** Register the observers that count the views and release removed targets.
 *
 * @param world The world
 */
void box_render_view_init(
    ecs_world_t *world);

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

/** Register the observers that track material layer registrations and use.
 *
 * @param world The world
 */
void box_render_layers_init(
    ecs_world_t *world);

/** Resolve the material layers an entity enables into a mask of layer bits.
 *
 * The pair is inherited, so the prefabs the entity instantiates are walked too.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @return One bit per enabled layer, the bit being its index in the registry
 */
uint32_t box_render_layer_mask(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e);

/** Send the layer registrations and their parameters with the packet.
 *
 * The ordered registration list only travels when it changed; the parameters
 * are scene global and travel every frame while a material enables a layer.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_layers(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Register the observers that track light modulator registrations.
 *
 * @param world The world
 */
void box_render_modulators_init(
    ecs_world_t *world);

/** Resolve the modulators of a light into a mask of modulator bits.
 *
 * @param world The world
 * @param state The registry
 * @param e The light entity
 * @return One bit per modulator, the bit being its index in the registry
 */
uint32_t box_render_modulator_mask(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e);

/** Send the parameter block of every light modulator with the packet.
 *
 * The registration list itself never travels: it composes the mesh shader,
 * which reaches the renderer as a shader source like any other reload.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
void box_render_extract_modulators(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p);

/** Register the Extract system.
 *
 * @param world The world
 */
void box_render_extract_init(
    ecs_world_t *world);

#endif
