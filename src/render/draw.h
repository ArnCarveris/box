/**
 * @file render/draw.h
 * @brief Instanced batches of the forward pass, shared by the change tracking and the extract.
 *
 * Shapes live in batches keyed by scene, mesh, pipeline, whether they are dynamic and whether
 * they cast shadows; a
 * batch keeps one slot per entity, swap-removed so its instances stay contiguous.
 */

#ifndef BOX_RENDER_DRAW_H
#define BOX_RENDER_DRAW_H

#include "render.h"
#include "../geometry/shape2d.h"
#include "../texture/texture.h"
#include "../text/text.h"

/** Tints composed into one mix: a display encoded color becomes color * keep + add */
typedef struct box_draw_tint_t {
    float add[3];   /**< Sum of the tint colors weighted by what the later tints keep, 0..1 */
    float keep;     /**< Product of one minus the strength of every tint */
} box_draw_tint_t;

/** Batch and slot of a shape, added with every ShapeMesh */
typedef struct DrawSlot {
    int32_t batch;  /**< Index of the batch, -1 when the entity is not drawn */
    int32_t slot;   /**< Slot of the entity in the batch */
    float fade;     /**< Product of the Opacity of the ancestors, 1 without any */
    float layer;    /**< Layer of a 2D shape, its depth in the hierarchy */
    bool dirty;     /**< The entity is in the list of changed entities */
    bool tinted;    /**< An ancestor has a Tint of a strength above 0 */
    box_draw_tint_t tint; /**< Tints of the ancestors, composed from the root down */
} DrawSlot;

/** Component id of DrawSlot */
extern ECS_COMPONENT_DECLARE(DrawSlot);

/** What a batch is keyed by */
typedef struct box_draw_key_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    uint32_t mesh;          /**< Mesh handle */
    uint32_t pipeline;      /**< Shader handle of the pipeline */
    uint8_t blend;          /**< Resolved BlendMode, Opaque unless the shape blends */
    uint8_t back_face;      /**< BackFaceMode */
    uint8_t wrap;           /**< TextureWrap of the texture samplers */
    bool dynamic;           /**< Instances are uploaded every frame */
    bool shadow;            /**< Instances cast shadows, false under NoShadow */
    bool bake;              /**< Static 3D instances without NoBake, drawn by probe captures */
    ecs_entity_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture entities of PbrTextures */
    uint32_t atlas;         /**< Texture handle of the glyph atlas page of a glyph batch, or 0 */
} box_draw_key_t;

/** Batch and slot of one glyph of a Text */
typedef struct box_draw_glyph_slot_t {
    int32_t batch;  /**< Index of the batch */
    int32_t slot;   /**< Slot of the glyph in the batch */
} box_draw_glyph_slot_t;

/** Slots of the glyphs of a Text, one per glyph of its TextGlyphs, added with every Text */
typedef struct TextSlots {
    ecs_vec_t slots; /**< box_draw_glyph_slot_t */
} TextSlots;

/** Component id of TextSlots */
extern ECS_COMPONENT_DECLARE(TextSlots);

/** The material components of a shape, NULL for the ones it does not have */
typedef struct box_draw_material_t {
    const Rgb *color;           /**< Base color, display encoded */
    const Opacity *opacity;     /**< Opacity of the shape itself */
    const PbrMaterial *pbr;     /**< Metallic and roughness */
    const Emissive *emissive;   /**< Emitted light */
    const TextureTransform *uv; /**< Scale and offset of the texture coordinates */
    const Tint *tint;           /**< Tint of the shape itself, applied after the inherited */
    const box_draw_tint_t *inherited; /**< Composed tints of the ancestors, NULL for none */
    const Shape2D *shape2d;     /**< Resolved 2D shape, NULL for a 3D shape */
    float fade;                 /**< Product of the Opacity of the ancestors */
} box_draw_material_t;

/** A blended instance of a view, ordered back to front by its view depth */
typedef struct box_draw_sorted_t {
    float depth;            /**< View space z of the instance origin, smaller is farther */
    int32_t batch;          /**< Index of the batch */
    int32_t slot;           /**< Slot of the instance in the batch */
} box_draw_sorted_t;

/** An entity with 2D slots in the draw order */
typedef struct box_draw_order_t {
    ecs_entity_t entity;    /**< The entity, its shape slot first and then its glyph slots */
    const uint64_t *path;   /**< Sibling keys of the entity and its ancestors, root first */
    int32_t depth;          /**< Keys in path */
} box_draw_order_t;

/** Consecutive slots of one batch that draw one after another in the draw order of a scene */
typedef struct box_draw_run_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    int32_t batch;          /**< Index of the batch */
    uint32_t first;         /**< First slot */
    uint32_t count;         /**< Slots of the run */
} box_draw_run_t;

/** A parent with OrderedChildren whose child order the draw order was computed from */
typedef struct box_draw_parent_t {
    ecs_entity_t parent;    /**< The parent */
    int32_t first;          /**< First of its children in the copied children */
    int32_t count;          /**< Children it had */
} box_draw_parent_t;

