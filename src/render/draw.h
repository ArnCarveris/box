/**
 * @file render/draw.h
 * @brief Instanced batches of the forward pass, shared by the change tracking and the extract.
 *
 * Shapes live in batches keyed by scene, mesh, pipeline, whether their transform and material
 * are dynamic, whether they cast shadows and whether they have a parametric surface, one
 * swap-removed slot per entity and at most the chunk of its store (box_draw_store_t.chunk).
 *
 * A batch owns a run of slots of an instance page (store.c), a buffer per stream; a change
 * uploads only its streams. Only the pages of the surface store have a surface stream, plain mesh
 * and 2D pages bind a zero record. A mesh shading is a tint, an alpha and a material slot.
 */

#ifndef BOX_RENDER_DRAW_H
#define BOX_RENDER_DRAW_H

#include "render.h"
#include "../geometry/shape2d.h"
#include "../texture/texture.h"
#include "../text/text.h"

/** Bytes of the largest buffer of an instance page: the 128 MiB storage binding every WebGPU
 * device supports. The backend requests the default limits, whose maxStorageBufferBindingSize
 * is this and whose maxBufferSize is larger, so a page buffer never exceeds either */
#define BOX_DRAWS_PAGE_BYTES (128u << 20)

/** Instances of one batch and slots of one instance page of the surface store, the smallest
 * chunk of any store: BOX_DRAWS_PAGE_BYTES over its largest stride, the surface stream.
 *
 * Each store derives its chunk from its own largest stride (box_draws_layout_chunk), so plain
 * mesh pages hold 128 MiB / 48 slots and 2D pages 128 MiB / 64 */
#define BOX_DRAWS_CHUNK ((uint32_t)(BOX_DRAWS_PAGE_BYTES / sizeof(box_render_surface_t)))

/** Slots the smallest run of a batch holds, and the smallest page buffer */
#define BOX_DRAWS_MIN_CAPACITY (16)

/** Slots every run and page buffer is a multiple of when it grows by half. Every stride is a
 * multiple of 4 bytes, so run offsets, copies and buffer sizes stay multiples of the 4 bytes
 * WebGPU requires (BOX_RENDER_COPY_ALIGN) */
#define BOX_DRAWS_GROW_ALIGN (4)

/** Buffers of an instance page at most, one per stream of its layout; the handles of a page
 * leave room for all of them, see BOX_RENDER_INSTANCE_STREAMS */
#define BOX_DRAWS_STREAMS (BOX_RENDER_INSTANCE_STREAMS)

/** Store of the instance pages of the plain mesh batches, without a parametric surface: the
 * transform and the shading stream, from BOX_RENDER_BUFFER_FIRST */
#define BOX_DRAWS_STORE_MESH (0)

/** Store of the instance pages of the 2D shape and glyph batches */
#define BOX_DRAWS_STORE_2D (1)

/** Store of the instance pages of the mesh batches with a parametric surface: every stream,
 * from BOX_RENDER_BUFFER_FIRST_SURFACE */
#define BOX_DRAWS_STORE_SURFACE (2)

/** Stores of instance pages, one per page layout */
#define BOX_DRAWS_STORES (3)

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
    uint32_t serial;        /**< Bumped when the page is released, so a run of the released
                             *   page is told apart from a run of a page that reuses its index */
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
    uint32_t max;               /**< Slots a page may hold at most, BOX_DRAWS_PAGE_BYTES over
                                 *   the largest stride of the layout */
    uint32_t slots;             /**< Slots a new page holds at most, up to max */
    uint32_t chunk;             /**< Instances a batch holds before a key starts another, max
                                 *   unless lowered for tests */
    ecs_vec_t pages;            /**< box_draw_page_t by page index */
    int32_t alive;              /**< Pages alive */
} box_draw_store_t;

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
    uint32_t rank;  /**< Rank of a 2D shape in the draw order of its tree (box_draw_order_t),
                     *   which lifts its depth over the elements drawn before it */
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
    bool transmissive;      /**< A 3D shape that has or inherits Transmission: blend is
                             *   AlphaBlend, so it draws in the sorted blended pass, cutout and
                             *   shadow are false */
    uint8_t back_face;      /**< BackFaceMode */
    uint8_t wrap;           /**< TextureWrap of the texture samplers */
    bool dynamic_transform; /**< The shapes have DynamicTransform, owned or inherited, or are
                             *   moved by a transform transition (TransitionTransform), so the
                             *   transforms upload every frame */
    bool dynamic_material;  /**< The shapes or a ChildOf ancestor have DynamicMaterial, so the
                             *   shading uploads every frame */
    bool shadow;            /**< Instances cast shadows, false under NoShadow */
    bool surface;           /**< The shapes have a parametric surface: the batch has the
                             *   surface stream, its pages are of the surface store. The mesh
                             *   already tells surfaces apart, the bit selects the store */
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
    uint32_t rank;   /**< Rank of the glyphs in the draw order of the tree of the Text, above
                      *   the shape of its entity */
} TextSlots;

