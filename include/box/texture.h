/**
 * @file box/texture.h
 * @brief Texture components: images loaded from files on the job system.
 *
 * A TexturePath loads a raster image (stb_image) or an SVG (resvg) on a worker thread; the
 * engine then writes Texture and TextureStatus, and PbrTextures (box/material.h) samples it.
 *
 * A TextureGen generates a seamless height, albedo or normal map from noise, blocks and cells
 * instead, on the job system as well. A TextureGraph computes its image from a graph of node
 * entities on the render backend.
 */

#ifndef BOX_TEXTURE_H
#define BOX_TEXTURE_H

#include "types.h"

#ifdef BOX_TEXTURE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Size of a loaded texture, written by the engine when a load succeeds */
ECS_STRUCT(Texture, {
    int32_t width;  /**< Width in pixels */
    int32_t height; /**< Height in pixels */
});

/** Texture loaded from an image file, which writes Texture and TextureStatus.
 *
 * Raster formats load with stb_image, .svg files render with resvg at the requested size (one
 * dimension keeps the aspect ratio). Set linear for images made to be sampled without sRGB
 * decoding, single_channel for a mask such as a light cookie: R8Unorm, a quarter of RGBA8.
 *
 * The largest dimension is capped by Surface.texture_quality. Changing a member loads the
 * image again, and so does a change of its file that FileWatch (box/app.h) sees; the previous
 * image stays in use until the new one is ready.
 */
ECS_STRUCT(TexturePath, {
    char *path;     /**< Image file, relative to the working directory or an ancestor of it */
    int32_t width;  /**< Requested width in pixels, 0 for source width. */
    int32_t height; /**< Requested height in pixels, 0 for source height. */
    bool linear;    /**< The color channels hold linear light, not sRGB, default off: they are
                     *   encoded to sRGB on load, so the layers that decode color read them as
                     *   they are stored. Alpha is kept; leave it off for ORM and normal maps */
    bool single_channel; /**< Load as R8Unorm with mips, default off: the red channel as stored,
                          *   linear has no effect. Layers, UI images, particle sprites and other
                          *   bindings of its Texture sample it as (r, 0, 0, 1), as WebGPU does */
});

/** Pattern of a layer of a TextureGen */
ECS_ENUM(TextureGenPattern, {
    GenNoise = 0,  /**< Periodic fbm gradient (Perlin) noise, 0..1 around 0.5 */
    GenBlocks = 1, /**< Courses of blocks or planks: 1 on a face, 0 in the gaps, beveled; each
                    *   block has a random id and a tone in the order of tone_order */
    GenCells = 2   /**< Periodic Voronoi cells, by the distance to the nearest point or edge */
});

/** How a layer of a TextureGen combines with the sum of the layers before it */
ECS_ENUM(TextureGenOp, {
    GenAdd = 0,      /**< Add the layer, the default */
    GenMultiply = 1, /**< Multiply the sum by the layer */
    GenMax = 2       /**< Keep the larger of the sum and the layer */
});

/** What the value of a GenCells layer measures */
ECS_ENUM(TextureGenDistance, {
    GenNearest = 0, /**< Distance to the nearest point in cell sizes: 0 at the point, the default */
    GenEdge = 1     /**< Difference of the distances to the two nearest points in cell sizes,
                     *   0 on the border between two cells, through a smoothstep over edge */
});

/** Order of the tones of the blocks of a GenBlocks layer, see TextureGenLayer.tones */
ECS_ENUM(TextureGenToneOrder, {
    GenToneRandom = 0,  /**< A random tone per block, independent of its random id, the
                         *   default */
    GenToneChecker = 1, /**< Tone (course + block) % tones: a checker for two tones */
    GenToneCourses = 2, /**< Tone course % tones: alternating courses */
    GenToneColumns = 3  /**< Tone block % tones: alternating blocks along each course, the same in
                         *   every course */
});

