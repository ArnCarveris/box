/**
 * @file render/draw.h
 * @brief Instanced batches of the forward pass, shared by the change tracking and the extract.
 *
 * Shapes live in batches keyed by scene, mesh, pipeline, whether their transform and material
 * are dynamic and whether they cast shadows, one swap-removed slot per entity and at most
 * BOX_DRAWS_CHUNK of them, so a key may have several batches.
 *
 * A batch owns a run of slots of an instance page (store.c), a buffer per stream (transform,
 * shading, surface) with the same slots; a change uploads only its streams. Mesh and 2D batches
 * have stores of their own: a mesh shading is a material slot (materials.c), a tint and an alpha.
 */

#ifndef BOX_RENDER_DRAW_H
#define BOX_RENDER_DRAW_H

#include "render.h"
#include "../geometry/shape2d.h"
#include "../texture/texture.h"
#include "../text/text.h"

/** Instances of one batch and slots of one instance page: every stream of a page, the surface
 * stream being the largest, stays within the 128 MiB storage binding that every WebGPU device
 * supports, and a key with more shapes continues in another batch */
#define BOX_DRAWS_CHUNK ((uint32_t)((128u << 20) / sizeof(box_render_surface_t)))

/** Slots the smallest run of a batch holds, and the smallest page buffer */
#define BOX_DRAWS_MIN_CAPACITY (64)

/** Buffers of an instance page at most, one per stream of its layout */
#define BOX_DRAWS_STREAMS (BOX_RENDER_INSTANCE_STREAMS)

/** Store of the instance pages of the mesh batches */
#define BOX_DRAWS_STORE_MESH (0)

/** Store of the instance pages of the 2D shape and glyph batches */
#define BOX_DRAWS_STORE_2D (1)

/** Stores of instance pages, one per page layout */
#define BOX_DRAWS_STORES (2)

/** Streams of an instance that a change writes, a bit per stream of the instance pages */
typedef enum box_draw_kind_t {
    BOX_DRAW_TRANSFORM = 1 << BOX_RENDER_STREAM_TRANSFORM, /**< The transform stream */
    BOX_DRAW_SHADING = 1 << BOX_RENDER_STREAM_SHADING,     /**< The shading stream */
    BOX_DRAW_SURFACE = 1 << BOX_RENDER_STREAM_SURFACE,     /**< The surface stream */
    BOX_DRAW_BOTH = BOX_DRAW_TRANSFORM | BOX_DRAW_SHADING, /**< Transform and shading */
    BOX_DRAW_ALL = BOX_DRAW_BOTH | BOX_DRAW_SURFACE        /**< Every stream */
} box_draw_kind_t;

/** Layout of the instance pages of a store: one buffer per stream, each with its bytes per slot */
typedef struct box_draw_layout_t {
    int32_t streams;                    /**< Buffers of a page, 1 to BOX_DRAWS_STREAMS */
    uint32_t stride[BOX_DRAWS_STREAMS]; /**< Bytes of one slot in each buffer */
} box_draw_layout_t;

/** Free slots of an instance page below the end of its highest run */
typedef struct box_draw_hole_t {
    uint32_t first;         /**< First free slot */
    uint32_t count;         /**< Free slots */
} box_draw_hole_t;

/** An instance page: GPU buffers whose slots are handed out to batches as runs */
typedef struct box_draw_page_t {
    bool alive;             /**< The page is in use, else its index is free */
    uint32_t slots;         /**< Slots the page holds at most */
    uint32_t capacity;      /**< Slots its buffers hold, 0 before they are created */
    uint32_t top;           /**< End of its highest run, the slots its buffers must hold */
    int32_t runs;           /**< Runs handed out */
    ecs_vec_t holes;        /**< box_draw_hole_t below top by first slot, never adjacent */
    uint32_t entries[BOX_DRAWS_STREAMS]; /**< Scatter entries of the frame per stream */
    uint32_t offset[BOX_DRAWS_STREAMS];  /**< Byte offset of the scatter segment of each stream
                                          *   in the change buffer */
    uint32_t written[BOX_DRAWS_STREAMS]; /**< Entries written into the segment of each stream */
} box_draw_page_t;