/** Component id of TextSlots */
extern ECS_COMPONENT_DECLARE(TextSlots);

/** Batch, slot and rank of the shadow of a 2D shape with a BoxShadow, added with every BoxShadow.
 *
 * The shadow is a second instance of the entity drawn just before its shape, in the batch of the
 * shape with the AlphaBlend blend mode and no textures. */
typedef struct BoxShadowSlot {
    int32_t batch;  /**< Index of the batch, -1 when no shadow is drawn */
    int32_t slot;   /**< Slot of the shadow in the batch */
    uint32_t rank;  /**< Rank of the shadow in the draw order of the tree of the entity, below
                     *   the shape of its entity */
} BoxShadowSlot;

/** Component id of BoxShadowSlot */
extern ECS_COMPONENT_DECLARE(BoxShadowSlot);

/** The material components of a shape, NULL for the ones it does not have */
typedef struct box_draw_material_t {
    const Rgb *color;           /**< Base color, display encoded */
    const Opacity *opacity;     /**< Opacity of the shape itself */
    const AlphaCutoff *cutoff;  /**< Alpha test of the shape, ignored by 2D shapes */
    const PbrMaterial *pbr;     /**< Metallic and roughness */
    const Emissive *emissive;   /**< Emitted light */
    const TextureTransform *uv; /**< Scale and offset of the texture coordinates */
    const Transmission *transmission; /**< Light passing through a 3D shape */
    const ClearCoat *clear_coat; /**< Dielectric layer over a 3D shape, ignored by 2D shapes */
    const SpecularAnisotropy *anisotropy; /**< Stretched highlight of a 3D shape, ignored by 2D
                                 *   shapes */
    const Sheen *sheen;         /**< Grazing rim of a 3D shape, ignored by 2D shapes */
    const DiffuseTransmission *diffuse_transmission; /**< Back lit glow of a 3D shape, ignored
                                 *   by 2D shapes */
    const Tint *tint;           /**< Tint of the shape itself, applied after the inherited */
    const box_draw_tint_t *inherited; /**< Composed tints of the ancestors, NULL for none */
    const Shape2D *shape2d;     /**< Resolved 2D shape, NULL for a 3D shape */
    float fade;                 /**< Product of the Opacity of the ancestors */
    int32_t clip;               /**< Clip index of a 2D shape or glyph, 0 for none */
    uint32_t rank;              /**< Rank of a 2D shape or glyph in the draw order of its tree */
} box_draw_material_t;

/** What the record a material transition patches is */
typedef enum box_draw_transition_kind_t {
    BOX_DRAW_TRANSITION_TABLE = 0,  /**< A box_render_material_t of the material table */
    BOX_DRAW_TRANSITION_SHAPE,      /**< The box_render_shading_t of a 2D shape */
    BOX_DRAW_TRANSITION_GLYPH,      /**< The box_render_shading_t of a glyph */
    BOX_DRAW_TRANSITION_MESH        /**< The box_render_mesh_shading_t of a mesh shape: its tint
                                     *   and alpha */
} box_draw_transition_kind_t;

/** The target material of a record a material transition patches, as the game thread encoded
 * the record from it, in the packet data of a material or shading record.
 *
 * The resolver replaces the value of each component that has a node by the value the node
 * renders and encodes the record again with the functions the game thread used, so a component
 * at its target gives the bytes of the record without a transition. */