/** A batch: the entities of its slots, their instance data and what the GPU still needs */
typedef struct box_draw_batch_t {
    box_draw_key_t key;     /**< Key of the batch */
    bool alive;             /**< The batch is in use, else it is on the free list */
    int32_t next;           /**< Next batch with the same hash, -1 for none */
    uint32_t buffer;        /**< Handle of the instance buffer, 0 before it is created */
    uint32_t capacity;      /**< Instances the instance buffer holds */
    bool full;              /**< Upload every instance instead of the changed slots */
    bool ready;             /**< The backend has the mesh and the instances this frame */
    ecs_vec_t entities;     /**< ecs_entity_t per slot */
    ecs_vec_t glyphs;       /**< int32_t per slot of a glyph batch, the glyph of its Text */
    ecs_vec_t instances;    /**< box_render_instance_t per slot */
    ecs_vec_t changed;      /**< int32_t, slots of a static batch the GPU does not have */
} box_draw_batch_t;

/** Main thread state of the instanced batches */
typedef struct box_render_draws_t {
    ecs_query_t *dynamic_transform; /**< Shapes with DynamicTransform */
    ecs_query_t *dynamic_material;  /**< Shapes with DynamicMaterial and no DynamicTransform */
    ecs_query_t *dynamic_texts;     /**< Texts with DynamicTransform */
    ecs_query_t *dynamic_text_materials; /**< Texts with DynamicMaterial, self or up */
    uint32_t text_mesh;         /**< Mesh handle of the glyph quad */
    ecs_vec_t batches;          /**< box_draw_batch_t by index */
    ecs_vec_t free_batches;     /**< int32_t, indices of batches not in use */
    ecs_map_t lookup;           /**< Key hash to the index of the first batch plus one */
    ecs_vec_t dirty;            /**< ecs_entity_t, entities whose instance may have changed */
    ecs_vec_t moved;            /**< ecs_entity_t, roots of subtrees that changed */
    ecs_vec_t walk;             /**< ecs_entity_t, stack of a subtree walk */
    ecs_map_t meshes;           /**< Mesh handles the backend received */
    ecs_vec_t released;         /**< uint32_t, meshes to release this frame */
    ecs_vec_t blended;          /**< int32_t, blended batches the frame draws */
    ecs_vec_t sorted;           /**< box_draw_sorted_t, blended instances of one view */
    ecs_vec_t free_buffers;     /**< uint32_t, instance buffer handles to reuse */
    uint32_t next_buffer;       /**< Next instance buffer handle */
    uint32_t changes_size;      /**< Bytes of the change buffer, 0 before it is created */
    const box_textures_t *textures; /**< Registry the texture handles resolve from in a send */
    const ecs_world_t *world;   /**< World of the send, whose surfaces render texture entities */
    bool order_dirty;           /**< The entities or the hierarchy changed since the order */
    bool runs_dirty;            /**< 2D slots moved since the runs were made */
    ecs_vec_t order;            /**< box_draw_order_t, the entities with 2D slots in order */
    ecs_map_t ordered;          /**< Entity of the order to 1 */
    ecs_vec_t order_keys;       /**< uint64_t, the paths of the order */
    ecs_map_t paths;            /**< Entity to the offset and depth of its path, while ordering */
    ecs_vec_t runs;             /**< box_draw_run_t, the order as runs of consecutive slots */
    ecs_vec_t ordered_parents;  /**< box_draw_parent_t, parents the order read the order of */
    ecs_vec_t ordered_children; /**< ecs_entity_t, children of ordered_parents as read */
    ecs_map_t ranks;            /**< Child to its rank, while the order is made */
    ecs_map_t parents;          /**< Parents whose children have a rank, while the order is made */
    ecs_map_t order_z;          /**< Entity of a path to the z key it was ordered by */
    const void *backend;        /**< Context of the backend the GPU state was sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    box_render_draws_stats_t stats; /**< Counters of the last extract */
    const ecs_table_t *scene_table; /**< Table whose entities share scene_of while processing */
    ecs_entity_t scene_of;      /**< Scene of the entities of scene_table */
    int32_t refs;               /**< Observers and systems that use the state */
} box_render_draws_t;

/** Fill the conversion tables of the instance data, once per process.
 */
void box_draws_tables_init(void);

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

/** Add a slot for a glyph of a Text to a glyph batch.
 *
 * @param d The state
 * @param index Index of the batch
 * @param e The entity of the Text
 * @param glyph The glyph of the Text
 * @return The slot
 */
int32_t box_draws_glyph_add(
    box_render_draws_t *d,
    int32_t index,
    ecs_entity_t e,
    int32_t glyph);

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

/** Write the instance data of a shape: its model matrix and material.
 *
 * @param out The instance
 * @param transform World transform of the entity, NULL for the identity
 * @param shape Mesh and instance scale of the entity
 * @param material Material components of the entity
 */
void box_draws_instance(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape,
    const box_draw_material_t *material);

/** Write the model matrix of a shape into its instance data.
 *
 * @param out The instance
 * @param transform World transform of the entity, NULL for the identity
 * @param shape Mesh and instance scale of the entity
 */