/** What a TextureGen stores */
ECS_ENUM(TextureGenOutput, {
    GenHeight = 0, /**< The height field as a single channel texture (R8Unorm), the default */
    GenAlbedo = 1, /**< The height field mapped through the color ramp, RGBA8, opaque; with a
                    *   palette the ramp color times the palette color of the tone */
    GenNormal = 2  /**< Tangent space normal map of the height field, green up the image, stored */
});

/** Layer of the height field of a TextureGen: a pattern with its weight in the sum.
 *
 * Positions are fractions of the texture, so a layer looks the same at every size, and members of
 * the other patterns are ignored. Its randomness comes from TextureGen.seed and seed.
 *
 * Two block or cell layers with the same seed and counts share their cuts or points, such as a
 * mortar mask and a dome of the same cells; noise layers of another frequency are independent.
 */
ECS_STRUCT(TextureGenLayer, {
    TextureGenPattern kind; /**< Pattern, default GenNoise */
    TextureGenOp op;        /**< How the layer combines with the layers before it, default GenAdd */
    float amount;           /**< Weight of the pattern; a layer with neither amount, id_amount
                             *   nor tone_amount weighs 1. May be negative */
    float id_amount;        /**< Weight of the random per block or per cell id in 0..1, which
                             *   varies the stones on top of their tone; GenNoise has no id */
    int32_t seed;           /**< Mixed with TextureGen.seed for the randomness of the layer */
    int32_t tones;          /**< GenBlocks and GenCells: number of tones a block or cell takes,
                             *   which pick the TextureGen.palette color; 0 for none. Without a
                             *   seam when courses or per_course is a multiple of tones */
    TextureGenToneOrder tone_order; /**< GenBlocks: order of the tones, default GenToneRandom;
                                     *   GenCells take every order as GenToneRandom */
    float tone_amount;      /**< Weight of the tone level tone / (tones - 1) in the height, 0
                             *   for one tone; the default 0 keeps the tone out of the height,
                             *   so a palette colors blocks without changing the normal map */
    float frequency;        /**< GenNoise: lattice cells across the width in the first octave,
                             *   rounded to a whole number so it tiles, default 4; down the
                             *   height as many per pixel */
    int32_t octaves;        /**< GenNoise: octaves of the fbm, default 1 */
    float lacunarity;       /**< GenNoise: frequency factor from one octave to the next, default
                             *   2; the frequency of each octave is rounded to a whole number */
    float gain;             /**< GenNoise: amplitude factor from one octave to the next, default
                             *   0.5 */
    int32_t courses;        /**< GenBlocks: courses down the texture, default 8 */
    int32_t per_course;     /**< GenBlocks: blocks along a course, default 4 */
    float gap;              /**< GenBlocks: width of the joints as a fraction of the width */
    float jitter;           /**< GenBlocks: random shift of each cut, up to +-jitter blocks */
    float stagger;          /**< GenBlocks: shift of every second course in blocks */
    float softness;         /**< GenBlocks: width of the bevel from a joint to the face as a
                             *   fraction of the width, default 1.3 times gap; with neither a
                             *   hard edge */
    int32_t count;          /**< GenCells: approximate number of cells, default 64 */
    float squash;           /**< GenCells: distances down the texture count squash times, so the
                             *   cells are squash times wider than high, default 1 */
    float edge;             /**< GenCells: width of the smoothstep of GenEdge in cell sizes,
                             *   default 0.25 */
    TextureGenDistance distance; /**< GenCells: what the value measures, default GenNearest */
});

/** Key of the color ramp of a TextureGen. In script {position, {r, g, b}}. */
ECS_STRUCT(TextureGenKey, {
    float position; /**< Height of the key in 0..1, after the position of the previous key */
    rgb color;      /**< Display encoded color at the height */
});