typedef struct box_draw_transition_material_t {
    uint32_t nodes[BOX_RENDER_TRANSITION_COMPONENTS]; /**< By component, the node that owns the
                             *   value and runs a transition of it, 0 to keep the target */
    uint16_t present;       /**< 1 << box_render_transition_component_t per component the
                             *   material has */
    uint8_t kind;           /**< box_draw_transition_kind_t */
    bool tinted;            /**< inherited holds the tints of the ancestors */
    Rgb color;              /**< Rgb */
    AlphaCutoff cutoff;     /**< AlphaCutoff */
    PbrMaterial pbr;        /**< PbrMaterial */
    Emissive emissive;      /**< Emissive */
    TextureTransform uv;    /**< TextureTransform */
    Transmission transmission; /**< Transmission */
    ClearCoat clear_coat;   /**< ClearCoat */
    SpecularAnisotropy anisotropy; /**< SpecularAnisotropy */
    Sheen sheen;            /**< Sheen */
    DiffuseTransmission diffuse_transmission; /**< DiffuseTransmission */
    Opacity opacity;        /**< Opacity of the shape itself */
    Tint tint;              /**< Tint of the shape itself */
    box_draw_tint_t inherited; /**< Tints of the ancestors, when tinted */
    float fade;             /**< Product of the Opacity of the ancestors */
    Shape2D shape2d;        /**< The 2D shape of a shape record */
    uint32_t fade_node;     /**< Nearest node at or above the entity with an active Tint or
                             *   Opacity transition, 0 for none: the resolver replaces
                             *   inherited and fade by the values it renders */
    bool fade_self;         /**< The entity is that node: it inherits what the node renders
                             *   above it, and its own Tint and Opacity are values of the node */
    box_draw_tint_t fade_rel; /**< Composed tints of the entities strictly between the node and
                             *   the entity, applied after those of the node */
    float fade_alpha;       /**< Product of the opacities of those entities */
    float opacity_scale;    /**< Factor of the own Opacity, the alpha of the span of a glyph */
} box_draw_transition_material_t;

/** A matrix known to lie, element by element, between two matrices: the world matrices a node
 * of a transform transition can render with in a frame */
typedef struct box_draw_transition_range_t {
    mat4 lo;                    /**< Minimum of each element */
    mat4 hi;                    /**< Maximum of each element */
} box_draw_transition_range_t;

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
    ecs_entity_t tree;      /**< Topmost ancestor that is not a scene root, the entity itself
                             *   without one: the entities of a tree count their ranks together */
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
    uint32_t sent;          /**< Slots of its run the GPU holds the instances of: the instances
                             *   it had when it was last sent, 0 before */
    int32_t from_page;      /**< Page of the run it moved out of in this extract */
    uint32_t from_first;    /**< First slot of that run */
    uint32_t from_count;    /**< Slots of that run whose instances the GPU copies into the new
                             *   run instead of uploading them, 0 when it did not move */
    uint32_t from_serial;   /**< Serial of that page when the batch moved, see
                             *   box_draw_page_t */
    bool ready;             /**< The backend has the mesh and the instances this frame */
    bool was_ready;         /**< It had them in the last frame, so a caster batch that gets
                             *   them records the regions of its slots */
    uint32_t shadow_texture; /**< Revision of the albedo image an alpha tested caster batch had
                             *   in the last frame, see box_textures_revision */
    bool bounds_dirty;      /**< The surface of a slot changed since the bounds were computed */
    bool bounded;           /**< A slot holds a parametric surface, see center and extent */
    float center[3];        /**< Center of the local bounds of every slot, when bounded */
    float extent[3];        /**< Half size of the local bounds of every slot, when bounded */
    int64_t transform_frame; /**< Frame of the packet the transform stream of its run was last
                             *   uploaded in */
    uint32_t transform_data; /**< Offset of that upload in the packet data, where the transform
                             *   of slot i starts at i * sizeof(box_render_transform_t) */
    int64_t shading_frame;  /**< Frame of the packet the shading stream of its run was last
                             *   uploaded in */
    uint32_t shading_data;  /**< Offset of that upload in the packet data, where the shading of
                             *   slot i of a 2D batch starts at i * sizeof(box_render_shading_t) */
    ecs_vec_t entities;     /**< ecs_entity_t per slot */
    ecs_vec_t glyphs;       /**< int32_t per slot of a glyph batch, the glyph of its Text */
    ecs_vec_t transforms;   /**< box_render_transform_t per slot */
    ecs_vec_t shadings;     /**< box_render_shading_t per slot of a 2D batch */
    ecs_vec_t mesh_shadings; /**< box_render_mesh_shading_t per slot of a mesh batch */
    ecs_vec_t surfaces;     /**< box_render_surface_t per slot of a mesh batch with a parametric
                             *   surface, empty for any other batch */
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

/** Consecutive records of the material table one send uploaded */
typedef struct box_draw_material_upload_t {
    uint32_t first;         /**< First slot */
    uint32_t count;         /**< Slots */
    uint32_t data;          /**< Offset of the records in the packet data */
} box_draw_material_upload_t;

