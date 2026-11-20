/**
 * @file render/draw.h
 * @brief Instanced batches of the forward pass, shared by the change tracking and the extract.
 *
 * Shapes live in batches keyed by scene, mesh, pipeline, whether their transform and material
 * are dynamic, whether they cast shadows and whether they have a parametric surface, one
 * swap-removed slot per entity and at most the chunk of its store (box_draw_store_t.chunk).
 *
 * A batch owns a run of slots of an instance page (store.c), a buffer per stream; a change
 * uploads only its streams. Only the pages of the parametric surfaces have a surface stream, the
 * others bind a zero record; only frame pages, those of the MappingFrame materials and of the
 * MappingObject materials under a TextureFrame, have a frame stream. A mesh shading is a tint,
 * an alpha and a material slot.
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

/** Store of the instance pages of the plain mesh batches whose material uses MappingFrame: the
 * transform, the shading and the frame stream, without a surface stream (its stride 0), from
 * BOX_RENDER_BUFFER_FIRST_FRAME */
#define BOX_DRAWS_STORE_FRAME (3)

/** Store of the instance pages of the mesh batches with a parametric surface whose material uses
 * MappingFrame: every stream, from BOX_RENDER_BUFFER_FIRST_FRAME_SURFACE */
#define BOX_DRAWS_STORE_FRAME_SURFACE (4)

/** Store of the instance pages of the plain mesh batches whose shapes move with a mover: the
 * transform stream relative to the mover, the shading and the mover stream, from
 * BOX_RENDER_BUFFER_FIRST_MOVER */
#define BOX_DRAWS_STORE_MOVER (5)

/** Store of the mover pages of the mesh batches with a parametric surface */
#define BOX_DRAWS_STORE_MOVER_SURFACE (6)

/** Store of the mover pages of the mesh batches whose material uses MappingFrame */
#define BOX_DRAWS_STORE_MOVER_FRAME (7)

/** Store of the mover pages of the mesh batches with a parametric surface whose material uses
 * MappingFrame */
#define BOX_DRAWS_STORE_MOVER_FRAME_SURFACE (8)

/** Stores of instance pages, one per page layout */
#define BOX_DRAWS_STORES (9)

/** Streams of an instance that a change writes, a bit per stream of the instance pages */
typedef enum box_draw_kind_t {
    BOX_DRAW_TRANSFORM = 1 << BOX_RENDER_STREAM_TRANSFORM, /**< The transform stream */
    BOX_DRAW_SHADING = 1 << BOX_RENDER_STREAM_SHADING,     /**< The shading stream */
    BOX_DRAW_SURFACE = 1 << BOX_RENDER_STREAM_SURFACE,     /**< The surface stream */
    BOX_DRAW_FRAME = 1 << BOX_RENDER_STREAM_FRAME,         /**< The frame stream, which a change
                                                            *   of the transform writes too */
    BOX_DRAW_MOVER = 1 << BOX_RENDER_STREAM_MOVER,         /**< The mover stream, which a change
                                                            *   of the transform or the frame
                                                            *   writes too */
    BOX_DRAW_BOTH = BOX_DRAW_TRANSFORM | BOX_DRAW_SHADING, /**< Transform and shading */
    BOX_DRAW_ALL = BOX_DRAW_BOTH | BOX_DRAW_SURFACE | BOX_DRAW_FRAME, /**< Every stream, with the
                                                            *   mover stream they imply */
    BOX_DRAW_SLOT = 1 << 5, /**< With BOX_DRAW_FADE: the material slot of the shape changed,
                             *   so its shading reads its material again */
    BOX_DRAW_FADE = 1 << 6, /**< To box_draws_mark: a change that may change only the blend
                             *   and the alpha test of the key: an ancestor Opacity or Tint, a
                             *   material slot, a transform relative to a mover; in
                             *   DrawSlot.changed: the walk computed fade and tint for this
                             *   extract */
    BOX_DRAW_KEY = 1 << 7   /**< In DrawSlot.changed: a change other than BOX_DRAW_FADE, which
                             *   may change the key */
} box_draw_kind_t;

/** Layout of the instance pages of a store: one buffer per stream, each with its bytes per slot */
typedef struct box_draw_layout_t {
    int32_t streams;                    /**< Buffers of a page, 1 to BOX_DRAWS_STREAMS */
    uint32_t stride[BOX_DRAWS_STREAMS]; /**< Bytes of one slot in each buffer, 0 for a stream
                                         *   below streams that the pages do not have */
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
    int32_t slice;  /**< Index of the slice record of a sliced 2D shape in the clip buffer
                     *   (box_draws_slice_of), or of the polygon record of a polygon
                     *   (box_draws_polygon_of), 0 for none */
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
    bool transmissive;      /**< A 3D shape that has or inherits Transmission, or Distortion
                             *   with Scene.distortion: blend is
                             *   AlphaBlend, so it draws in the sorted blended pass, cutout and
                             *   shadow are false */
    bool water;             /**< A 3D shape that has or inherits Water in a scene with
                             *   Scene.water: transmissive too, drawn by the extended mesh
                             *   shader without a TextureBlend */
    ecs_entity_t foam;      /**< Water.foam_texture of a water shape, 0 for none */
    uint8_t back_face;      /**< BackFaceMode */
    uint8_t wrap;           /**< TextureWrap of the texture samplers */
    bool dynamic_transform; /**< The shapes have DynamicTransform, owned or inherited, or are
                             *   moved by a transform transition (TransitionTransform), so the
                             *   transforms upload every frame */
    bool dynamic_material;  /**< The shapes or a ChildOf ancestor have DynamicMaterial, so the
                             *   shading uploads every frame */
    bool mover;             /**< 3D shapes that inherit DynamicTransform through
                             *   DynamicTransformSpread, moved by an ancestor that owns it, and
                             *   that no transform transition moves: their transforms are
                             *   relative to that mover and upload when they change, and the
                             *   compose pass places them with the mover every frame. Their
                             *   pages are of a mover store and dynamic_transform is false */
    bool shadow;            /**< Instances cast shadows, false under NoShadow */
    bool fixture;           /**< The shapes or a ChildOf ancestor have LightFixture: they cast
                             *   no shadow into the tile of a light inside their world bounds.
                             *   Only set with shadow */
    bool surface;           /**< The shapes have a parametric surface: the batch has the
                             *   surface stream, its pages are of the surface store. The mesh
                             *   already tells surfaces apart, the bit selects the store */
    bool frame;             /**< The material of the 3D shapes uses MappingFrame, or
                             *   MappingObject under a TextureFrame: the batch has the frame
                             *   stream, its pages are of a frame store. Other materials never
                             *   pay for the frame */
    bool bake;              /**< Static 3D instances without NoBake of a scene with LightBake or
                             *   ReflectionBake, drawn by the captures */
    bool box_shadow;        /**< The shadows of 2D shapes (BoxShadow), drawn with the
                             *   fs_shadow entry point of the 2D shape shader */
    bool slice;             /**< Rects with an ImageSlice, drawn with the fs_slice entry point
                             *   of the 2D shape shader */
    bool polygon;           /**< Polygons (Polygon2D), drawn with the polygon variant of the 2D
                             *   shape shader */
    bool wind;              /**< 3D shapes that have or inherit a WindSway in a scene with
                             *   Scene.wind: their pipelines move the vertices
                             *   (material_wind), other batches keep those without it */
    float range[3];         /**< DrawRange near, far and fade of 3D shapes that have or whose
                             *   ChildOf ancestor has one (box_draws_range), all 0 without: the
                             *   cull pass drops the instances out of range and the pipelines
                             *   dither the fades (draw_range) */
    bool texture_blend;     /**< 3D shapes with a TextureBlend (texture_blend pipeline):
                             *   textures[0] is the owner of the TextureBlend, textures[2] and
                             *   textures[3] are 0, so the key does not grow */
    ecs_entity_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture entities of PbrTextures */
    ecs_entity_t layers[BOX_RENDER_LAYERS]; /**< TextureLayers.layers of a 3D shape of a scene
                             *   with Scene.texture_layers that is not transmissive, all 0
                             *   without: shapes with the same layers share a batch and draw
                             *   with the layers variants of the mesh pipelines. Follows
                             *   textures, which the key compares with it as one block */
    uint32_t atlas;         /**< Texture handle of the glyph atlas page of a glyph batch, or 0 */
    uint32_t features;      /**< BOX_RENDER_FEATURE_* features of the scene of a 3D shape, 0 for
                             *   2D shapes and texts */
    uint32_t planet;        /**< Planet light slot of the nearest PlanetAtmosphere at or above a
                             *   3D shape of a scene with Scene.planet_light, 0 for none */
    ecs_entity_t atmosphere; /**< Nearest PlanetAtmosphere at or above a blended 3D shape, 0 for
                             *   none: its instances that are layers around the ground of the
                             *   planet draw before the atmosphere shells (box_draws_layer) */
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
 * shape with the AlphaBlend blend mode, no textures and box_shadow set.
 *
 * A removed BoxShadow frees the slot and leaves the component without a batch; the changes of
 * the entity then skip it, as they check for BoxShadow. */
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
    const TextureMapping *mapping; /**< Source of the texture coordinates of a 3D shape, ignored
                                 *   by 2D shapes */
    const TextureScroll *scroll; /**< Endless UV scrolling of a 3D shape, ignored by 2D shapes */
    double time;                /**< Total render time the scroll offsets are computed at */
    double origin[3];           /**< Absolute position of the local origin (WorldOrigin) the
                                 *   world space patterns are packed for, 0 without one */
    const Transmission *transmission; /**< Light passing through a 3D shape */
    const ClearCoat *clear_coat; /**< Dielectric layer over a 3D shape, ignored by 2D shapes */
    const SpecularAnisotropy *anisotropy; /**< Stretched highlight of a 3D shape, ignored by 2D
                                 *   shapes */
    const Sheen *sheen;         /**< Grazing rim of a 3D shape, ignored by 2D shapes */
    const DiffuseTransmission *diffuse_transmission; /**< Back lit glow of a 3D shape, ignored
                                 *   by 2D shapes */
    const Fresnel *fresnel;     /**< Artistic rim of a 3D shape, ignored by 2D shapes */
    const Distortion *distortion; /**< Shimmer of the scene behind a 3D shape, ignored by 2D
                                 *   shapes and by Water */
    const Water *water;         /**< Water surface of a 3D shape, ignored by 2D shapes */
    const WaterWaves *waves;    /**< Travelling waves of the Water, NULL without Water */
    const WaterFlow *flow;      /**< Velocity the waves, caustics and, with its textures, the
                                 *   normal map and foam texture move with, NULL without Water */
    uint32_t water_waves;       /**< Set of the waves in the wave table, 0 for none */
    const WaterCaustics *caustics; /**< Caustics of the Water, NULL without Water */
    const WaterSpectrum *spectrum; /**< Spectrum waves of the Water, whose slopes its fragments
                                 *   take from the view, NULL without Water */
    float caustics_frame[2];    /**< Offset of the water frame of the caustics in cells, wrapped
                                 *   (box_draws_water_caustics_frame), 0 for still water */
    const WindSway *sway;       /**< Wind sway of a 3D shape, ignored by 2D shapes */
    box_render_wind_t wind;     /**< Wind of the frame the sway is packed with, read with sway */
    const VertexReveal *reveal; /**< Hides the parts of a 3D mesh by its per vertex data, ignored
                                 *   by 2D shapes */
    const Tint *tint;           /**< Tint of the shape itself, applied after the inherited */
    const box_draw_tint_t *inherited; /**< Composed tints of the ancestors, NULL for none */
    const Shape2D *shape2d;     /**< Resolved 2D shape, NULL for a 3D shape */
    const Border *border;       /**< Border shape2d was resolved with, NULL for none, read by
                                 *   the target of a transition */
    bool border_radius;         /**< The corner radius of shape2d is the Border.radius: the
                                 *   shape is a Rect */
    float fade;                 /**< Product of the Opacity of the ancestors */
    int32_t clip;               /**< Clip index of a 2D shape or glyph, 0 for none */
    int32_t slice;              /**< Slice record index of a sliced 2D shape, polygon record
                                 *   index of a polygon, 0 for none */
    uint32_t rank;              /**< Rank of a 2D shape or glyph in the draw order of its tree */
} box_draw_material_t;