/** Texture generated from a description on the job system, written to Texture and TextureStatus
 * as a TexturePath is, so every user of a texture samples it unchanged.
 *
 * The layers make a height field: each pattern weighted by amount and id_amount, combined in order
 * by its op from 0, scaled to span 0..1. With a source, the output derives from the field of the
 * source at its size: GenAlbedo through colors, GenNormal by central differences across the wrap.
 *
 * It tiles, gives the same bytes on every platform and is capped by Surface.texture_quality. A
 * change regenerates it, keeping the previous image meanwhile. A source without a TextureGen by
 * the next frame fails it. A TextureGen and a TexturePath on one entity is an error.
 *
 * @par Ashlar of generate.py: the blocks are the face mask, their id the tone of each stone (ids *
 * 0.2), the two noise layers its fine and big noise.
 * @code
 * ashlar_height {
 *   TextureGen: {
 *     width: 512, height: 512, seed: 11
 *     layers: [
 *       {kind: GenBlocks, courses: 8, per_course: 4, gap: 0.006, jitter: 0.22, stagger: 0.5,
 *        amount: 0.8, id_amount: 0.2}
 *       {kind: GenNoise, frequency: 16, octaves: 4, amount: 0.2}
 *       {kind: GenNoise, frequency: 4, octaves: 3, amount: 0.18, seed: 1}
 *     ]
 *   }
 * }
 * ashlar {
 *   TextureGen: {source: ashlar_height, output: GenAlbedo,
 *                colors: [{0, {150, 146, 138}}, {0.5, {214, 208, 196}}, {1, {240, 234, 222}}]}
 * }
 * ashlar_n { TextureGen: {source: ashlar_height, output: GenNormal, strength: 3} }
 * @endcode
 * @par Cobble of generate.py: 100 cells, a mortar mask of the narrow smoothstep of the edge
 * distance with the cell id as tone (smooth(2, 7, edge) of about 51 pixel cells), a dome of the
 * wide one (sqrt(edge / 22)) and fine noise; the ramp puts the dark joints at the bottom.
 * @code
 * cobble_height {
 *   TextureGen: {
 *     width: 512, height: 512, seed: 111
 *     layers: [
 *       {kind: GenCells, count: 100, distance: GenEdge, edge: 0.14, amount: 0.5,
 *        id_amount: 0.15}
 *       {kind: GenCells, count: 100, distance: GenEdge, edge: 0.45, amount: 0.6}
 *       {kind: GenNoise, frequency: 32, octaves: 3, amount: 0.15}
 *     ]
 *   }
 * }
 * cobble {
 *   TextureGen: {source: cobble_height, output: GenAlbedo,
 *                colors: [{0, {72, 66, 58}}, {0.3, {120, 114, 106}}, {1, {222, 214, 202}}]}
 * }
 * cobble_n { TextureGen: {source: cobble_height, output: GenNormal, strength: 3.6} }
 * @endcode
 * @par Two-tone checker floor: the tone of each tile picks its palette color, a ramp near white
 * darkens the joints, and the random id and the noise vary the tiles of one tone.
 * @code
 * floor_height {
 *   TextureGen: {
 *     width: 512, height: 512, seed: 5
 *     layers: [
 *       {kind: GenBlocks, courses: 2, per_course: 2, gap: 0.004, softness: 0.006,
 *        tones: 2, tone_order: GenToneChecker, amount: 0.9, id_amount: 0.05}
 *       {kind: GenNoise, frequency: 16, octaves: 3, amount: 0.05}
 *     ]
 *   }
 * }
 * floor {
 *   TextureGen: {source: floor_height, output: GenAlbedo,
 *                palette: [{92, 103, 101}, {39, 53, 55}],
 *                colors: [{0, {120, 120, 120}}, {0.6, {235, 235, 235}}, {1, {255, 255, 255}}]}
 * }
 * floor_n { TextureGen: {source: floor_height, output: GenNormal, strength: 2} }
 * @endcode
 */