/** The instance pages every batch shares and the slot allocator that hands out their runs.
 *
 * Runs are handed out first fit at the lowest offset of the lowest page and merge with free
 * neighbors when freed; a run that fits no page starts a new page at the lowest free index.
 *
 * The allocator only depends on the order of its calls, so the same adds, removes and key
 * changes give the same pages and slots.
 */
typedef struct box_draw_store_t {
    box_draw_layout_t layout;   /**< Buffers of a page and their strides */
    uint32_t base;              /**< Handle of the first buffer of the first page */
    uint32_t slots;             /**< Slots a new page holds at most */
    ecs_vec_t pages;            /**< box_draw_page_t by page index */
    int32_t alive;              /**< Pages alive */
} box_draw_store_t;

/** Layers a floating element and its descendants are lifted over the elements that do not float,
 * more than twice the depth of any UI hierarchy */
#define BOX_DRAWS_FLOAT_LAYERS (512.0f)

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
    int32_t clip;   /**< Clip index of the nearest ClipChildren ancestor of a 2D shape, 0 none */
    bool dirty;     /**< The entity is in the list of changed entities */
    uint8_t changed; /**< box_draw_kind_t, the streams its changes since the last extract write */
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
    bool cutout;            /**< A 3D shape with AlphaCutoff, alpha tested */
    uint8_t back_face;      /**< BackFaceMode */
    uint8_t wrap;           /**< TextureWrap of the texture samplers */
    bool dynamic_transform; /**< The shapes have DynamicTransform, owned or inherited, so the
                             *   transforms upload every frame */
    bool dynamic_material;  /**< The shapes or a ChildOf ancestor have DynamicMaterial, so the
                             *   shading uploads every frame */
    bool shadow;            /**< Instances cast shadows, false under NoShadow */
    bool bake;              /**< Static 3D instances without NoBake of a scene with LightBake or
                             *   ReflectionBake, drawn by the captures */
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
    const AlphaCutoff *cutoff;  /**< Alpha test of the shape, ignored by 2D shapes */
    const PbrMaterial *pbr;     /**< Metallic and roughness */
    const Emissive *emissive;   /**< Emitted light */
    const TextureTransform *uv; /**< Scale and offset of the texture coordinates */
    const Tint *tint;           /**< Tint of the shape itself, applied after the inherited */
    const box_draw_tint_t *inherited; /**< Composed tints of the ancestors, NULL for none */
    const Shape2D *shape2d;     /**< Resolved 2D shape, NULL for a 3D shape */
    float fade;                 /**< Product of the Opacity of the ancestors */
    int32_t clip;               /**< Clip index of a 2D shape or glyph, 0 for none */
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
    bool floating;          /**< The entity or an ancestor floats, so it draws after the rest */
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
    int32_t page;           /**< Instance page of its run, -1 before it has one */
    uint32_t first;         /**< First slot of its run in the page */
    uint32_t capacity;      /**< Slots of its run */
    uint8_t full;           /**< box_draw_kind_t, streams that upload every instance instead of
                             *   the changed slots */
    bool ready;             /**< The backend has the mesh and the instances this frame */
    bool bounds_dirty;      /**< The surface of a slot changed since the bounds were computed */
    bool bounded;           /**< A slot holds a parametric surface, see center and extent */
    float center[3];        /**< Center of the local bounds of every slot, when bounded */
    float extent[3];        /**< Half size of the local bounds of every slot, when bounded */
    ecs_vec_t entities;     /**< ecs_entity_t per slot */
    ecs_vec_t glyphs;       /**< int32_t per slot of a glyph batch, the glyph of its Text */
    ecs_vec_t transforms;   /**< box_render_transform_t per slot */
    ecs_vec_t shadings;     /**< box_render_shading_t per slot of a 2D batch */
    ecs_vec_t mesh_shadings; /**< box_render_mesh_shading_t per slot of a mesh batch */
    ecs_vec_t surfaces;     /**< box_render_surface_t per slot */
    ecs_vec_t changed[BOX_DRAWS_STREAMS]; /**< int32_t per stream, slots whose data of a stream
                             *   that does not upload every frame the GPU does not have */
} box_draw_batch_t;