/** What the record a material transition patches is */
typedef enum box_draw_transition_kind_t {
    BOX_DRAW_TRANSITION_TABLE = 0,  /**< A box_render_material_t of the material table */
    BOX_DRAW_TRANSITION_SHAPE,      /**< The box_render_shading_t of a 2D shape */
    BOX_DRAW_TRANSITION_GLYPH,      /**< The box_render_shading_t of a glyph */
    BOX_DRAW_TRANSITION_MESH,       /**< The box_render_mesh_shading_t of a mesh shape: its tint
                                     *   and alpha */
    BOX_DRAW_TRANSITION_BOX_SHADOW  /**< The box_render_shading_t of the shadow of a 2D shape:
                                     *   its color and cover alpha (material.w), or all of it
                                     *   and its transform for a box shadow record */
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
    uint32_t present;       /**< 1 << box_render_transition_component_t per component the
                             *   material has */
    uint8_t kind;           /**< box_draw_transition_kind_t */
    bool tinted;            /**< inherited holds the tints of the ancestors */
    Rgb color;              /**< Rgb */
    AlphaCutoff cutoff;     /**< AlphaCutoff */
    PbrMaterial pbr;        /**< PbrMaterial */
    Emissive emissive;      /**< Emissive */
    TextureTransform uv;    /**< TextureTransform */
    TextureMapping mapping; /**< TextureMapping, which has no transition: the record keeps it */
    bool mapped;            /**< The material has a TextureMapping */
    TextureScroll scroll;   /**< TextureScroll, which has no transition: the record keeps it */
    bool scrolled;          /**< The material has a TextureScroll */
    double time;            /**< Total render time the scroll offsets are computed at */
    double origin[3];       /**< Absolute position of the local origin the world space
                             *   patterns are packed for */
    Transmission transmission; /**< Transmission */
    ClearCoat clear_coat;   /**< ClearCoat */
    SpecularAnisotropy anisotropy; /**< SpecularAnisotropy */
    Sheen sheen;            /**< Sheen */
    DiffuseTransmission diffuse_transmission; /**< DiffuseTransmission */
    Fresnel fresnel;        /**< Fresnel */
    Distortion distortion;  /**< Distortion */
    Water water;            /**< Water, which has no transition: the record keeps it */
    bool watered;           /**< The material has a Water */
    uint32_t water_waves;   /**< Set of the WaterWaves of the Water in the wave table, which
                             *   has no transition: the record keeps it */
    WaterCaustics caustics; /**< WaterCaustics, which has no transition: the record keeps it */
    bool flowed;            /**< The Water has a WaterFlow */
    WaterFlow flow;         /**< Its WaterFlow, read with flowed */
    bool causticked;        /**< The Water has a WaterCaustics */
    float caustics_frame[2]; /**< Offset of the water frame of the caustics in cells */
    WindSway sway;          /**< WindSway, which has no transition: the record keeps it */
    bool swayed;            /**< The material has a WindSway */
    VertexReveal reveal;    /**< VertexReveal, which has no transition: the record keeps it */
    bool revealed;          /**< The material has a VertexReveal */
    box_render_wind_t wind; /**< Wind of the frame the sway was packed with */
    Opacity opacity;        /**< Opacity of the shape itself */
    Tint tint;              /**< Tint of the shape itself */
    box_draw_tint_t inherited; /**< Tints of the ancestors, when tinted */
    float fade;             /**< Product of the Opacity of the ancestors */
    Shape2D shape2d;        /**< The 2D shape of a shape record, of a box shadow record, with
                             *   its target Border */
    Border border;          /**< Border of the 2D shape of a shape or box shadow record, which
                             *   the encode applies to shape2d (box_shape2d_border) */
    bool border_radius;     /**< The corner radius of shape2d is the Border.radius: a Rect */
    uint32_t fade_node;     /**< Nearest node at or above the entity with an active Tint or
                             *   Opacity transition, 0 for none: the resolver replaces
                             *   inherited and fade by the values it renders */
    bool fade_self;         /**< The entity is that node: it inherits what the node renders
                             *   above it, and its own Tint and Opacity are values of the node */
    box_draw_tint_t fade_rel; /**< Composed tints of the entities strictly between the node and
                             *   the entity, applied after those of the node */
    float fade_alpha;       /**< Product of the opacities of those entities */
    float opacity_scale;    /**< Factor of the own Opacity, the alpha of the span of a glyph */
    rgba shadow;            /**< Display encoded color of a BoxShadow, of a shadow record */
    float shadow_cover;     /**< The alpha the shape of a shadow record covers it with at an
                             *   opacity of 1, 0 for a blend mode that covers nothing */
    uint8_t shadow_blend;   /**< Blend mode of the batch of the shape of a shadow record, from
                             *   which shadow_cover is computed again for a rendered Border */
} box_draw_transition_material_t;

/** What a box shadow record points at in the packet data: the target of the shadow of a 2D
 * shape whose BoxShadow transitions, which the resolver resolves again with the BoxShadow its
 * node renders (box_draws_box_shadow_patch).
 *
 * The shadow box shapes both the instance matrix and most words of the shading, so the record
 * writes the whole transform and shading of the shadow slot. */
typedef struct box_draw_transition_box_shadow_t {
    box_draw_transition_material_t material; /**< Kind BOX_DRAW_TRANSITION_BOX_SHADOW: the
                             *   tints, opacity and fade of the shape with their nodes, the cover
                             *   alpha of the shape and in shape2d the resolved Rect */
    BoxShadow shadow;       /**< Target BoxShadow */
    float mesh_scale[2];    /**< ShapeMesh.scale of the shape, x and y, when meshed */
    bool meshed;            /**< The shape has a ShapeMesh */
    float world[4][4];      /**< Column major: target world matrix of the shape */
    float rel[4][4];        /**< Column major: the target chain from the node that moves the
                             *   shape (the parent of the record) down to the shape, read when
                             *   that node has a pose in the packet */
    uint32_t transform;     /**< Packet data offset of the box_render_transform_t of the shadow */
} box_draw_transition_box_shadow_t;

/** A matrix known to lie, element by element, between two matrices: the world matrices a node
 * of a transform transition can render with in a frame */
typedef struct box_draw_transition_range_t {
    mat4 lo;                    /**< Minimum of each element */
    mat4 hi;                    /**< Maximum of each element */
} box_draw_transition_range_t;