ECS_STRUCT(TextureGen, {
    int32_t width;                  /**< Width in pixels, 0 for 512; ignored with a source */
    int32_t height;                 /**< Height in pixels, 0 for the width; ignored with a source */
    int32_t seed;                   /**< Seed of the randomness of every layer */
    ecs_vec(TextureGenLayer) layers; /**< Layers of the height field, in order */
    ecs_entity_t source;            /**< Generator whose height field this one derives from, 0
                                     *   to use its own layers */
    TextureGenOutput output;        /**< What is stored, default GenHeight */
    ecs_vec(TextureGenKey) colors;  /**< Color ramp of GenAlbedo by height; gray without keys,
                                     *   white with a palette */
    ecs_vec(rgb) palette;           /**< GenAlbedo: display encoded color per tone of the layer
                                     *   palette_layer, palette[tone % count], multiplied by the
                                     *   ramp color; a layer without tones takes one per color */
    int32_t palette_layer;          /**< Layer of the height field (of the source with one)
                                     *   whose tone picks the palette color, default 0; it must
                                     *   exist and not be GenNoise */
    float strength;                 /**< Steepness of GenNormal, default 1: the slope is strength
                                     *   times the change of height per pixel of a 512 pixel wide
                                     *   texture, whatever its size */
});

/** Coordinates a TextureGraph is evaluated over, which TexCoords outputs */
ECS_ENUM(TextureGraphDomain, {
    DomainPlane = 0,  /**< The image plane: (u, v, 0, 1) with u and v in 0..1, v 0 at the top,
                       *   the default */
    DomainSphere = 1  /**< The unit sphere of an equirectangular image: (x, y, z, 1), the
                       *   direction of the texel, y up */
});

/** What the image of a TextureGraph stores */
ECS_ENUM(TextureGraphFormat, {
    GraphColor = 0,  /**< RGBA8 display encoded from the linear output, the default */
    GraphLinear = 1, /**< RGBA8 stored as the output is, for normal and ORM maps */
    GraphSingle = 2, /**< R8 of the first channel of the output, as a GenHeight TextureGen */
    GraphHdr = 3     /**< RGBA16Float of the linear output, for emissive maps */
});

/** Type of a value that flows between the nodes of a TextureGraph */
ECS_ENUM(TextureValueType, {
    TexScalar = 0, /**< One float */
    TexVector = 1  /**< Four floats */
});

/** Texture whose image a graph of node entities computes per texel on the render backend.
 *
 * Each node entity has one node component such as TexMath, whose entity members are its
 * inputs; the graph is the output node and every node it reaches. A change of either builds it
 * again. It writes Texture and TextureStatus as a TexturePath does, capped by texture_quality.
 *
 * A cycle, a deleted node, an entity without a node component by the next frame or an input of
 * the wrong type fails it, and so does a TexturePath or a TextureGen on the same entity.
 *
 * @code
 * plating {
 *   uv     { TexCoords: {} }
 *   bright { TexMath: {op: MathMultiply, a: uv, b_value: {0.5, 0.5, 0.5, 1}} }
 *   tinted { TexMath: {op: MathAdd, a: bright, b_value: {0.2, 0.1, 0.05, 0}} }
 *   TextureGraph: {output: tinted, width: 1024}
 * }
 * @endcode
 */
ECS_STRUCT(TextureGraph, {
    ecs_entity_t output;       /**< Node whose value becomes the image */
    int32_t width;             /**< Width in pixels, 0 for 512 */
    int32_t height;            /**< Height in pixels, 0 for the width */
    TextureGraphDomain domain; /**< Coordinates of the texels, default DomainPlane */
    TextureGraphFormat format; /**< What the image stores, default GraphColor */
});

/** Node of a TextureGraph with a constant value */
ECS_STRUCT(TexConstant, {
    vec4 value; /**< The value */
});

/** Node of a TextureGraph whose value is the coordinates of the texel in the domain of the
 * graph, see TextureGraphDomain */
ECS_STRUCT(TexCoords, {
    float scale; /**< Factor of the coordinates, 0 for 1 */
});

/** Operation of a TexMath node */
ECS_ENUM(TextureMathOp, {
    MathAdd = 0,      /**< a + b, the default */
    MathSubtract = 1, /**< a - b */
    MathMultiply = 2, /**< a * b */
    MathMin = 3,      /**< min(a, b) */
    MathMax = 4,      /**< max(a, b) */
    MathMix = 5,      /**< mix(a, b, c): a where c is 0, b where c is 1 */
    MathClamp = 6     /**< clamp(a, b, c): a limited to b..c */
});

/** Node of a TextureGraph that combines two or three vectors per channel. An input without a
 * node takes its value member instead. */