/** The ClipChildren elements the 2D draws clip to, by clip index.
 *
 * An element gets an index when a 2D draw below it first asks for it and keeps it until it loses
 * ClipChildren, so the instances that hold the index stay valid; freed indices are reused. Index
 * 0 is no clip. The areas are computed every frame and uploaded when one changed.
 */
typedef struct box_draw_clips_t {
    ecs_map_t index;        /**< Element to its clip index */
    ecs_vec_t entities;     /**< ecs_entity_t per clip index, 0 for a free index */
    ecs_vec_t records;      /**< box_render_clip_t per clip index, as the backend has them */
    ecs_vec_t free;         /**< int32_t, clip indices to reuse */
    uint32_t capacity;      /**< Clips the clip buffer holds, 0 before it is created */
    int32_t tagged;         /**< Entities with ClipChildren, so a world without any skips the
                             *   walk up the hierarchy */
    bool dirty;             /**< A record changed since the last upload */
} box_draw_clips_t;

/** The material table: a slot per prefab or mesh shape that owns material components, slot 0
 * the default material, as the main thread packed it and what the GPU still needs of it.
 *
 * Slots are handed out from the free list, last freed first, else after the highest slot, so
 * the same adds and removes give the same slots.
 */
typedef struct box_draw_materials_t {
    ecs_vec_t records;      /**< box_render_material_t per slot, as packed last */
    ecs_vec_t owners;       /**< ecs_entity_t per slot, 0 for slot 0 and free slots */
    ecs_vec_t marked;       /**< bool per slot, the slot is in dirty */
    ecs_vec_t dirty;        /**< uint32_t, slots the next send packs and uploads */
    ecs_vec_t free;         /**< uint32_t, freed slots */
    ecs_map_t owned;        /**< Owner entity to its slot */
    uint32_t capacity;      /**< Records the table buffer holds, 0 before it is created */
    int32_t alive;          /**< Slots alive, slot 0 included */
    bool overflow;          /**< An id past what the binding holds was logged */
} box_draw_materials_t;

/** The root of a changed subtree and the streams the change writes for each of its entities */
typedef struct box_draw_moved_t {
    ecs_entity_t root;      /**< Root of the subtree */
    uint8_t kinds;          /**< box_draw_kind_t */
} box_draw_moved_t;