/** A blended instance of a view, ordered back to front by its view depth */
typedef struct box_draw_sorted_t {
    float depth;            /**< View space z of the instance origin, smaller is farther */
    bool atmosphere;        /**< The instance is a layer around the ground of a planet
                             *   (box_draws_layer) */
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

/** Texture frame of the frame record of a slot of a frame batch */
typedef struct box_draw_frame_src_t {
    ecs_entity_t frame;     /**< Nearest TextureFrame at or above the shape, 0 for none */
    uint8_t mode;           /**< TextureMappingMode of the shape */
    bool live;              /**< The record of a slot of a mover batch is written every frame: a
                             *   MappingFrame shape whose frame does not move with its mover */
} box_draw_frame_src_t;

/** Bounds of the slots of a mover batch that move with one mover, in the frame of the mover */
typedef struct box_draw_group_t {
    uint32_t mover;         /**< Index of the mover */
    float min[3];           /**< Minimum corner of the boxes of the slots */
    float max[3];           /**< Maximum corner of the boxes of the slots */
    float extent[3];        /**< Largest half size of the box of a slot on each axis */
} box_draw_group_t;

/** A mover: an entity that owns DynamicTransform, which places the shapes of the mover batches
 * that move with it */
typedef struct box_draw_mover_t {
    ecs_entity_t entity;    /**< The mover, 0 for a free index */
    int32_t refs;           /**< Slots that move with it */
    int32_t next;           /**< Next free index, while free */
    box_render_transform_t pose; /**< Its world transform when the movers were last placed */
    box_render_transform_t was; /**< Its world transform before that */
    float scale;            /**< Its uniform world scale when its slots were last written */
    bool moved;             /**< pose differs from was */
} box_draw_mover_t;

/** The world transform of a texture frame and its inverse, computed once per extract */
typedef struct box_draw_frame_pose_t {
    ecs_entity_t frame;     /**< The texture frame */
    int64_t tick;           /**< Extract it was computed in */
    bool valid;             /**< The frame has a world transform that inverts */
    mat4 inv;               /**< Inverse of its world transform */
    float scale;            /**< Its uniform world scale */
} box_draw_frame_pose_t;

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
    bool world_dirty;       /**< A slot, its transform or its surface changed since the world
                             *   bounds were computed */
    bool world_valid;       /**< world_min, world_max and world_radius hold the world bounds */
    float world_local[6];   /**< Local center and extent the world bounds were computed with */
    float world_min[3];     /**< Minimum corner of the world bounds of its slots, padded */
    float world_max[3];     /**< Maximum corner of the world bounds of its slots, padded */
    float world_radius;     /**< Largest radius of the world box of a slot, padded */
    float world_wind;       /**< wind_reach the world bounds were grown by */
    float wind_reach;       /**< Largest world distance the WindSway of a slot of a wind batch
                             *   moves a vertex in the wind of this extract (box_draws_wind) */
    uint32_t lods[BOX_LOD_LEVELS_MAX]; /**< Mesh of each level of detail the views of the packet
                             *   use, see box_render_batch_t.lods */
    int64_t record_frame;   /**< Frame of the packet its batch record was last sent in */
    int32_t record;         /**< Index of that record in the batches of the packet */
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
    ecs_vec_t frames;       /**< box_render_transform_t per slot of a frame batch, its transform
                             *   relative to its texture frame (box_draws_frame_of_shape), empty
                             *   for any other batch */
    ecs_vec_t frame_srcs;   /**< box_draw_frame_src_t per slot of a frame batch, the texture frame
                             *   its frame record was written from */
    int32_t live_frames;    /**< Slots of a mover frame batch whose frame record is written every
                             *   frame (box_draw_frame_src_t.live) */
    ecs_vec_t movers;       /**< uint32_t per slot of a mover batch, its record of the mover
                             *   stream; its transform in transforms is relative to the mover */
    ecs_vec_t groups;       /**< box_draw_group_t, the bounds of the slots of a mover batch in
                             *   the frame of each of its movers */
    bool groups_dirty;      /**< A slot, its transform or its surface changed since the groups
                             *   were made */
    ecs_vec_t changed[BOX_DRAWS_STREAMS]; /**< int32_t per stream, slots whose data of a stream
                             *   that does not upload every frame the GPU does not have */
} box_draw_batch_t;

/** The slice record of a sliced 2D shape (box_draws_slice_of): two consecutive records of the
 * clip buffer that hold its box_render_slice_t, which are 0 in the entities of the clips.
 *
 * The record is computed again in a send when the shape changed since, or when the Texture of
 * its image got another size, such as when it finished loading.
 */
typedef struct box_draw_slice_t {
    ecs_entity_t entity;    /**< The shape, 0 for a free entry */
    ecs_entity_t texture;   /**< Texture entity of its Image when the record was computed */
    int32_t index;          /**< First of the two clip indices of its record */
    int32_t width;          /**< Texture size the record was computed with, 0 without one */
    int32_t height;         /**< Texture height the record was computed with */
    bool dirty;             /**< The shape changed since the record was computed */
} box_draw_slice_t;

/** The polygon record of a Polygon2D (box_draws_polygon_of): consecutive records of the clip
 * buffer that hold its box_render_polygon_t and its edges, which are 0 in the entities of the
 * clips. The record is computed again in a send when the shape changed since.
 */
typedef struct box_draw_polygon_t {
    ecs_entity_t entity;    /**< The shape, 0 for a free entry */
    int32_t index;          /**< First clip index of its record */
    int32_t count;          /**< Clip records the entry holds */
    bool dirty;             /**< The shape changed since the record was computed */
} box_draw_polygon_t;

/** A run of free clip records of the polygon records, reused by a polygon that fits it */
typedef struct box_draw_polygon_run_t {
    int32_t index;          /**< First clip index */
    int32_t count;          /**< Clip records */
} box_draw_polygon_run_t;

/** The ClipChildren elements the 2D draws clip to, by clip index.
 *
 * An element gets an index when a 2D draw below it first asks for it and keeps it until it loses
 * ClipChildren, so the instances that hold the index stay valid; freed indices are reused. Index
 * 0 is no clip. The areas are computed every frame and uploaded when one changed.
 */
typedef struct box_draw_clips_t {
    ecs_map_t index;        /**< Element to its clip index */
    ecs_map_t floats;       /**< Float with LayoutFloat.hide_clipped to its clip index, whose
                             *   area is empty while its anchor is clipped out; freed in a send
                             *   once the float is gone or no longer hides */
    ecs_vec_t entities;     /**< ecs_entity_t per clip index, 0 for a free index */
    ecs_vec_t records;      /**< box_render_clip_t per clip index, as the backend has them */
    ecs_vec_t free;         /**< int32_t, clip indices to reuse */
    ecs_map_t slices;       /**< Sliced 2D shape to its entry in slice_entries */
    ecs_vec_t slice_entries; /**< box_draw_slice_t, the slice records */
    ecs_vec_t slice_free;   /**< int32_t, entries of slice_entries to reuse with their records */
    ecs_map_t polygons;     /**< Polygon to its entry in polygon_entries */
    ecs_vec_t polygon_entries; /**< box_draw_polygon_t, the polygon records */
    ecs_vec_t polygon_free; /**< box_draw_polygon_run_t, records of freed polygon entries */
    ecs_vec_t polygon_edges; /**< vec4, scratch edges of the polygon being computed */
    uint32_t capacity;      /**< Clips the clip buffer holds, 0 before it is created */
    int32_t tagged;         /**< Entities with ClipChildren, so a world without any skips the
                             *   walk up the hierarchy */
    int32_t changed_first;  /**< First record of the polygon records that changed since the
                             *   last upload, uploaded alone when no other record changed */
    int32_t changed_end;    /**< End of the changed polygon records, changed_first for none */
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

/** The phases of the WaterWaves of an owner of the material table, a set of the wave table */
typedef struct box_draw_water_waves_state_t {
    ecs_entity_t owner;     /**< Owner of the slot whose Water has the waves, 0 for a free set */
    double time;            /**< Total render time the phases were integrated to */
    double phases[4];       /**< Phase of each wave, wrapped to 0..2 pi
                             *   (box_draws_water_waves_step) */
} box_draw_water_waves_state_t;

/** The water frame of the WaterCaustics of an owner of the material table that has a
 * WaterFlow */
typedef struct box_draw_water_caustics_state_t {
    double time;            /**< Total render time the frame was integrated to */
    double frame[2];        /**< Offset of the water frame in cells, the integral of the WaterFlow
                             *   velocity over the scale, wrapped to 0..BOX_RENDER_CAUSTICS_PERIOD
                             *   (box_draws_water_caustics_step) */
} box_draw_water_caustics_state_t;

/** The wave table (BOX_RENDER_BUFFER_WATER_WAVES) as the main thread integrates and packs it */
typedef struct box_draw_water_waves_t {
    box_draw_water_waves_state_t states[BOX_RENDER_WATER_WAVE_SETS]; /**< By set, set 0
                             *   unused */
    box_render_water_waves_t table[BOX_RENDER_WATER_WAVE_SETS]; /**< By set, as uploaded */
    uint32_t used;          /**< Sets up to the highest one ever handed out plus one, which the
                             *   send uploads */
    bool created;           /**< The table buffer was created */
    bool dirty;             /**< A set changed since the last upload */
    bool full;              /**< A Water found no free set, which was logged */
} box_draw_water_waves_t;

/** The material table, slot 0 the default material, as the main thread packed it and what the
 * GPU still needs of it.
 *
 * Mesh shapes that own material components share a slot per distinct record, with reference
 * counts. Prefabs, shapes of DynamicMaterial batches, with a TextureScroll (packed every send)
 * or a running material transition own a private slot, packed from its owner, patched by slot.
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
    ecs_vec_t scrolling;    /**< uint32_t, private slots whose owner has a TextureScroll, packed
                             *   again every send */
    double time;            /**< Total render time of the packet the table packs for, which
                             *   moves the TextureScroll offsets */
    int32_t stamp;          /**< Extracts the table resolved, which dates the transitions */
    uint32_t capacity;      /**< Records the table buffer holds, 0 before it is created */
    int32_t alive;          /**< Slots alive, slot 0 included */
    bool overflow;          /**< An id past what the binding holds was logged */
    int64_t uploaded_frame; /**< Frame of the packet of the last send that uploaded */
    ecs_vec_t uploaded;     /**< box_draw_material_upload_t of that send */
    float transmission_roughness; /**< Largest roughness of the transmissive materials, 1 for a
                             *   diffuse blur, 0 without any */
    bool features_valid;    /**< transmission_roughness was computed from the records */
    box_draw_water_waves_t *waves; /**< The wave table, NULL until a Water has WaterWaves */
    ecs_map_t caustics;     /**< Owner whose Water has WaterCaustics and a WaterFlow to its
                             *   box_draw_water_caustics_state_t, allocated */
    double origin[3];       /**< Absolute position of the local origin (WorldOrigin) the
                             *   records were packed for; a change packs every owner again */
} box_draw_materials_t;