/** A slot of the material table: shared by the owners whose material packs to its record, or
 * private to one owner */
typedef struct box_draw_material_slot_t {
    ecs_entity_t owner;     /**< Owner of a private slot, 0 for a shared or free slot */
    uint64_t hash;          /**< Hash of the record of a shared slot */
    int32_t refs;           /**< Owners of a shared slot, slot 0 one more; 0 for a private or
                             *   free slot */
    int32_t stamp;          /**< box_draw_materials_t.stamp when the walk of a running material
                             *   transition last took the private slot */
    uint8_t flags;          /**< Lists the slot is in and why it is private (materials.c) */
} box_draw_material_slot_t;

/** The material table, slot 0 the default material, as the main thread packed it and what the
 * GPU still needs of it.
 *
 * Mesh shapes that own material components share a slot per distinct record, with reference
 * counts. Prefabs, shapes of DynamicMaterial batches and shapes with a running material
 * transition own a private slot, which is packed from its owner and patched by slot.
 *
 * Slots are handed out from the free list, last freed first, else after the highest slot, so
 * the same adds and removes give the same slots.
 */
typedef struct box_draw_materials_t {
    ecs_vec_t records;      /**< box_render_material_t per slot, as packed last */
    ecs_vec_t slots;        /**< box_draw_material_slot_t per slot */
    ecs_vec_t dirty;        /**< uint32_t, slots the next send packs and uploads */
    ecs_vec_t free;         /**< uint32_t, freed slots */
    ecs_map_t owned;        /**< Owner entity to its slot, with a pending bit above the low 32
                             *   bits while the owner of a shared slot waits to be hashed */
    ecs_map_t contents;     /**< Hash of a record to the shared slot that holds it */
    ecs_vec_t pending;      /**< ecs_entity_t, owners of shared slots whose material changed */
    ecs_vec_t transiting;   /**< uint32_t, private slots of running material transitions */
    int32_t stamp;          /**< Extracts the table resolved, which dates the transitions */
    uint32_t capacity;      /**< Records the table buffer holds, 0 before it is created */
    int32_t alive;          /**< Slots alive, slot 0 included */
    bool overflow;          /**< An id past what the binding holds was logged */
    int64_t uploaded_frame; /**< Frame of the packet of the last send that uploaded */
    ecs_vec_t uploaded;     /**< box_draw_material_upload_t of that send */
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
    ecs_map_t tree_ranks;       /**< Tree of the order to its next rank and its count of ranks,
                                 *   while the runs are made */
    const void *backend;        /**< Context of the backend the GPU state was sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    box_render_draws_stats_t stats; /**< Counters of the last extract */
    box_draw_clips_t clips;     /**< The clip areas of the 2D draws */
    const ecs_table_t *scene_table; /**< Table whose entities share scene_of while processing */
    ecs_entity_t scene_of;      /**< Scene of the entities of scene_table */
    ecs_vec_t shadow_regions;   /**< box_render_shadow_region_t, the bounds shadow casters had
                                 *   and have after their changes, which the light extract takes */
    ecs_vec_t shadow_materials; /**< uint32_t, slots of the material table whose cutoff or UV
                                 *   transform, which alpha tested casters read, changed in this
                                 *   extract while recording */
    bool shadow_record;         /**< Record shadow_regions, while a scene keeps shadow tiles */
    int32_t refs;               /**< Observers and systems that use the state */
} box_render_draws_t;

/** Get the state of the draw extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the draw extract
 */
box_render_draws_t* box_draws_state(
    const ecs_world_t *world);

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

/** Check whether the slots of a batch cast shadows into the shadow atlas tiles: opaque mesh
 * shapes without NoShadow, as the cull of the tiles keeps them.
 *
 * @param b The batch
 * @return Whether its instances are shadow casters
 */
bool box_draws_caster(
    const box_draw_batch_t *b);

/** Compute the world bounds of a slot of a batch at every transform that lies, element by
 * element, between two transforms: those of its local bounds, from its surface record for a
 * surface instance or else from the mesh of the batch. Equal transforms give one transform.
 *
 * Each term of a row picks its corner coordinate apart from the others and rounded sums keep
 * their order, so the sums of the least and the largest terms are the bounds of the 8 corners,
 * bit for bit, with a quarter of the products.
 *
 * @param world The world, whose mesh cache has the mesh of the batch
 * @param b The batch
 * @param lo The minimum of each element of the transform
 * @param hi The maximum of each element of the transform
 * @param surface The surface of the slot, NULL for a batch without surfaces
 * @param out Receives the bounds, with the scene of the batch
 * @return Whether the bounds are known, false when the mesh of the batch is missing or empty
 */