/** Main thread state of the instanced batches */
typedef struct box_render_draws_t {
    ecs_query_t *dynamic_transform; /**< Shapes with DynamicTransform */
    ecs_query_t *dynamic_material;  /**< Shapes with DynamicMaterial, self or up, and no owned
                                     *   DynamicTransform */
    ecs_query_t *dynamic_texts;     /**< Texts with DynamicTransform */
    ecs_query_t *dynamic_text_materials; /**< Texts with DynamicMaterial, self or up */
    uint32_t text_mesh;         /**< Mesh handle of the glyph quad */
    ecs_vec_t batches;          /**< box_draw_batch_t by index */
    ecs_vec_t free_batches;     /**< int32_t, indices of batches not in use */
    ecs_map_t lookup;           /**< Key hash to the index of the first batch plus one */
    ecs_vec_t dirty;            /**< ecs_entity_t, entities whose instance may have changed */
    ecs_vec_t moved;            /**< box_draw_moved_t, roots of changed subtrees */
    ecs_vec_t walk;             /**< ecs_entity_t, stack of a subtree walk */
    ecs_map_t meshes;           /**< Mesh handles the backend received */
    ecs_vec_t released;         /**< uint32_t, meshes to release this frame */
    ecs_vec_t blended;          /**< int32_t, blended batches the frame draws */
    ecs_vec_t sorted;           /**< box_draw_sorted_t, blended instances of one view */
    box_draw_store_t stores[BOX_DRAWS_STORES]; /**< Instance pages of the batches, by layout */
    box_draw_materials_t materials; /**< The material table */
    uint32_t chunk;             /**< Instances a batch holds before a key starts another */
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
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    box_render_draws_stats_t stats; /**< Counters of the last extract */
    box_draw_clips_t clips;     /**< The clip areas of the 2D draws */
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

/** Find a batch of a key with room for another slot, creating one when there is none.
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

/** Remember that streams of a slot changed. The streams the batch uploads every frame are left
 * out; a stream with as many changed slots as the batch has slots uploads every instance.
 *
 * @param b The batch
 * @param slot The slot
 * @param kinds box_draw_kind_t, the streams written
 */
void box_draws_slot_changed(
    box_draw_batch_t *b,
    int32_t slot,
    uint8_t kinds);

/** Get the streams a batch uploads every frame.
 *
 * @param b The batch
 * @return box_draw_kind_t: the transform for dynamic_transform, the shading for dynamic_material
 */
uint8_t box_draws_dynamic_kinds(
    const box_draw_batch_t *b);

/** Get the transform of a slot.
 *
 * @param b The batch
 * @param slot The slot
 * @return The transform
 */
box_render_transform_t* box_draws_slot_transform(
    box_draw_batch_t *b,
    int32_t slot);

/** Get the shading of a slot of a 2D batch.
 *
 * @param b The batch
 * @param slot The slot
 * @return The shading
 */
box_render_shading_t* box_draws_slot_shading(
    box_draw_batch_t *b,
    int32_t slot);

/** Get the shading of a slot of a mesh batch.
 *
 * @param b The batch
 * @param slot The slot
 * @return The shading
 */
box_render_mesh_shading_t* box_draws_slot_mesh_shading(
    box_draw_batch_t *b,
    int32_t slot);

/** Get the store of the instance pages of a batch.
 *
 * @param d The state
 * @param b The batch
 * @return The store of its layout
 */
box_draw_store_t* box_draws_batch_store(
    box_render_draws_t *d,
    const box_draw_batch_t *b);

/** Get the surface of a slot.
 *
 * @param b The batch
 * @param slot The slot
 * @return The surface
 */
box_render_surface_t* box_draws_slot_surface(
    box_draw_batch_t *b,
    int32_t slot);

/** Write the transform of a shape: the rows of its model matrix with the instance scale.
 *
 * @param out The transform
 * @param transform World transform of the entity, NULL for the identity
 * @param shape Mesh and instance scale of the entity
 */
void box_draws_transform(
    box_render_transform_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape);

/** Write the surface parameters of a shape, zero for a plain mesh.
 *
 * @param out The surface
 * @param shape Mesh and surface of the entity
 */
void box_draws_surface(
    box_render_surface_t *out,
    const ShapeMesh *shape);

/** Write the material of a 2D shape or glyph into its instance data.
 *
 * The base color is mixed toward the tints of the ancestors from the root down and then toward
 * the tint of the shape, in display encoding, before it and the emission are converted to
 * linear light. The alpha is the opacity of the shape times the opacity of its ancestors.
 *
 * The clip index goes into ids[0].
 *
 * @param out The shading
 * @param material Material components of the entity
 */
void box_draws_material(
    box_render_shading_t *out,
    const box_draw_material_t *material);

/** Pack the material components of the owner of a slot into its record of the material table.
 *
 * The record holds the display encoded base color (the default color without Rgb), metallic and
 * roughness, the linear emission times its strength, the texture transform and the AlphaCutoff
 * (0 without one), clamped as the shading was. Opacity, tints and the 2D shape are not in it.
 *
 * @param out The record
 * @param material Material components of the owner
 */
void box_draws_material_pack(
    box_render_material_t *out,
    const box_draw_material_t *material);

/** Write the shading of a mesh instance: the slot of its material, the tints of its ancestors
 * composed with its own Tint, and its alpha.
 *
 * @param out The shading
 * @param id Slot of the material table the instance renders with
 * @param material Material components of the entity, of which the tints, the opacity and the
 * fade are read
 */
void box_draws_mesh_shading(
    box_render_mesh_shading_t *out,
    uint32_t id,
    const box_draw_material_t *material);

/** Write the shape of a 2D shape into its instance data, over the 3D surface constants.
 *
 * Material holds the corner radius, border width and border color of a rectangle or the end
 * points of a line, emissive w the aspect of a rectangle or the negated width of a line.
 *
 * @param out The shading
 * @param material Material components of the entity, with its Shape2D
 * @param border Linear border color mixed toward the tints, NULL for the color of the Border
 */
void box_draws_shape2d(
    box_render_shading_t *out,
    const box_draw_material_t *material,
    const float *border);

/** Resolve the blend mode a shape draws with: Opaque turns into AlphaBlend below alpha 1,
 * unless the shape is alpha tested, which keeps it in the opaque pass.
 *
 * @param mode The BlendMode of the shape, NULL for Opaque
 * @param alpha The resolved alpha of the shape
 * @param cutout Whether the shape is alpha tested (box_draws_cutout)
 * @return The blend mode
 */
uint8_t box_draws_blend(
    const BlendMode *mode,
    float alpha,
    bool cutout);

/** Check whether a shape is alpha tested: a 3D shape with AlphaCutoff.
 *
 * @param cutoff The AlphaCutoff of the shape, NULL for none
 * @param shape2d The resolved 2D shape, NULL for a 3D shape
 * @return Whether its fragments are tested against the cutoff
 */
bool box_draws_cutout(
    const AlphaCutoff *cutoff,
    const Shape2D *shape2d);

/** Get the resolved alpha of a shape.
 *
 * @param material Material components of the entity
 * @return The opacity of the shape times the opacity of its ancestors, clamped to 0..1
 */
float box_draws_alpha(
    const box_draw_material_t *material);

/** Add a shape to the changed entities unless it is in the list, with the streams its change
 * writes.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param kinds box_draw_kind_t, the streams the change writes
 */
void box_draws_mark(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds);

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
 * glyph, so it can be lifted over coplanar ancestors and the shape of its own entity. A floating
 * element (LayoutFloat) and its descendants are lifted BOX_DRAWS_FLOAT_LAYERS further, over the
 * elements that do not float.
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
 * @param out The transform
 * @param transform World transform of the entity, NULL for the identity
 * @param layer The layer
 */
void box_draws_lift(
    box_render_transform_t *out,
    const WorldTransform *transform,
    float layer);

/** Send the batches of the frame: meshes, instance pages, uploads and scatters.
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

/** Initialize a store of instance pages, without pages.
 *
 * @param s The store
 * @param layout Buffers of a page and their strides
 * @param base Handle of the first buffer of the first page
 * @param slots Slots a page holds at most
 */
void box_draws_store_init(
    box_draw_store_t *s,
    const box_draw_layout_t *layout,
    uint32_t base,
    uint32_t slots);

/** Release the memory of a store. The buffers of its pages are not released.
 *
 * @param s The store
 */
void box_draws_store_fini(
    box_draw_store_t *s);

/** Forget every page and run, after a new backend that has none of their buffers.
 *
 * @param s The store
 */
void box_draws_store_reset(
    box_draw_store_t *s);

/** Get the handle of a buffer of a page.
 *
 * @param s The store
 * @param page Index of the page
 * @param stream Buffer of the page
 * @return The handle
 */
uint32_t box_draws_store_handle(
    const box_draw_store_t *s,
    int32_t page,
    int32_t stream);

/** Hand out a run of slots, first fit at the lowest offset of the lowest page, starting a
 * page when none has room.
 *
 * @param s The store
 * @param count Slots of the run
 * @param page Receives the index of the page
 * @param first Receives the first slot of the run
 */
void box_draws_store_alloc(
    box_draw_store_t *s,
    uint32_t count,
    int32_t *page,
    uint32_t *first);

/** Free a run, merging it with the free slots next to it. A page without runs is released in
 * the packet, except the first page.
 *
 * @param s The store
 * @param p The packet
 * @param page Index of the page
 * @param first First slot of the run
 * @param count Slots of the run
 */
void box_draws_store_free(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page,
    uint32_t first,
    uint32_t count);

/** Make the buffers of a page hold its runs, creating them again by doubling up to the slots of
 * the page. A created buffer has none of the instances of the runs in it.
 *
 * @param s The store
 * @param p The packet
 * @param page Index of the page
 * @return Whether the buffers were created, so every run of the page must upload
 */
bool box_draws_store_reserve(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page);

/** Get a page of a store.
 *
 * @param s The store
 * @param page Index of the page
 * @return The page
 */
box_draw_page_t* box_draws_store_page(
    box_draw_store_t *s,
    int32_t page);

/** Register an observer of the instanced batches, which holds a reference to the state.
 *
 * @param world The world
 * @param d The state
 * @param name Name of the observer
 * @param desc Terms and events of the observer
 * @param callback What the observer does
 */
void box_draws_observer(
    ecs_world_t *world,
    box_render_draws_t *d,
    const char *name,
    ecs_observer_desc_t desc,
    ecs_iter_action_t callback);

/** Initialize the material table with its default slot 0.
 *
 * @param d The state
 */
void box_draws_materials_init(
    box_render_draws_t *d);

/** Release the material table.
 *
 * @param d The state
 */
void box_draws_materials_fini(
    box_render_draws_t *d);

/** Forget the table buffer the backend had, so it is created and uploaded again.
 *
 * @param d The state
 */
void box_draws_materials_reset(
    box_render_draws_t *d);

/** Register the observers that hand out and free the slots of the material table and pack them
 * again when a material component of their owner, or one they inherit, changes.
 *
 * A prefab gets a slot when it gets its MaterialId; a mesh shape that owns a MaterialId gets
 * one in the extract (box_draws_material_id). Both are freed when the owner loses its
 * MaterialId or is deleted.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_materials_observers(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Get the slot of the material table an entity renders with.
 *
 * A mesh shape that owns a MaterialId renders with a slot of its own, handed out when it has
 * none. Any other entity renders with the MaterialId it inherits, or slot 0, and gives back an
 * own slot it had. A slot past what the binding holds is logged once and renders as slot 0.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param mesh Whether the entity is drawn by the mesh pipeline
 * @return The slot
 */
uint32_t box_draws_material_id(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    bool mesh);

/** Give back the own slot of an entity that stops being a mesh shape, not the slot of a prefab.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_material_release(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Pack the slot an entity owns again in the next send, when it owns one.
 *
 * @param d The state
 * @param e The entity
 * @return Whether the entity owns a slot
 */
bool box_draws_material_changed(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Pack the changed slots of the material table and upload them: merged runs of consecutive
 * slots, or the whole table when its buffer is created.
 *
 * @param world The world
 * @param d The state
 * @param p The packet
 */
void box_draws_materials_send(
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
 * Glyphs that keep their slots record the streams of the change; new slots write every stream.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param kinds box_draw_kind_t, the streams the change writes
 */
void box_draws_text_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds);

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

/** Mark the texts whose instances change every frame, without streams of their own: their
 * glyphs write the streams their batches upload every frame.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_text_dynamic(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Initialize the clip areas of the 2D draws.
 *
 * @param d The state
 */
void box_draws_clips_init(
    box_render_draws_t *d);

/** Release the clip areas of the 2D draws.
 *
 * @param d The state
 */
void box_draws_clips_fini(
    box_render_draws_t *d);

/** Get the clip index of the nearest ClipChildren ancestor of an entity, giving the ancestor
 * an index when it has none. The walk stops at a floating element (LayoutFloat).
 *
 * @param world The world
 * @param d The state
 * @param e The entity, whose own ClipChildren does not clip it
 * @return The clip index, 0 when no ancestor clips
 */
int32_t box_draws_clip_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Count an element that got ClipChildren.
 *
 * @param d The state
 */
void box_draws_clip_added(
    box_render_draws_t *d);

/** Free the clip index of an element that lost ClipChildren.
 *
 * @param d The state
 * @param e The element
 */
void box_draws_clip_removed(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Write the areas of the clips into the clip buffer when one changed: added, removed, moved,
 * resized or reparented.
 *
 * @param world The world
 * @param d The state
 * @param p The packet
 */
void box_draws_clips_send(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p);

/** Forget the clip buffer the backend had, so it is created and uploaded again.
 *
 * @param d The state
 */
void box_draws_clips_reset(
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