ECS_STRUCT(TexMath, {
    TextureMathOp op;   /**< Operation, default MathAdd */
    ecs_entity_t a;     /**< First input node, 0 for a_value */
    ecs_entity_t b;     /**< Second input node, 0 for b_value */
    ecs_entity_t c;     /**< Third input node of MathMix and MathClamp, 0 for c_value */
    vec4 a_value;       /**< Value of a without a node */
    vec4 b_value;       /**< Value of b without a node */
    vec4 c_value;       /**< Value of c without a node */
});

/** Node of a TextureGraph that blurs its input with a separable gaussian, in two passes.
 *
 * The input is a Scalar or a Vector node and the value has its type, input_value without one.
 * DomainPlane wraps, so a tiling input still tiles. DomainSphere wraps in longitude, scales the
 * horizontal radius by 1/cos(latitude) and continues vertical taps across a pole.
 *
 * @code
 * soft { TexBlur: {input: noise, radius: 8} }
 * @endcode
 */
ECS_STRUCT(TexBlur, {
    ecs_entity_t input; /**< Node to blur, a Scalar or a Vector, 0 for input_value */
    vec4 input_value;   /**< Value of input without a node */
    float radius;       /**< Reach of the gaussian in texels of a 512 pixel wide image, scaled
                         *   with the width so the look does not change with the resolution:
                         *   three standard deviations, 0 for no blur */
});

/** Most inputs of a node kind */
#define BOX_TEXTURE_NODE_INPUTS (4)

/** Most parameters of a node kind */
#define BOX_TEXTURE_NODE_PARAMS (8)

/** Most bytes of the parameters of a node kind in the parameter buffer */
#define BOX_TEXTURE_NODE_PARAM_BYTES (64)

/** Flag of a node kind that reads its inputs at other texels, so they are materialized */
#define BOX_TEXTURE_NODE_NEIGHBOURHOOD (1u << 0)

/** Flag of a node kind whose value depends on time */
#define BOX_TEXTURE_NODE_ANIMATED (1u << 1)

/** Flag of a node kind whose first input takes a Scalar or a Vector node and whose value has
 * the type of that node; without a node the input and the value have the declared types */
#define BOX_TEXTURE_NODE_FOLLOWS_INPUT (1u << 2)

/** Most passes of a node kind */
#define BOX_TEXTURE_NODE_PASSES (4)

/** An input port of a node kind, as TextureNodeKind lists it */
ECS_STRUCT(TextureNodePort, {
    const char *name;      /**< Member of the ecs_entity_t of the input node */
    TextureValueType type; /**< Type the input node must output */
    bool fallback;         /**< The input has a constant member used without a node */
});

/** A parameter of a node kind, as TextureNodeKind lists it */
ECS_STRUCT(TextureNodeParam, {
    const char *name;   /**< Member copied into the parameter buffer */
    int32_t offset;     /**< Byte offset in the parameters of a node */
    int32_t size;       /**< Bytes, 4 to 16 */
});

/** Description of a node kind, set by the engine on the component entity of each registered
 * kind so tools can list kinds and check connections from the world */
ECS_STRUCT(TextureNodeKind, {
    int32_t id;              /**< Id of the kind in the compiled graphs, from 1 */
    TextureValueType output; /**< Type of the value of the node */
    int32_t input_count;     /**< Inputs in inputs */
    TextureNodePort inputs[4]; /**< The BOX_TEXTURE_NODE_INPUTS inputs, in order */
    int32_t param_count;     /**< Parameters in params */
    TextureNodeParam params[8]; /**< The BOX_TEXTURE_NODE_PARAMS parameters, in order */
    int32_t param_size;      /**< Bytes of the parameters of a node, a multiple of 16 */
    const char *function;    /**< Name of the WGSL function of the node */
    bool neighbourhood;      /**< The node reads its inputs at other texels */
    bool animated;           /**< The node depends on time */
    bool follows_input;      /**< The value has the type of the node of the first input */
    int32_t passes;          /**< Passes that compute the value, 1 for most kinds */
});