/** The root of a changed subtree and the streams the change writes for each of its entities */
typedef struct box_draw_moved_t {
    ecs_entity_t root;      /**< Root of the subtree */
    uint8_t kinds;          /**< box_draw_kind_t */
} box_draw_moved_t;

/** An ancestor with an Opacity or a Tint, in a chain from the nearest ancestor up, which a
 * subtree walk carries down so a shape composes the fade of its ancestors without an up search */
typedef struct box_draw_fade_link_t {
    Opacity opacity;        /**< Its Opacity, when has_opacity */
    Tint tint;              /**< Its Tint, when has_tint */
    bool has_opacity;       /**< It has an Opacity */
    bool has_tint;          /**< It has a Tint */
    int32_t next;           /**< Index of the next ancestor of the chain, -1 for none */
} box_draw_fade_link_t;

/** An entity of a subtree walk with the chain of its ancestors */
typedef struct box_draw_walk_t {
    ecs_entity_t e;         /**< The entity */
    int32_t fade;           /**< Index of the first box_draw_fade_link_t of its ancestors, -1 for
                             *   none */
} box_draw_walk_t;

/** Main thread state of the instanced batches */
typedef struct box_render_draws_t {
    ecs_query_t *dynamic_transform; /**< Shapes with DynamicTransform */
    ecs_query_t *dynamic_material;  /**< Shapes with DynamicMaterial, self or up, and no owned
                                     *   DynamicTransform */
    ecs_query_t *dynamic_texts;     /**< Texts with DynamicTransform */
    ecs_query_t *dynamic_text_materials; /**< Texts with DynamicMaterial, self or up */
    ecs_query_t *extended;          /**< Shapes with Sheen, ClearCoat, SpecularAnisotropy,
                                     *   DiffuseTransmission or Fresnel, self or up IsA, which
                                     *   draw with the extended mesh shader */
    uint32_t text_mesh;         /**< Mesh handle of the glyph quad */
    ecs_vec_t batches;          /**< box_draw_batch_t by index */
    ecs_vec_t free_batches;     /**< int32_t, indices of batches not in use */
    ecs_map_t lookup;           /**< Key hash to the index of the first batch plus one */
    ecs_vec_t dirty;            /**< ecs_entity_t, entities whose instance may have changed */
    ecs_vec_t moved;            /**< box_draw_moved_t, roots of changed subtrees */
    ecs_vec_t walk;             /**< box_draw_walk_t, stack of a subtree walk */
    ecs_vec_t fade_links;       /**< box_draw_fade_link_t, ancestors of the subtree walks */
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
    ecs_map_t overlays;         /**< Clipped overlays (LayoutFloat.clip) to 1 and floats with
                                 *   hide_clipped to 2, so a set LayoutFloat changes the order or
                                 *   the clip indices of the subtree only when these changed */
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
    box_render_wind_t wind;     /**< Wind of this extract */
    bool wind_blew;             /**< The wind blew in the last extract, so the swaying casters
                                 *   render their shadows once more where they came to rest */
    ecs_vec_t swaying;          /**< ecs_entity_t, the scenes (0 for the default one) with a
                                 *   ready caster batch with a WindSway while the wind blows in
                                 *   this extract, whose shadow cascades render every frame */
    int32_t box_shadows;        /**< BoxShadowSlot values with a batch, so the paths every 2D
                                 *   shape takes (the runs, the transitions) skip the lookup of
                                 *   the slot while no shadow is drawn */
    ecs_vec_t planets;          /**< ecs_entity_t by planet light slot minus one: the entities
                                 *   with a PlanetAtmosphere whose meshes took the slot, 0 for a
                                 *   free one */
    ecs_vec_t movers;           /**< box_draw_mover_t by mover index, index 0 the identity */
    ecs_map_t mover_of;         /**< Mover entity to its index */
    int32_t free_mover;         /**< First free mover index, -1 for none */
    uint32_t movers_size;       /**< Records the mover table on the GPU holds, 0 before it is
                                 *   created */
    bool movers_dirty;          /**< A mover moved or joined since the table was last sent */
    ecs_map_t locals;           /**< Entity under a mover to the hash of the local transform
                                 *   its subtree was last written with */
    ecs_map_t frame_poses;      /**< Texture frame to its index in frame_cache plus one */
    ecs_vec_t frame_cache;      /**< box_draw_frame_pose_t, the frames of the live records */
    int64_t tick;               /**< Extracts run, which tells the frame poses of this one apart */
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

/** Put the planet light slot of an instance above the slot of the material table it renders
 * with, as box_render_mesh_shading_t.material holds them.
 *
 * @param material Slot of the material table
 * @param planet Planet light slot, 0 for none
 * @return The material word of the shading
 */
static inline uint32_t box_draws_planet_material(
    uint32_t material,
    uint32_t planet)
{
    return (material & BOX_RENDER_MESH_MATERIAL_MASK) |
        (planet << BOX_RENDER_MESH_MATERIAL_BITS);
}

/** Get the planet light slot of a PlanetAtmosphere entity, taking the lowest free one when it
 * has none.
 *
 * @param d The state
 * @param planet The entity with the PlanetAtmosphere
 * @return The slot, 0 when every slot below BOX_RENDER_PLANET_LIGHT_MAX is taken
 */
uint32_t box_draws_planet_slot(
    box_render_draws_t *d,
    ecs_entity_t planet);

/** Find the planet light slot of a PlanetAtmosphere entity.
 *
 * @param d The state
 * @param planet The entity
 * @return The slot, 0 when it has none
 */
uint32_t box_draws_planet_find(
    const box_render_draws_t *d,
    ecs_entity_t planet);

/** Find the nearest entity with a PlanetAtmosphere at or above an entity along ChildOf.
 *
 * @param world The world
 * @param e The entity
 * @return The entity with the PlanetAtmosphere, 0 for none or without box.atmosphere
 */
ecs_entity_t box_draws_planet_of(
    const ecs_world_t *world,
    ecs_entity_t e);

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

/** Record the world bounds a caster slot of a batch has now, as box_draws_shadow_region with
 * the world transform of the slot (box_draws_slot_world).
 *
 * @param world The world, whose mesh cache has the mesh of the batch
 * @param d The state
 * @param b The batch
 * @param slot The slot
 */
void box_draws_shadow_slot(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot);

/** Make the bounds of the slots of a mover batch in the frame of each of its movers again,
 * when a slot changed since.
 *
 * @param d The state
 * @param b The mover batch
 * @param cache The mesh cache
 */
void box_draws_groups(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    box_mesh_cache_t *cache);

/** Record the regions of the caster slots of the mover batches whose mover moved in this
 * extract, where the movers were and are, from the bounds of their groups.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_shadow_movers(
    const ecs_world_t *world,
    box_render_draws_t *d);

/** Follow the Wind of this extract in the batches with a WindSway (box_draw_key_t.wind):
 * resolve the wind, compute the reach of every ready wind batch (with pivots over the diameter of
 * the world box of a slot, which bounds the distance of a vertex from a pivot of its branch, as
 * last computed without the reach), record the regions of its
 * casters while the wind blows so the shadow tiles they reach render again, and list the scenes
 * with swaying casters (swaying). Batches without one are left as they are.
 *
 * @param world The world
 * @param d The state
 * @param cache The mesh cache
 */
void box_draws_wind(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_mesh_cache_t *cache);

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

/** Get the streams the instance pages of a batch have: the transform and the shading, the
 * surface stream for a mesh batch with a parametric surface and the frame stream for a batch of
 * a MappingFrame material.
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

/** Get the transform of a slot relative to its texture frame.
 *
 * @param b The batch
 * @param slot The slot
 * @return The relative transform, NULL for a batch that is not a frame batch
 */
box_render_transform_t* box_draws_slot_frame(
    box_draw_batch_t *b,
    int32_t slot);

/** Write the transform of an instance relative to its texture frame: the inverse of the world
 * transform of the frame, its scale included, times the model matrix of the instance. Positions
 * are in the local units of the frame, so a frame scaled by 10 shows its textures 10x larger;
 * without a frame, or with a flat one, it is the model matrix.
 *
 * @param out The relative transform
 * @param model The model matrix of the instance (box_draws_transform)
 * @param frame World transform of the frame, NULL for none
 */
void box_draws_frame(
    box_render_transform_t *out,
    const box_render_transform_t *model,
    const WorldTransform *frame);

/** Write the frame record of a MappingObject instance under a texture frame: the model matrix
 * with its axes divided by the uniform world scale of the frame (box_transform_world_scale), so
 * the lengths of its columns are the scale of the instance relative to the frame.
 *
 * @param out The record
 * @param model The model matrix of the instance (box_draws_transform)
 * @param frame World transform of the frame, NULL for none
 */
void box_draws_frame_scaled(
    box_render_transform_t *out,
    const box_render_transform_t *model,
    const WorldTransform *frame);

/** Check whether a 3D shape is transmissive by a Distortion its scene enables
 * (Scene.distortion): it has or inherits one, or fades out a removed one with on_remove, which
 * keeps it transmissive until the fade ends.
 *
 * @param world The world
 * @param e The entity
 * @param features The scene features of its key
 * @return Whether it draws over the snapshot for its Distortion
 */
bool box_draws_distorted(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint32_t features);

/** Check whether a 3D shape draws with the extended mesh shader: its key is transmissive or
 * water, it has a PlanarReflection with Scene.planar_reflections or a material feature its scene
 * enables. Alpha tested, blended and TextureBlend shapes without those use the mesh shader.
 *
 * The query of the extended materials tells whether it has Sheen, ClearCoat, SpecularAnisotropy,
 * DiffuseTransmission or Fresnel; only a scene with some of those off checks which.
 *
 * A shape that fades out a removed one of those its scene enables, with on_remove, keeps the
 * extended mesh shader until the fade ends, so the fade renders in the same batch.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param key Its key, with every field but the pipeline set
 * @return Whether it draws with BOX_RENDER_SHADER_MESH_EXTENDED
 */
bool box_draws_extended(
    const ecs_world_t *world,
    const box_render_draws_t *d,
    ecs_entity_t e,
    const box_draw_key_t *key);

/** Look up the texture frame and the mapping mode of a shape, which its frame record is written
 * from until the shape or its ancestors change.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the frame and the mode, live false
 */
void box_draws_frame_src(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_frame_src_t *out);

/** Get the world transform of a texture frame and its inverse, computed once per extract.
 *
 * @param world The world
 * @param d The state
 * @param frame The texture frame
 * @return The pose, valid false when the frame has no world transform that inverts
 */
const box_draw_frame_pose_t* box_draws_frame_pose(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t frame);

/** Write the frame record of a shape from its texture frame: box_draws_frame_scaled for a
 * MappingObject material, else box_draws_frame.
 *
 * @param world The world
 * @param d The state
 * @param src The texture frame and the mapping mode of the shape
 * @param out The record
 * @param model The model matrix of the instance (box_draws_transform)
 */
void box_draws_frame_record(
    const ecs_world_t *world,
    box_render_draws_t *d,
    const box_draw_frame_src_t *src,
    box_render_transform_t *out,
    const box_render_transform_t *model);

/** Find the mover of a shape: the nearest of its ancestors that owns DynamicTransform, or has
 * it without inheriting it from DynamicTransformSpread, as an instance of a dynamic prefab does.
 *
 * @param world The world
 * @param e The shape
 * @return The mover, 0 when no ancestor is one
 */
ecs_entity_t box_draws_mover_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Find the mover of an entity, the entity itself when it is one (box_draws_mover_of).
 *
 * @param world The world
 * @param e The entity
 * @return The mover, 0 when neither the entity nor an ancestor is one
 */
ecs_entity_t box_draws_mover_at(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Write the frame record of a slot of a frame batch from the texture frame of its shape.
 *
 * A mover batch slot whose record moves with its mover (MappingObject, or MappingFrame without a
 * frame that inverts) keeps it relative to the mover, placed with BOX_RENDER_MOVER_FRAME.
 *
 * A MappingFrame record is fixed while its frame moves with the same mover, and is written every
 * frame otherwise (box_draw_frame_src_t.live).
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param b The batch
 * @param slot The slot
 * @param model The model matrix of the shape (box_draws_transform)
 * @param lookup Look the texture frame and the mapping mode up again, for a change that may
 * change the key; a transform keeps them
 */
void box_draws_frame_write(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    box_draw_batch_t *b,
    int32_t slot,
    const box_render_transform_t *model,
    bool lookup);

/** Follow the local transforms under movers, which move shapes in the frame of their mover: a
 * Position, Rotation or Scale of a dynamic entity, the WorldTransform the application writes for
 * a dynamic ManualTransform entity, and the movers that come and go.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_mover_observers(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Write the frame records of the slots of the mover batches that are written every frame:
 * MappingFrame shapes whose texture frame does not move with their mover.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_live_frames(
    const ecs_world_t *world,
    box_render_draws_t *d);

/** Place the slots of every mover page with the movers: one compose per page that holds slots,
 * after the mover table, sent when a mover moved or the table changed.
 *
 * @param p The packet
 * @param d The state
 */
void box_draws_composes(
    box_render_packet_t *p,
    box_render_draws_t *d);

/** Check whether the shapes of a table of the dynamic transform query move with a mover, so
 * their transforms upload only when they change: 3D shapes that inherit DynamicTransform through
 * DynamicTransformSpread, that no transform transition moves and that own no PlanarReflection,
 * whose instance the reflection passes recognize by its transform record.
 *
 * @param world The world
 * @param table The table
 * @param flat The shapes are 2D shapes
 * @param spread The pair of DynamicTransformSpread
 * @return Whether the shapes of the table are in mover batches
 */
bool box_draws_mover_table(
    const ecs_world_t *world,
    const ecs_table_t *table,
    bool flat,
    ecs_id_t spread);

/** Create the mover table of the state.
 *
 * @param d The state
 */
void box_draws_movers_init(
    box_render_draws_t *d);

/** Free the mover table of the state.
 *
 * @param d The state
 */
void box_draws_movers_fini(
    box_render_draws_t *d);

/** Forget the mover table the backend has, after the backend changed.
 *
 * @param d The state
 */
void box_draws_movers_reset(
    box_render_draws_t *d);

/** Get the index of a mover, adding it to the table, and count a slot that moves with it.
 *
 * @param world The world
 * @param d The state
 * @param mover The mover, 0 for the identity
 * @return The index, 0 for the identity
 */
uint32_t box_draws_mover_ref(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t mover);

/** Uncount a slot that moved with a mover, taking the mover out of the table with its last slot.
 *
 * @param d The state
 * @param index The index of the mover, 0 for the identity
 */
void box_draws_mover_unref(
    box_render_draws_t *d,
    uint32_t index);

/** Get a mover of the table.
 *
 * @param d The state
 * @param index The index of the mover, 0 for the identity
 * @return The mover
 */
box_draw_mover_t* box_draws_mover(
    const box_render_draws_t *d,
    uint32_t index);

/** Place every mover of the table at its world transform. A mover whose uniform world scale
 * changed writes its subtree again, whose frame records divide by the scale of their frame.
 *
 * @param world The world
 * @param d The state
 * @param p The packet
 */
void box_draws_movers_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p);

/** Compose two transforms: the transform of a mover times a transform relative to it.
 *
 * @param out The world transform, not one of the inputs
 * @param pose The transform of the mover
 * @param local The transform relative to the mover
 */
void box_draws_compose(
    box_render_transform_t *out,
    const box_render_transform_t *pose,
    const box_render_transform_t *local);

/** Write the transform of a shape relative to its mover: the inverse of the world transform of
 * the mover times the model matrix of the shape, in double precision.
 *
 * @param out The transform relative to the mover
 * @param mover World transform of the mover, NULL for the identity
 * @param model The model matrix of the shape (box_draws_transform)
 */
void box_draws_relative(
    box_render_transform_t *out,
    const WorldTransform *mover,
    const box_render_transform_t *model);

/** Get the world transform of a slot: its transform, or for a mover batch its transform
 * composed with its mover.
 *
 * @param d The state
 * @param b The batch
 * @param slot The slot
 * @param tmp Storage for a composed transform
 * @return The world transform, the slot transform or tmp
 */
const box_render_transform_t* box_draws_slot_world(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot,
    box_render_transform_t *tmp);

/** Get the record of the mover stream of a slot of a mover batch.
 *
 * @param b The batch
 * @param slot The slot
 * @return The record, NULL for a batch that is not a mover batch
 */
uint32_t* box_draws_slot_mover(
    box_draw_batch_t *b,
    int32_t slot);

/** Get the texture frame of the frame record of a slot of a frame batch.
 *
 * @param b The batch
 * @param slot The slot
 * @return The frame source, NULL for a batch that is not a frame batch
 */
box_draw_frame_src_t* box_draws_slot_frame_src(
    box_draw_batch_t *b,
    int32_t slot);

/** Write the transform of a shape: the rows of its model matrix with the instance scale.
 *
 * @param out The transform
 * @param transform World transform of the entity
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
 * The base color is mixed toward the tints of the ancestors and then of the shape in display
 * encoding, then it and the emission are converted to linear light.
 *
 * The alpha is the opacity of the shape times that of its ancestors. Color and alpha are stored
 * as unorm16x4 (box_draws_color_pack); the clip index goes into ids[0].
 *
 * @param out The shading
 * @param material Material components of the entity
 */
void box_draws_material(
    box_render_shading_t *out,
    const box_draw_material_t *material);

/** Write a display encoded color mixed toward the tints of a shape as linear light: those of
 * its ancestors and then its own, as its base and border color are.
 *
 * @param out Three floats receiving the color
 * @param color The color, display encoded, three channels
 * @param material Material components of the entity, of which inherited and tint are read
 */
void box_draws_tinted(
    float *out,
    const uint8_t *color,
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
 * DiffuseTransmission 0 without one, its blur 0 when negative or not finite, Fresnel 0 without
 * one. A Distortion without Water packs its strength, frequency and phase, and without
 * Transmission the transmission of a factor of 1 (box_draws_distortion_pack). A Water packs its
 * WaterCaustics in the words of the Fresnel, which it ignores (box_draws_water_caustics_pack).
 * Opacity, tints and the 2D shape are not.
 *
 * The shaders wrap the projection of the absolute position of the local origin of a world mapped
 * Water to BOX_RENDER_WATER_ORIGIN_PERIOD repeats; its ripple offset takes the whole periods that
 * leaves out times the ripple scale as the fragments read it (a half float), so the second normal
 * sample stays continuous across a shift of the origin, for an up facing surface: u along Z, v
 * along -X.
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

/** Resolve the 2D shape of a shape or box shadow record with the Border of its material, the
 * rendered value when it has a node: the shape of the record with that Border applied by
 * box_shape2d_border, its corner radius set again from it for a Rect.
 *
 * Pure: reads no world, so the resolver calls it on the render thread.
 *
 * @param material The material
 * @param out Receives the shape
 */
void box_draws_transition_shape(
    const box_draw_transition_material_t *material,
    Shape2D *out);

/** Encode a record from a material transition with the encoding of the game thread: a table
 * record with box_draws_material_pack, a 2D shading or glyph with box_draws_material, a mesh
 * shading with box_draws_mesh_shading, a shadow with box_draws_box_shadow_encode.
 *
 * Words that do not depend on a transitioned component (clip, rank, glyph atlas area, mesh
 * material slot, emissive.w of a polygon) are left as they are.
 *
 * Pure: reads no world, so the resolver calls it on the render thread.
 *
 * @param material The material, with the rendered values of its nodes
 * @param dst The record in the packet data, unaligned
 */
void box_draws_transition_encode(
    const box_draw_transition_material_t *material,
    void *dst);

/** Apply the Tint and Opacity of an entity after a chain of composed tints and opacities, as
 * box_draws_fade and box_draws_mesh_shading compose them.
 *
 * @param world The world
 * @param e The entity
 * @param tint The composed tints of the chain
 * @param alpha The product of the opacities of the chain
 */
void box_draws_transition_fade_own(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint,
    float *alpha);

/** Get the value of a component in the target material of a material or shading record.
 *
 * @param material The material
 * @param component The component
 * @return The value, NULL for a component that is not a material component
 */
void* box_draws_transition_value(
    box_draw_transition_material_t *material,
    box_render_transition_component_t component);

/** Write the transform and the shading of a shadow from a box shadow record with the encoding
 * of the game thread (box_draws_box_shadow_patch), using the rendered Tint, Opacity and fade.
 *
 * The rendered Border is applied to the shape first, so the cutout radius and the cover alpha
 * follow it. Pure: the resolver calls it on the render thread.
 *
 * @param record The record, its material holding the rendered values
 * @param shadow The BoxShadow its node renders
 * @param world World matrix the shape renders at
 * @param transform The box_render_transform_t of the shadow in the packet data, unaligned
 * @param shading The box_render_shading_t of the shadow in the packet data, unaligned
 */
void box_draws_transition_box_shadow(
    const box_draw_transition_box_shadow_t *record,
    const BoxShadow *shadow,
    mat4 world,
    void *transform,
    void *shading);

/** Send the fade record of a node of a Tint or Opacity transition: the composed Tint and
 * Opacity from its parent node, or of every ancestor without a parent node, and its target.
 *
 * @param world The world
 * @param p The packet
 * @param e The entity of the node
 * @param node The node id
 * @param parent The parent node, 0 for none
 * @param tint The composed tints of the chain from the parent node, NULL without one
 * @param alpha The product of the opacities of that chain, read with tint
 * @param leaving_tint The neutral Tint a removed Tint of the node animates to, NULL when it does
 * not leave
 * @param leaving_opacity The neutral Opacity a removed Opacity of the node animates to, NULL
 * when it does not leave
 */
void box_draws_transition_send_fade(
    const ecs_world_t *world,
    box_render_packet_t *p,
    ecs_entity_t e,
    uint32_t node,
    uint32_t parent,
    const box_draw_tint_t *tint,
    float alpha,
    const Tint *leaving_tint,
    const Opacity *leaving_opacity);

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

/** Check whether the shape, the BoxShadow or a glyph of an entity has a slot in an alive shadow
 * caster batch. A BoxShadow is drawn blended and casts nothing today; it is checked so the range
 * of its node follows it, as box_transition_send_slots moves it with its shape.
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
 * @param time Total render time the TextureScroll offsets are computed at
 * @param out Receives the material
 */
void box_draws_material_of(
    const ecs_world_t *world,
    ecs_entity_t owner,
    double time,
    box_draw_material_t *out);

/** Resolve a WaterWave to what it travels with: its normalized direction (+X for a zero or
 * non finite one), its wave number k = 2 pi / wavelength and its angular frequency, speed times
 * k, or sqrt(g k) without a speed (deep water dispersion, g BOX_MATERIAL_WATER_GRAVITY).
 *
 * @param wave The wave
 * @param direction Receives the direction, NULL to skip
 * @param k Receives the wave number, 0 for a disabled wave, NULL to skip
 * @param omega Receives the angular frequency, 0 for a disabled wave, NULL to skip
 * @return Whether the wave is enabled: a positive finite wavelength
 */
bool box_draws_water_wave_resolve(
    const WaterWave *wave,
    float *direction,
    double *k,
    double *omega);

/** Get the rate a wave's phase grows with: k times the WaterFlow velocity along its direction
 * plus its angular frequency, radians per second, 0 for a disabled wave.
 *
 * @param wave The wave
 * @param flow The velocity of the surface, NULL for still water
 * @return The rate
 */
double box_draws_water_wave_rate(
    const WaterWave *wave,
    const WaterFlow *flow);

/** Wrap a phase to 0..2 pi, 0 when not finite.
 *
 * @param phase The phase
 * @return The wrapped phase
 */
double box_draws_water_phase_wrap(
    double phase);

/** Advance the phases of the waves of a WaterWaves by a time step in double precision, at the
 * rates of box_draws_water_wave_rate, wrapped to 0..2 pi.
 *
 * @param phases The phases of the BOX_MATERIAL_WATER_WAVES waves
 * @param waves The waves
 * @param flow The velocity of the surface, NULL for still water
 * @param dt Seconds, a step that is not finite or negative does nothing
 */
void box_draws_water_waves_step(
    double *phases,
    const WaterWaves *waves,
    const WaterFlow *flow,
    double dt);

/** Pack a WaterWaves with its phases into a set of the wave table: the directions normalized,
 * the phases as f32 below 2 pi, the steepness clamped to 0..1 and a disabled wave zero.
 *
 * @param out The set
 * @param waves The waves
 * @param phases The phases of the BOX_MATERIAL_WATER_WAVES waves
 */
void box_draws_water_waves_pack(
    box_render_water_waves_t *out,
    const WaterWaves *waves,
    const double *phases);

/** Get the set of the wave table of an owner of the material table, 0 for none.
 *
 * @param m The table
 * @param owner The owner
 * @return The set
 */
uint32_t box_draws_water_waves_find(
    const box_draw_materials_t *m,
    ecs_entity_t owner);

/** Get the set of the wave table of an owner whose Water has WaterWaves, or hand it a free one
 * whose phases start as if integrated from time 0 to the time of the table. 0 when the table is
 * full, logged once.
 *
 * @param m The table
 * @param owner The owner
 * @param waves Its waves
 * @param flow Its WaterFlow, NULL for none
 * @return The set
 */
uint32_t box_draws_water_waves_set(
    box_draw_materials_t *m,
    ecs_entity_t owner,
    const WaterWaves *waves,
    const WaterFlow *flow);

/** Integrate the phases of every set of the wave table to the time of the table with the
 * WaterWaves and WaterFlow of its owner, and free the sets whose owner is gone, no longer owns
 * a slot or lost its Water, WaterWaves or Scene.water.
 *
 * @param world The world
 * @param m The table
 */
void box_draws_water_waves_advance(
    const ecs_world_t *world,
    box_draw_materials_t *m);

/** Create the buffer of the wave table if needed and upload the sets that changed.
 *
 * @param m The table
 * @param p The packet
 */
void box_draws_water_waves_upload(
    box_draw_materials_t *m,
    box_render_packet_t *p);

/** Free the wave table.
 *
 * @param m The table
 */
void box_draws_water_waves_fini(
    box_draw_materials_t *m);

/** Wrap an offset of the water frame of WaterCaustics to 0..BOX_RENDER_CAUSTICS_PERIOD cells,
 * 0 when not finite.
 *
 * @param offset The offset in cells
 * @return The wrapped offset
 */
double box_draws_water_caustics_wrap(
    double offset);

/** Get the cells per world unit of a WaterCaustics: 1 over its scale, which is at least
 * BOX_RENDER_CAUSTICS_MIN; 1 over the default scale for a scale that is not finite.
 *
 * @param caustics The caustics
 * @return The frequency
 */
float box_draws_water_caustics_frequency(
    const WaterCaustics *caustics);

/** Phase of the animation of WaterCaustics at a time: speed times time over
 * BOX_RENDER_CAUSTICS_LOOP wrapped to [0, 1) in double precision, 0 when not finite.
 *
 * @param speed WaterCaustics.speed, cells per second
 * @param time Total render time
 * @return The phase
 */
float box_draws_water_caustics_phase(
    float speed,
    double time);

/** Advance the water frame of WaterCaustics by a time step in double precision: the WaterFlow
 * velocity times the frequency of the caustics times the step, wrapped
 * (box_draws_water_caustics_wrap).
 *
 * @param frame The X and Z offset of the frame in cells
 * @param caustics The caustics
 * @param flow The velocity of the surface, NULL for still water
 * @param dt Seconds, a step that is not finite or negative does nothing
 */
void box_draws_water_caustics_step(
    double *frame,
    const WaterCaustics *caustics,
    const WaterFlow *flow,
    double dt);

/** Check whether the record of a Water with WaterCaustics changes every frame: it has a
 * WaterFlow or a speed other than 0, and a strength above 0.
 *
 * @param caustics The caustics, NULL for none
 * @param flow The WaterFlow, NULL for none
 * @return Whether it moves
 */
bool box_draws_water_caustics_moves(
    const WaterCaustics *caustics,
    const WaterFlow *flow);

/** Get the water frame of the WaterCaustics of an owner at the time of the table. An owner with a
 * WaterFlow keeps its frame in the table, created as if integrated from time 0; still water has
 * the frame 0.
 *
 * @param m The table
 * @param owner The owner
 * @param caustics Its caustics
 * @param flow Its WaterFlow, NULL for none
 * @param out The X and Z offset of the frame in cells
 */
void box_draws_water_caustics_frame(
    box_draw_materials_t *m,
    ecs_entity_t owner,
    const WaterCaustics *caustics,
    const WaterFlow *flow,
    float *out);

/** Get the water frame of the WaterCaustics of an owner the table holds without changing it.
 *
 * @param m The table
 * @param owner The owner
 * @param out The X and Z offset of the frame in cells, 0 for an owner without one
 * @return Whether the table holds a frame of the owner
 */
bool box_draws_water_caustics_find(
    const box_draw_materials_t *m,
    ecs_entity_t owner,
    float *out);

/** Integrate the water frame of every owner with WaterCaustics and a WaterFlow to the time of
 * the table, and forget those whose owner is gone, no longer owns a slot or lost its Water,
 * WaterCaustics, WaterFlow or Scene.water.
 *
 * @param world The world
 * @param m The table
 */
void box_draws_water_caustics_advance(
    const ecs_world_t *world,
    box_draw_materials_t *m);

/** Free the water frames of the WaterCaustics.
 *
 * @param m The table
 */
void box_draws_water_caustics_fini(
    box_draw_materials_t *m);

/** Compute the rim factor of a Fresnel, the C twin of fresnel_factor of material.wgsl:
 * saturate(bias + pow(1 - saturate(ndv), power)), 1 - ndv kept above 1e-6.
 *
 * @param ndv Cosine between the shading normal and the direction toward the camera
 * @param power Fresnel.power
 * @param bias Fresnel.bias
 * @return The factor, 0..1
 */
float box_draws_fresnel_factor(
    float ndv,
    float power,
    float bias);

/** Phase of the noise of a Distortion at a time: speed times time wrapped to [0, 1) in double
 * precision, the period over which the noise of mesh_extended.wgsl repeats in time.
 *
 * @param speed Distortion.speed, features per second
 * @param time Total render time
 * @return The phase, 0 for a speed or time that is not finite
 */
float box_draws_distortion_phase(
    float speed,
    double time);

/** Phase of the shore waves of a Water at a time: time over Water.shore_period wrapped to
 * [0, 1) in double precision, so it stays precise however long the render runs.
 *
 * @param period Water.shore_period, seconds
 * @param time Total render time
 * @return The phase, 0 for a period that is not positive and finite or a time that is not finite
 */
float box_draws_water_shore_phase(
    float period,
    double time);

/** Phase of the two phase flow mapping of WaterFlow.textures at a time: time over
 * BOX_RENDER_WATER_FLOW_PERIOD wrapped to [0, 1) in double precision.
 *
 * @param time Total render time
 * @return The phase, 0 for a time that is not finite
 */
float box_draws_water_flow_phase(
    double time);

/** Mix factor of the deep color of a Water body, the C twin of water_depth_mix of
 * material.wgsl: 1 - exp(-3 d / depth), 1 for an infinite depth (the sky behind).
 *
 * @param d Depth of the water at the pixel along its normal, world units
 * @param depth Water.depth, at least BOX_RENDER_WATER_MIN
 * @return The factor, 0..1
 */
float box_draws_water_depth_mix(
    float d,
    float depth);

/** Share of the scene behind a Water that shows through its body, the C twin of
 * water_transmittance of material.wgsl: exp(-3 ray_length / visibility), 0 for an infinite
 * ray (the sky behind).
 *
 * @param ray_length Water along the view ray, world units
 * @param visibility Water.visibility, at least BOX_RENDER_WATER_MIN
 * @return The transmittance, 0..1
 */
float box_draws_water_transmittance(
    float ray_length,
    float visibility);

/** Tint of one channel of the scene behind a Water, the C twin of water_absorbed of
 * material.wgsl: absorption to the power of ray_length / visibility, 1 for an infinite ray (the
 * sky behind, which shows nothing) and for a white absorption.
 *
 * @param absorption Channel of Water.absorption in linear light, 0..1
 * @param ray_length Water along the view ray, world units
 * @param visibility Water.visibility, at least BOX_RENDER_WATER_MIN
 * @return The tint, 0..1
 */
float box_draws_water_absorption(
    float absorption,
    float ray_length,
    float visibility);

/** Foam factor of a Water, the C twin of water_foam of material.wgsl: saturate(1 - d /
 * foam_width), 0 for a width of 0. With a texture the band is cut at the red of the texture,
 * smoothstep(t, t + 0.1, tex) with t = 1 - band, so the foam thins out toward its edge.
 *
 * @param d Depth of the water at the pixel along its normal, world units
 * @param foam_width Water.foam_width
 * @param textured Whether the foam is broken up by a texture
 * @param tex Red of the foam texture at the pixel, ignored without one
 * @return The foam, 0..1
 */
float box_draws_water_foam(
    float d,
    float foam_width,
    bool textured,
    float tex);

/** Compute the offsets of a TextureScroll at a time: velocity times time per axis in double
 * precision, wrapped to 0..2 and kept positive, so WrapMirror tiles stay continuous. A velocity
 * that is not finite does not move.
 *
 * @param scroll The component, NULL for none
 * @param time Total render time
 * @param out Receives the u and v offsets of every layer, then the u and v offsets the normal
 *        map adds on top, all 0 without a TextureScroll
 */
void box_draws_texture_scroll(
    const TextureScroll *scroll,
    double time,
    float *out);

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
 * A sliced rectangle holds its slice index in uv.x. A polygon holds its border as a rectangle
 * does, in local units, and the index of its polygon record in emissive w.
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

/** Check whether a shape is alpha tested in its scene: box_draws_cutout or a 3D shape with a
 * VertexReveal, which discards in the alpha tested path, only when its scene has
 * BOX_RENDER_FEATURE_ALPHA_CUTOUT.
 *
 * @param cutoff The AlphaCutoff of the shape, NULL for none
 * @param reveal The VertexReveal of the shape, NULL for none
 * @param shape2d The resolved 2D shape, NULL for a 3D shape
 * @param features BOX_RENDER_FEATURE_* features of the scene of a 3D shape
 * @return Whether its fragments are tested against the cutoff
 */
bool box_draws_scene_cutout(
    const AlphaCutoff *cutoff,
    const VertexReveal *reveal,
    const Shape2D *shape2d,
    uint32_t features);

/** Resolve the blend mode a shape draws with in its scene: box_draws_blend, Opaque for a 3D
 * shape whose scene lacks BOX_RENDER_FEATURE_BLEND_MODES.
 *
 * @param mode The BlendMode of the shape, NULL for Opaque
 * @param alpha The resolved alpha of the shape
 * @param cutout Whether the shape is alpha tested (box_draws_scene_cutout)
 * @param shape2d The resolved 2D shape, NULL for a 3D shape
 * @param features BOX_RENDER_FEATURE_* features of the scene of a 3D shape
 * @return The blend mode
 */
uint8_t box_draws_scene_blend(
    const BlendMode *mode,
    float alpha,
    bool cutout,
    const Shape2D *shape2d,
    uint32_t features);

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

/** Resolve the DrawRange of a shape: its own, inherited from a prefab, or else that of its
 * nearest ChildOf ancestor that has one. A near or far that is not positive is none, and so is
 * the fade without either.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives near, far and fade, all 0 without a DrawRange
 * @return Whether the shape has a near or a far distance
 */
bool box_draws_range(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *out);

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
 * A batch that outgrows its run grows to the step at or above its count, so a run of 1,000
 * slots grows to 1,460. From its own count, the result is the count rounded up to
 * BOX_DRAWS_GROW_ALIGN slots, at least BOX_DRAWS_MIN_CAPACITY (box_draws_reserve).
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
 * takes or creates when it has none or its material changed since, or with a private slot in a
 * DynamicMaterial batch, with a TextureScroll or while a material transition runs.
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
 * owner to a shared slot, unless it is a prefab, in a DynamicMaterial batch or scrolls.
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
 * The private slots of TextureScroll owners pack at the packet time every send and upload when
 * they moved; a table without one packs nothing per frame.
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

/** Compute the local bounds of the slots of a batch with parametric surfaces: the union of the
 * bounds of each surface, and of the mesh for a slot without one. A plain mesh batch keeps no
 * surfaces, so it is not bounded and draws with the bounds of its mesh.
 *
 * @param b The batch
 * @param cache The mesh cache
 */
void box_draws_bounds(
    box_draw_batch_t *b,
    box_mesh_cache_t *cache);

/** Compute the world bounds of a batch: the box around the world box the cull pass computes for
 * each slot (bounds of cull.wgsl) from the local bounds it tests, and the largest radius of such
 * a box. Both are padded, so rounding never makes them smaller than what the GPU computes.
 *
 * A batch without DynamicTransform computes them again only when a slot, its transform or its
 * surface changed, or its local bounds did. A dynamic batch computes them every frame from the
 * transforms box_draws_dynamic just wrote, one pass over its slots. A mover batch computes them
 * every frame from the bounds of its groups (box_draws_groups) placed by their movers.
 *
 * @param d The state
 * @param b The batch, ready
 * @param cache The mesh cache
 * @param wanted Whether a view of the packet culls batches by their bounds
 */
void box_draws_world_bounds(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    box_mesh_cache_t *cache,
    bool wanted);

/** Compute the bounds of a blended instance in the image plane of a view, which tell the
 * transmissive draws of the view whose footprints can overlap (box_render_batch_t.screen).
 *
 * The corners of the world box of the instance project to x and y over -z of view space for a
 * perspective camera, to x and y for an orthographic one, which any projection keeps disjoint.
 *
 * An instance without mesh bounds, a Water shape and one that reaches behind the camera get none.
 *
 * @param d The state, during a send
 * @param b The batch, ready
 * @param v The view
 * @param slot The slot of the instance
 * @param out Receives minimum x and y, then maximum x and y
 * @return Whether the instance has bounds
 */
bool box_draws_screen_bounds(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    const box_render_view_t *v,
    int32_t slot,
    float *out);

/** A planet whose blended instances may be layers around its ground, and the local bounds of
 * the instances of a batch (box_draws_layer_of) */
typedef struct box_draw_layer_t {
    vec3 center;            /**< World position of the PlanetAtmosphere */
    float ground;           /**< Radius of the ground sphere, PlanetAtmosphere.radius */
    float top;              /**< Radius of the top sphere of the atmosphere */
    vec3 local_center;      /**< Center of the local bounds of the instances */
    vec3 local_extent;      /**< Half size of the local bounds of the instances */
} box_draw_layer_t;

/** Get the planet of a blended batch inside a PlanetAtmosphere (key.atmosphere), for
 * box_draws_layer.
 *
 * @param d The state, during a send
 * @param b The batch, ready
 * @param out Receives the planet and the local bounds of the instances
 * @return Whether the batch has a valid PlanetAtmosphere and bounds
 */
bool box_draws_layer_of(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_draw_layer_t *out);

/** Check whether an instance is a layer around the ground of a planet, such as clouds: the
 * sphere inscribed in its world bounds holds the ground sphere and lies within the top sphere.
 * Such a layer draws before the atmosphere shells, which haze it with the ground; the other
 * blended instances, such as a ring or a window near the camera, draw after them.
 *
 * @param l The planet and the local bounds of the instance
 * @param t The transform of the instance
 * @return Whether it is
 */
bool box_draws_layer(
    const box_draw_layer_t *l,
    const box_render_transform_t *t);

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

/** Get the texture handle a layer of a batch samples a texture entity through: its image, or
 * the image of the surface that renders into it.
 *
 * @param d The state
 * @param e The texture entity, 0 for none
 * @return The handle, 0 while the entity has no image
 */
uint32_t box_draws_texture_handle(
    const box_render_draws_t *d,
    ecs_entity_t e);

/** Write a TileTerrain into the blend record of its batch: its grid as the mask and the
 * albedo, ORM and normal atlases of each kind as a layer.
 *
 * @param d The state
 * @param e The terrain entity
 * @param terrain Its TileTerrain
 * @param r The record, zeroed with default transforms
 */
void box_draws_batch_tiles(
    const box_render_draws_t *d,
    ecs_entity_t e,
    const TileTerrain *terrain,
    box_render_texture_blend_t *r);

/** Write the TextureBlend of a texture_blend batch into the packet data of its draw.
 *
 * Writes the texture handles of the mask, layers and heights, the TextureTransform of each layer
 * and the contrast, so a change of a layer reaches the draw without a change of the key.
 *
 * Without a contrast the heights stay 0. A deleted layer counts as one without PbrTextures.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param out The draw, unchanged for a batch without texture_blend; its TextureLayers are
 * written after it (box_draws_batch_layers)
 */
void box_draws_batch_blend(
    box_render_packet_t *p,
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out);

/** Get the entity whose TextureBlend a 3D shape draws with: the shape itself when it owns a
 * TileTerrain, the terrain of a TerrainTile with a TextureBlend (a copy of that of its
 * terrain), so the tiles of a terrain share a batch, else the owner of the TextureBlend it has
 * or inherits, 0 when it has neither or is no mesh shape. Alpha tested, blended, transmissive
 * and triplanar shapes blend alike.
 *
 * @param world The world
 * @param e The shape
 * @param key Its key, with its pipeline resolved
 * @return The owner, 0 for none
 */
ecs_entity_t box_draws_blend_owner(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_draw_key_t *key);

/** Hash the TextureLayers of a batch key, 0 without layers.
 *
 * @param key The key
 * @return The hash
 */
uint64_t box_draws_layers_hash(
    const box_draw_key_t *key);

/** Check whether a batch key has TextureLayers: a layer entity in some slot.
 *
 * @param key The key
 * @return Whether its batch draws with the layers variants
 */
bool box_draws_key_layered(
    const box_draw_key_t *key);

/** Write the TextureLayers of a batch whose key has layers into the packet data of its draw.
 *
 * Writes the entity, values and texture handles of each layer, read from the layer entities
 * every frame, so a change of a layer (its amount) reaches the draw and the buffer of the layer
 * without a change of the key. A deleted layer or one without TextureLayer does nothing.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param out The draw, unchanged for a batch without layers
 */
void box_draws_batch_layers(
    box_render_packet_t *p,
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out);

/** Pack the values of a TextureLayer entity as the mesh shaders read them.
 *
 * @param world The world
 * @param e The layer entity, alive with a TextureLayer
 * @param time Total render time of the frame, which its TextureScroll moves by
 * @param out Receives the values
 */
void box_draws_layer_pack(
    const ecs_world_t *world,
    ecs_entity_t e,
    double time,
    box_render_texture_layer_t *out);

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
 * an index when it has none.
 *
 * The walk stops at a LayoutFloat that is not a clipped overlay; when that float has
 * hide_clipped, the entity takes the clip index of the float, which hides with its anchor.
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

/** Get the index of the slice record of a sliced 2D shape, giving it one when it has none, and
 * mark the record to be computed again in the next send.
 *
 * A slice record takes two consecutive records of the clip buffer, computed in a send with the
 * clips from the Shape2D, Image, ImageSlice and the size of the texture of the image of the
 * shape.
 *
 * @param d The state
 * @param e The shape, which changed
 * @return The index of the first of its two records
 */
int32_t box_draws_slice_of(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Free the slice record of a 2D shape that is removed or no longer sliced, if it has one.
 *
 * @param d The state
 * @param e The shape
 */
void box_draws_slice_release(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Compute the slice record of a sliced 2D shape: the nine parts of its Image, as fractions of
 * the element, or the plain Image while the texture has no size: no Texture and no Surface
 * that renders into it (box_surface_texture_size).
 *
 * @param world The world
 * @param e The shape
 * @param out Receives the record
 * @return Whether the shape is still sliced
 */
bool box_draws_slice_record(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_render_slice_t *out);

/** Get the index of the polygon record of a polygon, giving it one when it has none or when its
 * record no longer holds its edges, and mark it to be computed in the next send when it is new,
 * moved or its shape changed.
 *
 * The record takes BOX_RENDER_POLYGON_RECORDS(edges) consecutive records of the clip buffer,
 * which the edges of its Shape2D size; its index changes only when they no longer fit.
 *
 * @param world The world
 * @param d The state
 * @param e The shape, with a polygon Shape2D
 * @param changed Whether its shape may have changed, a write of its shading; a move alone
 * keeps the record
 * @return The index of the first clip record of its record
 */
int32_t box_draws_polygon_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    bool changed);

/** Free the polygon record of a 2D shape that is removed or no longer a polygon, if it has one.
 *
 * @param d The state
 * @param e The shape
 */
void box_draws_polygon_release(
    box_render_draws_t *d,
    ecs_entity_t e);

/** Write the areas of the clips into the clip buffer when one changed: added, removed, moved,
 * resized or reparented; and the slice records of the sliced 2D shapes and the polygon records
 * of the polygons that changed.
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

/** Send a mesh to the backend unless it has it, taking a cache reference to it.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 * @param handle The mesh handle
 * @return Whether the backend has the mesh
 */
bool box_draws_mesh_send(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    uint32_t handle);

/** Get the levels of detail the views of a packet draw with.
 *
 * @param p The packet
 * @return The most levels of a view (box_render_view_t.lod), 0 when no view has levels
 */
int32_t box_draws_lod_levels(
    const box_render_packet_t *p);

/** Find the meshes of the levels of detail of an opaque 3D batch (box_draw_batch_t.lods) and
 * send them to the backend, which holds them as it holds the mesh of the batch.
 *
 * The levels are generated by the mesh cache on their first request, so a packet without
 * views with levels generates none. A level the backend cannot have draws the level before.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 * @param b The batch, whose lods are cleared when it is not ready, blended or not 3D
 * @param levels Levels the views of the packet draw with, 0 for none
 */
void box_draws_lods(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    box_draw_batch_t *b,
    int32_t levels);

/** Destroy the meshes no shape uses any more, the levels of detail with the last shape of
 * their mesh, and drop the hold of the renderer on them.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 */
void box_draws_meshes_release(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache);

/** Forget what an earlier backend received, so meshes and buffers are sent again.
 *
 * @param d The state
 * @param cache The mesh cache
 */
void box_draws_reset_backend(
    box_render_draws_t *d,
    box_mesh_cache_t *cache);

#include "draw_box_shadow.h"

#endif