void box_draws_transform(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape);

/** Write the material of a shape into its instance data.
 *
 * The base color is mixed toward the tints of the ancestors from the root down and then toward
 * the tint of the shape, in display encoding, before it and the emission are converted to
 * linear light. The alpha is the opacity of the shape times the opacity of its ancestors.
 *
 * @param out The instance
 * @param material Material components of the entity
 */
void box_draws_material(
    box_render_instance_t *out,
    const box_draw_material_t *material);

/** Write the shape of a 2D shape into its instance data, over the 3D surface constants.
 *
 * Material holds the corner radius, border width and border color of a rectangle or the end
 * points of a line, emissive w the aspect of a rectangle or the negated width of a line.
 *
 * @param out The instance
 * @param material Material components of the entity, with its Shape2D
 * @param border Linear border color mixed toward the tints, NULL for the color of the Border
 */
void box_draws_shape2d(
    box_render_instance_t *out,
    const box_draw_material_t *material,
    const float *border);

/** Resolve the blend mode a shape draws with: Opaque turns into AlphaBlend below alpha 1.
 *
 * @param mode The BlendMode of the shape, NULL for Opaque
 * @param alpha The resolved alpha of the shape
 * @return The blend mode
 */
uint8_t box_draws_blend(
    const BlendMode *mode,
    float alpha);

/** Get the resolved alpha of a shape.
 *
 * @param material Material components of the entity
 * @return The opacity of the shape times the opacity of its ancestors, clamped to 0..1
 */
float box_draws_alpha(
    const box_draw_material_t *material);

/** Add a shape to the changed entities unless it is in the list.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_mark(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Multiply the Opacity and compose the Tint of the ancestors, through ChildOf and Parent.
 *
 * @param world The world
 * @param e The entity
 * @param tint Receives the composed tints of the ancestors, keep 1 and add 0 without any
 * @return The product of the opacities, 1 when no ancestor has an Opacity
 */
float box_draws_fade(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint);

/** Get the layer of a 2D shape or glyph: twice the number of its ancestors, plus one for a
 * glyph, so it can be lifted over coplanar ancestors and the shape of its own entity.
 *
 * @param world The world
 * @param e The entity
 * @param glyph Whether the layer is for the glyphs of the Text of the entity
 * @return The layer
 */
float box_draws_layer(
    const ecs_world_t *world,
    ecs_entity_t e,
    bool glyph);

/** Lift a 2D instance by its layer along the local z axis of its entity, toward the side it
 * faces, so it wins the depth test over coplanar ancestors whose depth rounds differently when
 * the plane is rotated in perspective. A step is a tenth of a millimeter in world units.
 *
 * @param out The instance
 * @param transform World transform of the entity, NULL for the identity
 * @param layer The layer
 */
void box_draws_lift(
    box_render_instance_t *out,
    const WorldTransform *transform,
    float layer);

/** Send the batches of the frame: meshes, instance buffers, uploads and scatters.
 *
 * The texture entities of a batch resolve to the handles of the images the backend has.
 *
 * @param world The world
 * @param d The state
 * @param p The packet
 */
void box_draws_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p);

/** Initialize the draw order of the 2D slots.
 *
 * @param d The state
 */
void box_draws_order_init(
    box_render_draws_t *d);

/** Release the draw order of the 2D slots.
 *
 * @param d The state
 */
void box_draws_order_fini(
    box_render_draws_t *d);

/** Remember that a 2D slot was added or removed, so the runs are made again.
 *
 * A slot of an entity the order does not have yet makes the order again.
 *
 * @param d The state
 * @param e The entity of the slot, 0 for a removed slot
 */
void box_draws_order_slot(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Bring the draw order of the 2D slots up to date.
 *
 * 2D shapes and glyphs draw in hierarchy order, not by view depth: parents before children,
 * siblings by Position z, then OrderedChildren order or entity id, a shape before its glyphs.
 *
 * The order is made again when an entity got 2D slots, the hierarchy or the z of an entity in
 * it changed, or an OrderedChildren parent reordered its children; moved slots only make the
 * runs again.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_order_update(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Make the draw order again when the Position z of an entity it was made from changed.
 *
 * @param world The world
 * @param d The state
 * @param e The entity whose Position was set or removed
 */
void box_draws_order_position(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Add the 2D draws of every view in draw order, as runs of consecutive slots.
 *
 * @param p The packet
 * @param d The state
 */
void box_draws_send_ordered(
    box_render_packet_t *p,
    box_render_draws_t *d);

/** Write the texture handles and the sampler wrap of a batch into its draw.
 *
 * @param d The state
 * @param b The batch
 * @param out The draw
 */
void box_draws_batch_textures(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out);

/** Register the glyph batches of the texts: their slots, quad mesh and observers.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_text_init(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Bring the glyphs of a changed Text into their batches and write their instances.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_text_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Remove every glyph slot of a Text.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_text_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Mark the texts whose instances change every frame.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_text_dynamic(
    ecs_world_t *world,
    box_render_draws_t *d);

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