/** Kind of the scalar elements of a parameter of a node kind */
typedef enum box_texture_param_kind_t {
    BOX_TEXTURE_PARAM_F32 = 0, /**< float elements, f32 in WGSL */
    BOX_TEXTURE_PARAM_I32 = 1, /**< int32_t elements, such as an enum, i32 in WGSL */
    BOX_TEXTURE_PARAM_U32 = 2  /**< uint32_t elements, u32 in WGSL */
} box_texture_param_kind_t;

/** An input of a node kind: an ecs_entity_t member of its component */
typedef struct box_texture_node_input_t {
    const char *name;           /**< Name of the member, NULL ends the list */
    ecs_size_t offset;          /**< Offset of the ecs_entity_t member */
    TextureValueType type;      /**< Type the input node must output */
    const char *value;          /**< Name of the constant member used without a node, a float
                                 *   for TexScalar or a vec4 for TexVector, NULL for none */
    ecs_size_t value_offset;    /**< Offset of that member */
} box_texture_node_input_t;

/** A parameter of a node kind: a member copied into the parameter buffer */
typedef struct box_texture_node_param_t {
    const char *name;           /**< Name of the member, NULL ends the list */
    ecs_size_t offset;          /**< Offset of the member */
    ecs_size_t size;            /**< Bytes of the member: 4, 8, 12 or 16 */
    box_texture_param_kind_t kind; /**< Kind of its elements */
} box_texture_node_param_t;

/** Description of a node kind for box_texture_node_register.
 *
 * The WGSL function fn name(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f
 * gets the parameter bytes at the offsets of TextureNodeKind.params, the input values (a scalar
 * in x, the constant without a node) and the coordinates, and returns the value, a scalar in x.
 *
 * A neighbourhood kind also gets texel: vec2i, then t0: texture_2d<f32>, ... per input. A kind of
 * more passes gets pass_index: u32 and prev: texture_2d<f32> (the pass before, 1x1 zero in pass
 * 0) between texel and t0; its last pass is the value of the node.
 */
typedef struct box_texture_node_desc_t {
    ecs_entity_t component;     /**< Component of the nodes of the kind */
    TextureValueType output;    /**< Type of the value of the node */
    box_texture_node_input_t inputs[BOX_TEXTURE_NODE_INPUTS]; /**< Inputs in order */
    box_texture_node_param_t params[BOX_TEXTURE_NODE_PARAMS]; /**< Parameters in order */
    const char *function;       /**< Name of the WGSL function */
    const char *source;         /**< WGSL source that defines the function */
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, BOX_TEXTURE_NODE_ANIMATED,
                                 *   BOX_TEXTURE_NODE_FOLLOWS_INPUT */
    int32_t passes;             /**< Passes of the function, 0 for 1, at most
                                 *   BOX_TEXTURE_NODE_PASSES; more than one makes the kind a
                                 *   neighbourhood kind */
} box_texture_node_desc_t;

/** Register a node kind, or replace the kind of a component registered before; the graphs
 * that use it build again. box.texture registers the built in kinds this way.
 *
 * @param world The world, with box.texture imported
 * @param desc The kind
 * @return The id of the kind, 0 when the description is invalid
 */
int32_t box_texture_node_register(
    ecs_world_t *world,
    const box_texture_node_desc_t *desc);

/** Progress of the load of a TexturePath, a TextureGen or a TextureGraph */
ECS_ENUM(TextureState, {
    TextureLoading = 0, /**< A load is in flight */
    TextureReady = 1,   /**< The last load succeeded and Texture holds its size */
    TextureFailed = 2   /**< The last load failed, error says why */
});

/** Outcome of the loads of a TexturePath, a TextureGen or a TextureGraph, written by the
 * engine and read only for applications */
ECS_STRUCT(TextureStatus, {
    TextureState state; /**< Progress of the last requested load */
    int32_t revision;   /**< Loads that succeeded, the image changed when it increments */
    char *error;        /**< Why the last load failed, NULL unless state is TextureFailed */
});

/** Register the box.texture module.
 *
 * @param world The world
 */
void BoxTextureImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