bool box_draws_shadow_bounds(
    const ecs_world_t *world,
    const box_draw_batch_t *b,
    const box_render_transform_t *lo,
    const box_render_transform_t *hi,
    const box_render_surface_t *surface,
    box_render_shadow_region_t *out);

/** Record the world bounds a caster slot of a batch has with a transform and a surface, while
 * the state records the regions of changed shadow casters; nothing for a batch of no caster.
 *
 * @param world The world, whose mesh cache has the mesh of the batch
 * @param d The state
 * @param b The batch
 * @param transform The transform of the slot
 * @param surface The surface of the slot
 */
void box_draws_shadow_region(
    const ecs_world_t *world,
    box_render_draws_t *d,
    const box_draw_batch_t *b,
    const box_render_transform_t *transform,
    const box_render_surface_t *surface);

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

/** Get the instances a batch of a key holds before the key continues in another batch: the
 * chunk of the store of the key, at most the slots of its pages.
 *
 * @param d The state
 * @param key The key
 * @return The instances
 */
uint32_t box_draws_key_limit(
    box_render_draws_t *d,
    const box_draw_key_t *key);

/** Get the store of the instance pages of a batch.
 *
 * @param d The state
 * @param b The batch
 * @return The store of its layout
 */
box_draw_store_t* box_draws_batch_store(
    box_render_draws_t *d,
    const box_draw_batch_t *b);

/** Get the streams the instance pages of a batch have: every stream for a mesh batch with a
 * parametric surface, the transform and the shading for a plain mesh, 2D shape or glyph batch.
 *
 * @param b The batch
 * @return box_draw_kind_t, a bit per stream of its pages
 */
uint8_t box_draws_batch_kinds(
    const box_draw_batch_t *b);

/** Get the surface of a slot.
 *
 * @param b The batch
 * @param slot The slot
 * @return The surface, NULL for a plain mesh or 2D batch, whose pages have no surface stream
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
 * linear light. The alpha is the opacity of the shape times the opacity of its ancestors. The
 * color and alpha are stored as unorm16x4 (box_draws_color_pack).
 *
 * The clip index goes into ids[0].
 *
 * @param out The shading
 * @param material Material components of the entity
 */
void box_draws_material(
    box_render_shading_t *out,
    const box_draw_material_t *material);

/** Pack a linear color and alpha into the color of a 2D shading as unorm16x4: r | g << 16,
 * b | a << 16, each channel clamped to 0..1.
 *
 * @param out Two words receiving the packed color
 * @param color The color, r, g, b, a
 */
void box_draws_color_pack(
    uint32_t *out,
    const float *color);

/** Unpack the unorm16x4 color of a 2D shading, as unpack2x16unorm does.
 *
 * @param out Four floats receiving r, g, b, a
 * @param packed The two words of the packed color
 */
void box_draws_color_unpack(
    float *out,
    const uint32_t *packed);

/** Pack the material components of the owner of a slot into its record of the material table.
 *
 * The record holds the display encoded base color (the default color without Rgb), metallic,
 * roughness, emission, texture transform and AlphaCutoff (0 without one) clamped as the shading
 * was, the Transmission (factor 0 without one) and the lobes of the other material components.
 *
 * ClearCoat, SpecularAnisotropy and Sheen are 0 without one or at 0, a Sheen at a black color,
 * DiffuseTransmission 0 without one, its blur 0 when negative or not finite.
 * Opacity, tints and the 2D shape are not.
 *
 * @param out The record
 * @param material Material components of the owner
 */
void box_draws_material_pack(
    box_render_material_t *out,
    const box_draw_material_t *material);

/** Copy the material components of a record into the target material of a material
 * transition, with no node.
 *
 * @param out The target material
 * @param material Material components the record was encoded from
 * @param kind box_draw_transition_kind_t of the record
 */
void box_draws_transition_target(
    box_draw_transition_material_t *out,
    const box_draw_material_t *material,
    box_draw_transition_kind_t kind);

/** Encode a record from a material transition with the encoding of the game thread: a table
 * record with box_draws_material_pack, a 2D shading (color, emission, material, UV words) or a
 * glyph (color, emission) with box_draws_material, a mesh shading with box_draws_mesh_shading.
 *
 * The other words (the clip, the rank, the atlas area of a glyph, the material slot of a mesh)
 * do not depend on a transitioned component and are left as they are.
 *
 * Pure: reads no world, so the resolver calls it on the render thread.
 *
 * @param material The material, with the rendered values of its nodes
 * @param dst The record in the packet data, unaligned
 */
void box_draws_transition_encode(
    const box_draw_transition_material_t *material,
    void *dst);

/** Make the range of a single matrix.
 *
 * @param m The matrix
 * @param out Receives the range
 */
void box_draws_range_point(
    mat4 m,
    box_draw_transition_range_t *out);

/** Get the range of the product of two matrices that lie in ranges, element by element in the
 * order of glm_mul, so the range of the product of two single matrices is their glm_mul.
 * A single matrix operand takes plain products, with the same result as the interval math.
 *
 * @param a The range of the left matrix
 * @param b The range of the right matrix
 * @param out Receives the range of a * b, which may be a or b
 */
void box_draws_range_mul(
    const box_draw_transition_range_t *a,
    const box_draw_transition_range_t *b,
    box_draw_transition_range_t *out);

/** Get the range of the local matrices box_transform_local builds from a position, rotation
 * and scale whose fields lie in ranges: Ry * Rx * Rz, its columns scaled, then the position.
 * An element of a rotation never leaves -1 to 1, which bounds a long sweep.
 *
 * @param lo Minimum of each field of the position, the rotation and the scale, in that order
 * @param hi Maximum of each field of them
 * @param out Receives the range
 */
void box_draws_range_local(
    vec3 lo[3],
    vec3 hi[3],
    box_draw_transition_range_t *out);

/** Get the local matrix of the target pose of an entity, identity without a Position.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the matrix
 */
void box_draws_target_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out);

/** Get the target world matrix of the parent of a node without a parent node: that of its
 * nearest ancestor with a WorldTransform, identity for none.
 *
 * @param world The world
 * @param e The entity of the node
 * @param out Receives the matrix
 */
void box_draws_parent_world(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out);

/** Get the range of the world matrices of a node: the range of its parent node times its
 * chain, times the range of its local matrices.
 *
 * @param rel The target chain from the parent node, or the world matrix of the parent
 * @param up The range of the parent node, NULL without a parent node
 * @param lo The minimum of the position, rotation and scale
 * @param hi The maximum of the position, rotation and scale
 * @param out Receives the range
 */
void box_draws_range_pose(
    mat4 rel,
    const box_draw_transition_range_t *up,
    vec3 lo[3],
    vec3 hi[3],
    box_draw_transition_range_t *out);

/** Check whether the shape or a glyph of an entity has a slot in an alive shadow caster batch.
 *
 * @param world The world
 * @param d The state of the batches
 * @param e The entity, alive
 * @return Whether it has
 */
bool box_draws_entity_casts(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Get the world bounds of a slot of a caster batch at every matrix of a range times the
 * target chain of the slot (box_draws_shadow_bounds), or the whole scene without its mesh.
 *
 * @param world The world, whose mesh cache has the mesh of the batch
 * @param b The batch
 * @param slot The slot
 * @param range The range of the world matrices of the node that moves the slot
 * @param rel The target chain from the node down to the instance, with the instance matrix
 * @param out Receives the bounds
 */
void box_draws_range_region(
    const ecs_world_t *world,
    box_draw_batch_t *b,
    int32_t slot,
    const box_draw_transition_range_t *range,
    mat4 rel,
    box_render_shadow_region_t *out);

/** Get the material components a slot of the material table is packed from: those its owner
 * owns or inherits.
 *
 * @param world The world
 * @param owner The owner, 0 for the default material
 * @param out Receives the material
 */
void box_draws_material_of(
    const ecs_world_t *world,
    ecs_entity_t owner,
    box_draw_material_t *out);

/** Get the material components the shading of a 2D shape is written from.
 *
 * @param world The world
 * @param e The entity
 * @param s Its DrawSlot, whose fade, tints, clip and rank are read
 * @param out Receives the material
 */
void box_draws_shape_material(
    const ecs_world_t *world,
    ecs_entity_t e,
    const DrawSlot *s,
    box_draw_material_t *out);

/** Get the material components the glyphs of a Text are written from.
 *
 * @param world The world
 * @param e The entity
 * @param tint Receives the tints of the ancestors that out points at
 * @param out Receives the material
 */
void box_draws_text_material(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint,
    box_draw_material_t *out);

/** Get the material a glyph in a colored span is written from: the color of the span and its
 * alpha times the opacity of the text.
 *
 * @param material Material components of the text
 * @param color Display encoded color of the span
 * @param out Receives the material, pointing at rgb and opacity
 * @param rgb Receives the color of the span
 * @param opacity Receives the opacity of the glyph
 */
void box_draws_glyph_span_material(
    const box_draw_material_t *material,
    const rgba *color,
    box_draw_material_t *out,
    Rgb *rgb,
    Opacity *opacity);

/** Get the offset in the packet data of a record of the material table that the last send
 * uploaded.
 *
 * @param d The state
 * @param slot The slot
 * @param frame Frame of the packet
 * @param out Receives the offset
 * @return Whether the send of that packet uploaded the slot
 */
bool box_draws_material_data(
    const box_render_draws_t *d,
    uint32_t slot,
    int64_t frame,
    uint32_t *out);

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

/** Get the slots a page of a layout holds at most: BOX_DRAWS_PAGE_BYTES over its largest
 * stride, so no buffer of the page exceeds it.
 *
 * @param layout Buffers of a page and their strides
 * @return The slots, at least 1
 */
uint32_t box_draws_layout_chunk(
    const box_draw_layout_t *layout);

/** Initialize a store of instance pages, without pages. Its pages hold and its batches keep at
 * most the chunk of its layout (box_draws_layout_chunk).
 *
 * @param s The store
 * @param layout Buffers of a page and their strides
 * @param base Handle of the first buffer of the first page
 */
void box_draws_store_init(
    box_draw_store_t *s,
    const box_draw_layout_t *layout,
    uint32_t base);

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

/** Extend a run that ends at the top of its page in place, when the page has the slots.
 *
 * @param s The store
 * @param page Index of the page
 * @param first First slot of the run
 * @param count Slots of the run
 * @param extend Slots of the run after it is extended
 * @return Whether the run was extended
 */
bool box_draws_store_extend(
    box_draw_store_t *s,
    int32_t page,
    uint32_t first,
    uint32_t count,
    uint32_t extend);

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

/** Slots of the smallest growth step at or above a count: from a capacity, at least
 * BOX_DRAWS_MIN_CAPACITY, each step adds half and rounds up to BOX_DRAWS_GROW_ALIGN slots. From
 * the minimum the steps are 16, 24, 36, 56, 84, 128, 192, 288, ...
 *
 * A batch that outgrows its run grows to the step at or above its count, from 0, so a run of
 * 1,000 slots grows to 1,460. A new batch has no growth headroom: from its own count the result
 * is the count rounded up to BOX_DRAWS_GROW_ALIGN slots, at least BOX_DRAWS_MIN_CAPACITY
 * (box_draws_reserve).
 *
 * @param capacity Slots to grow from, 0 for the minimum
 * @param count Slots the result holds at least
 * @return Slots of the step
 */
uint32_t box_draws_grow(
    uint32_t capacity,
    uint32_t count);

/** Make the buffers of a page hold its runs, creating them again up to the slots of the page.
 *
 * A page's first buffers hold its top, rounded up to BOX_DRAWS_GROW_ALIGN slots and at least
 * BOX_DRAWS_MIN_CAPACITY. A page that outgrows them grows by half, or to its top when that is
 * more, which keeps the copies of a page that grows step by step in proportion to its slots.
 *
 * A buffer created again keeps the bytes of the buffer it replaces (BOX_RENDER_BUFFER_KEEP), so
 * the runs of the page keep their instances; a buffer created for the first time has none.
 *
 * @param s The store
 * @param p The packet
 * @param page Index of the page
 * @return Whether the buffers were created
 */
bool box_draws_store_reserve(
    box_draw_store_t *s,
    box_render_packet_t *p,
    int32_t page);

/** Add the slots and the bytes per stream of the page buffers of a store.
 *
 * Only the pages alive whose buffers are created count, with the slots their buffers hold.
 *
 * @param s The store
 * @param slots Incremented by the slots of the page buffers
 * @param bytes Incremented by the bytes of the page buffers, per stream
 */
void box_draws_store_memory(
    const box_draw_store_t *s,
    int64_t *slots,
    int64_t bytes[BOX_DRAWS_STREAMS]);

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
 * again, or hash the material of their owners again, when a material component of their owner,
 * or one they inherit, changes.
 *
 * A prefab gets a private slot when it gets its MaterialId, a mesh shape that owns one takes a
 * slot in the extract (box_draws_material_id). A private slot is freed when its owner loses its
 * MaterialId or is deleted, a shared slot with its last owner.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_materials_observers(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Get the slot of the material table an entity renders with.
 *
 * A mesh shape that owns a MaterialId renders with the shared slot of its material, which it
 * takes or creates when it has none or its material changed since, or with a private slot while
 * it is in a DynamicMaterial batch or a material transition runs.
 *
 * Any other entity renders with the MaterialId it inherits, or slot 0, and gives back an own
 * slot it had. A slot past what the binding holds is logged once and renders as slot 0.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param mesh Whether the entity is drawn by the mesh pipeline
 * @param dynamic Whether the entity is in a DynamicMaterial batch, which packs its record every
 * frame
 * @return The slot
 */
uint32_t box_draws_material_id(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    bool mesh,
    bool dynamic);

/** Give back the own slot of an entity that stops being a mesh shape, or its share of a shared
 * slot, not the slot of a prefab.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_material_release(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Pack the private slot an entity owns again in the next send, or hash the material of the
 * owner of a shared slot again in the next box_draws_materials_resolve.
 *
 * @param d The state
 * @param e The entity
 * @return Whether the entity owns a slot
 */
bool box_draws_material_changed(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Check whether an entity owns a slot of the material table, private or shared.
 *
 * @param d The state
 * @param e The entity
 * @return Whether it owns one
 */
bool box_draws_material_owns(
    const box_render_draws_t *d,
    ecs_entity_t e);

/** Give the owner of a slot a private slot for a running material transition, which patches
 * its record by slot, for the extract of this frame.
 *
 * The walk of the active material transitions calls it in every frame the transition runs,
 * before the extract of the batches. A shared slot the owner holds alone turns private in place;
 * else the owner gets a new slot and its sharers keep theirs.
 *
 * The first box_draws_materials_resolve after a frame that did not take the slot returns the
 * owner to a shared slot, unless it is a prefab or in a DynamicMaterial batch.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @return Its private slot, UINT32_MAX when it owns no slot
 */
uint32_t box_draws_material_private(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Bring the owners of shared slots to the slot of their material at the start of the extract.
 *
 * Hashes the material of each owner whose material changed and returns the owners whose
 * material transition ended to a shared slot. An owner whose slot changes writes its shading.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_materials_resolve(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Pack the changed private slots of the material table from their owners and upload the
 * changed slots: merged runs of consecutive slots, or the whole table when its buffer is created.
 *
 * A shared slot uploads the record it was hashed with, a freed slot nothing. While recording
 * shadow regions, the slots whose cutoff or UV transform changed go to shadow_materials.
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
 * siblings by Position z, then OrderedChildren order or entity id, a shape before its glyphs. New
 * entities, hierarchy, z or child order changes make the order again, moved slots only the runs.
 *
 * Making the runs ranks the shapes and glyphs of every tree (box_draw_order_t) along the order
 * into their shading (ids[1]) with box_draws_rank_next. The 2D shader lifts the depth of each by
 * its rank over the coplanar elements of its tree drawn before it, at any angle in perspective.
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

/** Register the shadows of the 2D shapes: the BoxShadowSlot of every BoxShadow.
 *
 * @param world The world
 */
void box_draws_box_shadow_init(
    ecs_world_t *world);

/** Register the observers of the shadows: a set BoxShadow writes the shadow again, a removed one
 * frees its slot, also on a deleted or disabled entity.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_box_shadow_observers(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Bring the shadow of a changed entity into the batch of its shape and write its instance, or
 * free its slot when it draws no shadow: without a BoxShadow, without a drawn Rect or Circle, or
 * with an empty shadow box.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param s The DrawSlot of the entity, its shape already updated
 */
void box_draws_box_shadow_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s);

/** Free the shadow slot of an entity.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_box_shadow_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Point the BoxShadowSlot of an entity at the slot its shadow moves to, when the shadow is the
 * instance that moves.
 *
 * @param world The world
 * @param moved The entity of the instance that moves
 * @param batch Index of the batch
 * @param from The slot the instance was in
 * @param to The slot the instance moves to
 * @return Whether the instance is the shadow of the entity
 */
bool box_draws_box_shadow_moved(
    ecs_world_t *world,
    ecs_entity_t moved,
    int32_t batch,
    int32_t from,
    int32_t to);

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
