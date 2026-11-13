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
#include "curve.h"

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
    GraphColor = 0,  /**< RGBA8 display encoded from the linear output, the default; the
                      *   format of colors such as a TexRamp outputs */
    GraphLinear = 1, /**< RGBA8 stored as the output is, for normal and ORM maps */
    GraphSingle = 2, /**< R8 of the first channel of the output, as a GenHeight TextureGen */
    GraphHdr = 3     /**< RGBA16Float of the linear output, for emissive maps */
});

/** Type of a value that flows between the nodes of a TextureGraph */
ECS_ENUM(TextureValueType, {
    TexScalar = 0, /**< One float */
    TexVector = 1, /**< Four floats */
    TexAny = 2     /**< Either: an input that takes both, a Scalar broadcast to the four
                    *   channels, or the output of a kind whose type follows its inputs */
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
    float rate;                /**< Updates per second of an animated graph, 0 for every frame.
                                *   An animated graph whose image no draw sampled in the
                                *   previous frame skips its updates until one samples it */
});

/** Settings of the cache of the values the texture graphs materialize, a singleton.
 *
 * Values are cached by the content hash of their node (its kind, parameters, inputs, the size and
 * domain of the graph) for every graph, so an edit evaluates only the stages after the edited
 * node, graphs with an identical subgraph share it and an edit undone finds the earlier value.
 *
 * Values no graph uses stay while the cache fits the budget, the least recently used evicted
 * first; values in use are never evicted, even over the budget.
 */
ECS_STRUCT(TextureGraphCache, {
    int64_t budget; /**< Bytes the cache holds at most, 0 for 256 MB: values no graph uses are
                     *   evicted while it holds more */
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

/** Node of a TextureGraph whose scalar value is the world time: the seconds of the world clock
 * times scale plus offset, so a paused world shows the same image. It makes the nodes after it
 * and their graph animated, see TextureGraph.rate */
ECS_STRUCT(TexTime, {
    float scale;  /**< Factor of the seconds, 0 for 1 */
    float offset; /**< Added after the factor */
});

/** Node of a TextureGraph that spreads a scalar over the four channels of a vector, such as a
 * TexTime into a TexMath */
ECS_STRUCT(TexSplat, {
    ecs_entity_t in; /**< Scalar input node, 0 for in_value */
    float in_value;  /**< Value of in without a node */
});

/** Operation of a TexMath node. Each one works per channel unless it says otherwise. */
ECS_ENUM(TextureMathOp, {
    MathAdd = 0,         /**< a + b, the default */
    MathSubtract = 1,    /**< a - b */
    MathMultiply = 2,    /**< a * b */
    MathMin = 3,         /**< min(a, b) */
    MathMax = 4,         /**< max(a, b) */
    MathMix = 5,         /**< mix(a, b, c): a where c is 0, b where c is 1 */
    MathClamp = 6,       /**< clamp(a, b, c): a limited to b..c */
    MathDivide = 7,      /**< a / b, 0 where b is 0 */
    MathPower = 8,       /**< a to the power b: 1 where b is 0, else 0 where a is 0 and where a
                          *   is negative and b is not a whole number; a negative a with a whole
                          *   b keeps the sign of an odd power */
    MathAbs = 9,         /**< abs(a) */
    MathNegate = 10,     /**< -a */
    MathFloor = 11,      /**< floor(a) */
    MathFract = 12,      /**< a - floor(a) */
    MathSin = 13,        /**< sin(a), a in radians */
    MathCos = 14,        /**< cos(a), a in radians */
    MathStep = 15,       /**< 1 where a is at least the edge b, else 0 */
    MathSmoothstep = 16, /**< smoothstep from the edge b to the edge c at a, 0 at b, 1 at c;
                          *   a step at b where b and c are equal */
    MathRemap = 17,      /**< a mapped from from_min..from_max to to_min..to_max, to_min where
                          *   the from range is empty */
    MathRemapClamped = 18, /**< MathRemap limited to the to range */
    MathDot = 19,        /**< Scalar: dot product of the first three channels of a and b, a * b
                          *   on Scalars */
    MathLength = 20,     /**< Scalar: length of the first three channels of a, abs(a) on a
                          *   Scalar */
    MathNormalize = 21,  /**< The first three channels of a scaled to length 1 with its fourth
                          *   kept, the sign of a on a Scalar; 0 stays 0 */
    MathCross = 22       /**< Vector: cross product of the first three channels of a and b, with
                          *   0 as the fourth */
});

/** Node of a TextureGraph that computes an operation on one, two or three values.
 *
 * Its inputs take Scalar and Vector nodes, or their value member without a node. It outputs a
 * Scalar when every input node its operation reads is a Scalar, else a Vector with a Scalar
 * broadcast; MathDot and MathLength always output a Scalar, MathCross a Vector.
 *
 * Unary operations read a, binary ones a and b, MathMix, MathClamp and MathSmoothstep a, b and
 * c. On Scalars a value member gives its first channel. The range members are the parameters
 * of MathRemap and MathRemapClamped.
 *
 * @code
 * glow {
 *   uv     { TexCoords: {} }
 *   red    { TexSplit: {input: uv, channel: ChannelR} }
 *   wave   { TexMath: {op: MathSin, a: red} }
 *   level  { TexMath: {op: MathRemapClamped, a: wave, from_min: -1, from_max: 1,
 *                      to_min: 0.2, to_max: 0.8} }
 *   tinted { TexMath: {op: MathMultiply, a: level, b_value: {1, 0.5, 0.25, 1}} }
 *   TextureGraph: {output: tinted}
 * }
 * @endcode
 */
ECS_STRUCT(TexMath, {
    TextureMathOp op;   /**< Operation, default MathAdd */
    ecs_entity_t a;     /**< First input node, 0 for a_value */
    ecs_entity_t b;     /**< Second input node, 0 for b_value */
    ecs_entity_t c;     /**< Third input node, 0 for c_value */
    vec4 a_value;       /**< Value of a without a node */
    vec4 b_value;       /**< Value of b without a node */
    vec4 c_value;       /**< Value of c without a node */
    float from_min;     /**< MathRemap: start of the range a is mapped from */
    float from_max;     /**< MathRemap: end of the range a is mapped from */
    float to_min;       /**< MathRemap: value at from_min */
    float to_max;       /**< MathRemap: value at from_max */
});

/** Channel of a Vector */
ECS_ENUM(TextureChannel, {
    ChannelR = 0, /**< The first channel, x, the default */
    ChannelG = 1, /**< The second channel, y */
    ChannelB = 2, /**< The third channel, z */
    ChannelA = 3  /**< The fourth channel, w */
});

/** Node of a TextureGraph whose Scalar value is one channel of a Vector.
 *
 * @code
 * height {
 *   uv { TexCoords: {} }
 *   v  { TexSplit: {input: uv, channel: ChannelG} }
 *   TextureGraph: {output: v, format: GraphSingle}
 * }
 * @endcode
 */
ECS_STRUCT(TexSplit, {
    ecs_entity_t input;     /**< Vector input node, 0 for value */
    vec4 value;             /**< Value of input without a node */
    TextureChannel channel; /**< Channel of the value, default ChannelR */
});

/** Node of a TextureGraph whose Vector value has four Scalar channels. An input without a node
 * takes its value member instead.
 *
 * @code
 * swapped {
 *   uv { TexCoords: {} }
 *   u  { TexSplit: {input: uv, channel: ChannelR} }
 *   v  { TexSplit: {input: uv, channel: ChannelG} }
 *   vu { TexCombine: {r: v, g: u, b_value: 0, a_value: 1} }
 *   TextureGraph: {output: vu}
 * }
 * @endcode
 */
ECS_STRUCT(TexCombine, {
    ecs_entity_t r;     /**< Scalar node of the first channel, 0 for r_value */
    ecs_entity_t g;     /**< Scalar node of the second channel, 0 for g_value */
    ecs_entity_t b;     /**< Scalar node of the third channel, 0 for b_value */
    ecs_entity_t a;     /**< Scalar node of the fourth channel, 0 for a_value */
    float r_value;      /**< Value of r without a node */
    float g_value;      /**< Value of g without a node */
    float b_value;      /**< Value of b without a node */
    float a_value;      /**< Value of a without a node */
});

/** Node of a TextureGraph that displaces coordinates: coords + (offset - 0.5) * amount.
 *
 * The offset is centred at 0.5, so a noise in 0..1 moves the coordinates by -0.5..0.5 times
 * amount; x, y and z move, w is kept. On DomainPlane the result keeps tiling where the offset
 * tiles. On DomainSphere the result is normalized back onto the unit sphere, so generators
 * reading it stay seamless; where the displaced point is at the origin it keeps coords.
 *
 * @code
 * warped {
 *   bumps { TexMath: {op: MathMultiply, a_value: {0.5, 0.5, 0.5, 1}, b: noise} }
 *   TexWarp: {offset: bumps, amount_value: 0.2}
 * }
 * @endcode
 */
ECS_STRUCT(TexWarp, {
    ecs_entity_t coords;  /**< Vector input of the coordinates to displace, 0 for the coordinates
                           *   of the texel in the domain of the graph */
    ecs_entity_t offset;  /**< Vector input of the displacement centred at 0.5, 0 for
                           *   offset_value */
    ecs_entity_t amount;  /**< Scalar input of the factor of the displacement, 0 for
                           *   amount_value */
    vec4 offset_value;    /**< Value of offset without a node; the default 0 moves by -0.5 *
                           *   amount, set {0.5, 0.5, 0.5} for no displacement */
    float amount_value;   /**< Value of amount without a node, default 0: no displacement */
});

/** Node of a TextureGraph that scales, rotates and translates coordinates, in that order:
 * rotate((coords - centre) * scale) + centre + translate, w kept.
 *
 * On DomainPlane the centre is the image centre (0.5, 0.5, 0) and the rotation turns (u, v)
 * about it from +u towards +v (clockwise in the image, whose v goes down), ignoring axis. The
 * result tiles for any translate, for whole number scales, and only for rotations by a
 * multiple of 90 degrees, which are exact; other angles leave seams at the image border.
 *
 * On DomainSphere the centre is the origin and the rotation is right handed about axis, so a
 * field computed from the result turns the other way over the sphere. A scale changes the
 * distance of the points from the origin, so 3D generators reading them see features scale
 * times smaller; a translate moves the points off the unit sphere through the 3D field of the
 * generators, which changes the pattern but keeps it seamless. Neither keeps unit length.
 *
 * The angle may come from a Scalar node, so a field spins when an animated value drives it.
 *
 * @code
 * turned {
 *   TexTransform: {angle_value: 90, scale: {2, 2, 1}}
 * }
 * @endcode
 */
ECS_STRUCT(TexTransform, {
    ecs_entity_t coords;  /**< Vector input of the coordinates to transform, 0 for the
                           *   coordinates of the texel in the domain of the graph */
    ecs_entity_t angle;   /**< Scalar input of the rotation in degrees, 0 for angle_value */
    float angle_value;    /**< Value of angle without a node, in degrees, default 0 */
    vec3 axis;            /**< DomainSphere: axis of the rotation, normalized; 0 for +y, the
                           *   poles. Ignored on DomainPlane */
    vec3 translate;       /**< Added to the coordinates after scale and rotation */
    vec3 scale;           /**< Factor of the coordinates about the centre, a 0 component for 1;
                           *   whole numbers keep the plane tiling */
});

/** Most keys of a TexRamp */
#define BOX_TEXTURE_RAMP_KEYS (32)

/** Most keys of the curve of a TexCurve */
#define BOX_TEXTURE_CURVE_KEYS (32)

/** Node of a TextureGraph that maps a scalar through a color ramp to a vector, with the keys
 * and the interpolation of TextureGen.colors: below the first key the color of the first,
 * above the last the color of the last, between two keys the display encoded colors mixed
 * linearly. Its value is the linear color of the result with alpha 1, so a GraphColor image
 * stores the display encoded colors of the ramp as a GenAlbedo TextureGen does; without keys
 * the input is a display encoded gray.
 *
 * Editing, adding or removing keys up to BOX_TEXTURE_RAMP_KEYS changes the parameters of the
 * graph and not its structure, more keys fail the graph.
 *
 * @code
 * // height is a node whose value is a Scalar
 * albedo { TexRamp: {input: height, keys: [{0, {20, 40, 90}}, {0.5, {200, 180, 120}}]} }
 * @endcode
 */
ECS_STRUCT(TexRamp, {
    ecs_entity_t input;          /**< Scalar input node, 0 for input_value */
    float input_value;           /**< Value of input without a node */
    ecs_vec(TextureGenKey) keys; /**< Keys in order of position, display encoded colors */
});

/** Node of a TextureGraph that maps a scalar through a curve of box/curve.h, the input being
 * the time: the value of box_curve_value of the curve at the input, the input itself without
 * keys. The controls of the spans are computed as the engine computes them on the CPU and the
 * cubics evaluated per texel.
 *
 * Editing, adding or removing keys up to BOX_TEXTURE_CURVE_KEYS changes the parameters of the
 * graph and not its structure, more keys or an invalid curve (box_curve_validate) fail the
 * graph.
 *
 * @code
 * bumps { TexCurve: {input: height, curve: {kind: CurveCatmullRom, keys: [{0, 0}, {1, 0.6}]}} }
 * @endcode
 */
ECS_STRUCT(TexCurve, {
    ecs_entity_t input;          /**< Scalar input node, 0 for input_value */
    float input_value;           /**< Value of input without a node */
    Curve curve;                 /**< The curve */
});

/** Node of a TextureGraph of courses of blocks or planks, as a GenBlocks layer of TextureGen
 * makes them: the same parameters and seeds give the same blocks, ids and tones.
 *
 * Its value is (mask, id, tone, random): mask 1 on a face and 0 in the joints, beveled; id the
 * random id of the block in 0..1 as GenBlocks gives it; tone the tone level of the block,
 * tone / (tones - 1), 0 with fewer than two tones; random a second random number of the block
 * in 0..1, independent of id. Lengths are fractions of the width of the image, the courses
 * repeat down v and the cuts wrap along u, so it tiles over 0..1.
 *
 * DomainPlane only: on DomainSphere it fails the graph.
 *
 * @code
 * ashlar {
 *   blocks { TexBlocks: {courses: 8, per_course: 4, gap: 0.006, jitter: 0.22, stagger: 0.5,
 *                        seed: 11} }
 *   TextureGraph: {output: blocks, format: GraphLinear}
 * }
 * @endcode
 */
ECS_STRUCT(TexBlocks, {
    ecs_entity_t coords;    /**< Vector input node of the (u, v) the blocks are evaluated at, 0
                             *   for the coordinates of the domain */
    int32_t courses;        /**< Courses down the texture, default 8 */
    int32_t per_course;     /**< Blocks along a course, default 4 */
    float gap;              /**< Width of the joints as a fraction of the width */
    float jitter;           /**< Random shift of each cut, up to +-jitter blocks */
    float stagger;          /**< Shift of every second course in blocks */
    float softness;         /**< Width of the bevel from a joint to the face as a fraction of the
                             *   width, default 1.3 times gap; with neither a hard edge */
    int32_t tones;          /**< Number of tones a block takes, 0 for none */
    TextureGenToneOrder tone_order; /**< Order of the tones, default GenToneRandom */
    int32_t seed;           /**< Seed, as TextureGen.seed */
    int32_t layer_seed;     /**< Seed mixed with seed, as TextureGenLayer.seed */
});

/** What the x of a TexCells node measures */
ECS_ENUM(TextureCellsDistance, {
    CellsNearest = 0, /**< Distance to the nearest point (F1) in cell sizes: 0 at the point, the
                       *   default */
    CellsSecond = 1,  /**< Distance to the second nearest point (F2) in cell sizes */
    CellsEdge = 2     /**< F2 - F1 through a smoothstep over edge: 0 on the border between two
                       *   cells, 1 inside, as GenEdge */
});

/** Node of a TextureGraph with Voronoi cells of jittered lattice points: a Vector of the
 * distance of distance in x, a random id in 0..1 of the cell in y and two more random values
 * of the cell in z and w, for tones and per cell variation; each is the same over a cell and
 * stable for a seed.
 *
 * On DomainPlane the cells are a periodic lattice of about count cells over the image, so it
 * tiles; distances are in cell sizes as TextureGen measures them, the square root of the area
 * of a cell in pixels. On DomainSphere they are 3D cells evaluated at the coordinates, scaled
 * so about count cells cover the unit sphere, seamless; distances are in 3D lattice cells.
 *
 * Each point moves within its cell, back and forth at its own speed, as evolve changes, so a
 * time on evolve animates the cells continuously; 0 keeps the points where the seed puts them.
 *
 * @code
 * stones {
 *   cells  { TexCells: {count: 100, distance: CellsEdge, edge: 0.15, seed: 3} }
 *   TextureGraph: {output: cells, width: 512}
 * }
 * @endcode
 */
ECS_STRUCT(TexCells, {
    TextureCellsDistance distance; /**< What x measures, default CellsNearest */
    int32_t count;      /**< Approximate number of cells over the domain, 0 for 64 */
    float jitter;       /**< Random shift of the points, 0 for a regular lattice with the
                         *   points at the cell centers to 1 for anywhere in their cell; 1
                         *   when the component is added */
    int32_t seed;       /**< Seed of the points and the random values of the cells */
    float edge;         /**< Width of the smoothstep of CellsEdge in cell sizes, 0 for 0.25 */
    ecs_entity_t coords; /**< Vector input node of the coordinates, 0 for the coordinates of
                          *   the texel in the domain */
    ecs_entity_t evolve; /**< Scalar input node that moves the points, 0 for evolve_value */
    float evolve_value; /**< Value of evolve without a node: a point crosses its cell and back
                         *   in 1 to 2 units */
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

/** Addressing of the texels outside a texture a TexImage samples */
ECS_ENUM(TextureAddress, {
    AddressWrap = 0, /**< The texture repeats, the default. On DomainSphere only the longitude
                      *   wraps, the rows of an equirectangular image clamp at the poles */
    AddressClamp = 1 /**< The edge texels extend */
});

/** Node of a TextureGraph whose value samples the image of a texture entity: a TexturePath, a
 * TextureGen, another TextureGraph or the texture of a Surface, bilinear between its texels.
 *
 * On DomainPlane it samples at the uv of coords, v 0 at the top; on DomainSphere at the
 * equirectangular uv of the direction of coords, so a graph of the size of an equirectangular
 * image reads it back texel by texel. A single channel image reads as (r, 0, 0, 1).
 *
 * The graph waits in TextureLoading until the texture has an image, fails when it fails or
 * samples the graph back, and evaluates after a graph it samples and again when its image
 * changes. Rebinding the node to another texture compiles nothing.
 *
 * @code
 * tinted {
 *   albedo { TexImage: {texture: brick} }
 *   warm   { TexMath: {op: MathMultiply, a: albedo, b_value: {1, 0.9, 0.8, 1}} }
 *   TextureGraph: {output: warm, width: 512}
 * }
 * @endcode
 */
ECS_STRUCT(TexImage, {
    ecs_entity_t texture;   /**< Texture entity sampled */
    ecs_entity_t coords;    /**< Vector input node of where to sample, 0 for the coordinates
                             *   of the texel in the domain of the graph */
    TextureAddress address; /**< Texels outside the image, default AddressWrap */
    bool linear;            /**< Read the color channels as stored, default off: RGBA8 is
                             *   decoded from display encoding as color layers do, so a
                             *   GraphColor image reads as its output; R8 and HDR as stored */
});

/** Node of a TextureGraph that looks up a texture entity as a 2D table by two scalars, such as
 * a biome color by height and temperature, bound as the texture of a TexImage.
 *
 * u 0..1 runs from the center of the first column of the image to the center of the last one,
 * v from the top row to the bottom row, bilinear between them and clamped outside. An input
 * without a node takes its value member instead.
 */
ECS_STRUCT(TexLut, {
    ecs_entity_t texture;   /**< Texture entity of the table */
    ecs_entity_t u;         /**< Scalar input node of the column, 0 for u_value */
    ecs_entity_t v;         /**< Scalar input node of the row, 0 for v_value */
    float u_value;          /**< Value of u without a node */
    float v_value;          /**< Value of v without a node */
    bool linear;            /**< Read the color channels as stored, see TexImage.linear */
});

/** Most inputs of a node kind */
#define BOX_TEXTURE_NODE_INPUTS (4)

/** Most parameters of a node kind */
#define BOX_TEXTURE_NODE_PARAMS (16)

/** Most bytes of the parameters of a node kind in the parameter buffer */
#define BOX_TEXTURE_NODE_PARAM_BYTES (64)

/** Most elements of the array parameter of a node kind */
#define BOX_TEXTURE_NODE_ARRAY_MAX (256)

/** Flag of a node kind that reads its inputs at other texels, so they are materialized */
#define BOX_TEXTURE_NODE_NEIGHBOURHOOD (1u << 0)

/** Flag of a node kind whose value depends on time */
#define BOX_TEXTURE_NODE_ANIMATED (1u << 1)

<<<<<<< HEAD
/** Flag of a node kind that samples a texture entity, set by box_texture_node_register for a
 * kind with a texture member (box_texture_node_desc_t.texture) */
#define BOX_TEXTURE_NODE_SAMPLES (1u << 2)
=======
/** Flag of a node kind that works on DomainPlane only, failing a graph on DomainSphere */
#define BOX_TEXTURE_NODE_PLANE (1u << 2)

/** Most passes of a node kind */
#define BOX_TEXTURE_NODE_PASSES (4)
>>>>>>> e676cfb85f792e412629ba6f2a7b7793c8e00917

/** An input port of a node kind, as TextureNodeKind lists it */
ECS_STRUCT(TextureNodePort, {
    const char *name;      /**< Member of the ecs_entity_t of the input node */
    TextureValueType type; /**< Type the input node must output */
    bool fallback;         /**< The input has a constant member used without a node */
    const char *value;     /**< Name of that constant member, NULL without one */
    bool coords;           /**< Without a node the input is the coordinates of the texel */
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
    TextureNodeParam params[16]; /**< The BOX_TEXTURE_NODE_PARAMS parameters, in order */
    int32_t param_size;      /**< Bytes of the parameters of a node, a multiple of 16 */
    const char *array;       /**< Member of the array parameter, NULL for none */
    int32_t array_max;       /**< Most elements of the array parameter */
    int32_t array_header;    /**< Byte offset in the parameters of the vec4u of the array:
                              *   (vec4 index of its first element in the parameter buffer,
                              *   element count, 0, 0) */
    int32_t types_offset;    /**< Byte offset of the types word of a kind with a TexAny input,
                              *   see box_texture_node_desc_t, -1 without */
    const char *function;    /**< Name of the WGSL function of the node */
    bool neighbourhood;      /**< The node reads its inputs at other texels */
    bool animated;           /**< The node depends on time */
<<<<<<< HEAD
    const char *texture;     /**< Member of the texture entity a node samples, NULL for a kind
                              *   that samples none */
=======
    bool plane;              /**< The node works on DomainPlane only */
    int32_t passes;          /**< Passes that compute the value, 1 for most kinds */
>>>>>>> e676cfb85f792e412629ba6f2a7b7793c8e00917
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
<<<<<<< HEAD
    const char *value;          /**< Constant member used without a node, a float for
                                 *   TexScalar or a vec4 for TexVector; NULL for none, then a
                                 *   TexVector input takes the coordinates, a TexScalar one 0 */
=======
    const char *value;          /**< Name of the constant member used without a node, a float
                                 *   for TexScalar or a vec4 for TexVector and TexAny, NULL for
                                 *   none */
>>>>>>> e676cfb85f792e412629ba6f2a7b7793c8e00917
    ecs_size_t value_offset;    /**< Offset of that member */
    bool coords;                /**< A TexVector input without value: without a node it is the
                                 *   coordinates of the texel in the domain of the graph */
} box_texture_node_input_t;

/** A parameter of a node kind: a member copied into the parameter buffer */
typedef struct box_texture_node_param_t {
    const char *name;           /**< Name of the member, NULL ends the list */
    ecs_size_t offset;          /**< Offset of the member */
    ecs_size_t size;            /**< Bytes of the member: 4, 8, 12 or 16 */
    box_texture_param_kind_t kind; /**< Kind of its elements */
} box_texture_node_param_t;

/** Pack the elements of the array parameter of a node into the parameter buffer.
 *
 * @param node The node component
 * @param count Number of elements, at most the max of the array
 * @param out Receives a vec4 of 32 bit words per element, zeroed before the call
 * @return Why the elements are invalid, freed with ecs_os_free, NULL when they are valid
 */
typedef char* (*box_texture_node_pack_t)(
    const void *node,
    int32_t count,
    uint32_t *out);

/** The array parameter of a node kind: the elements of an ecs_vec_t member, a variable number
 * of values such as the keys of a ramp.
 *
 * A node reserves max vec4 elements after the constants of its inputs, so the number of
 * elements changes the parameter bytes and never the structure of the graph; more than max
 * elements fail the graph. A vec4u after the parameters, at TextureNodeKind.array_header, holds
 * the index of the first element in box_texgraph_params and the element count.
 */
typedef struct box_texture_node_array_t {
    const char *name;           /**< Name of the ecs_vec_t member, NULL for none */
    ecs_size_t offset;          /**< Offset of the ecs_vec_t member */
    ecs_size_t size;            /**< Bytes of an element, copied into its vec4 without pack, 4
                                 *   to 16 */
    int32_t max;                /**< Most elements, 1 to BOX_TEXTURE_NODE_ARRAY_MAX */
    box_texture_node_pack_t pack; /**< Writes the elements, NULL to copy them */
} box_texture_node_array_t;

/** Type of the value of a node of a kind whose output is TexAny.
 *
 * The default, without a function or when it returns TexAny, is a Vector when an input node
 * is a Vector, else a Scalar when one is a Scalar, else a Vector.
 *
 * @param node The node component
 * @param inputs Type of the node of each input in order, TexAny for an input without a node
 * @return The type, or TexAny for the default
 */
typedef TextureValueType (*box_texture_node_type_t)(
    const void *node,
    const TextureValueType *inputs);

/** Description of a node kind for box_texture_node_register.
 *
 * The WGSL function fn name(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f
 * of the parameter bytes p, the input values i (a scalar in x, the constant without a node)
 * and the coordinates returns the value of the node, a scalar in x.
 *
<<<<<<< HEAD
 * A neighbourhood kind also gets the texel (vec2i) and the texture of each input, a sampling
 * kind (texture) the texture_2d<f32> it samples last, read with box_texgraph_sample. It
 * returns the value of the node, a scalar in x.
=======
 * A kind with an array parameter reads its elements from box_texgraph_params at the index in
 * its header, see box_texture_node_array_t.
 *
 * A kind with a TexAny input gets a u32 after its last parameter, 4 byte aligned, with bit j
 * set when input j has a Scalar node and bit 4 + j when it has a Vector node; its function
 * broadcasts a Scalar input itself.
 *
 * A neighbourhood kind also gets texel: vec2i, then t0: texture_2d<f32>, ... per input. A kind of
 * more passes gets pass_index: u32 and prev: texture_2d<f32> (the pass before, 1x1 zero in pass
 * 0) between texel and t0; its last pass is the value of the node.
>>>>>>> e676cfb85f792e412629ba6f2a7b7793c8e00917
 */
typedef struct box_texture_node_desc_t {
    ecs_entity_t component;     /**< Component of the nodes of the kind */
    TextureValueType output;    /**< Type of the value of the node, TexAny when it follows the
                                 *   inputs */
    box_texture_node_type_t output_type; /**< Type of a node of a kind whose output is TexAny,
                                          *   NULL for the default */
    box_texture_node_input_t inputs[BOX_TEXTURE_NODE_INPUTS]; /**< Inputs in order */
    box_texture_node_param_t params[BOX_TEXTURE_NODE_PARAMS]; /**< Parameters in order */
    box_texture_node_array_t array; /**< Array parameter, name NULL for none */
    const char *function;       /**< Name of the WGSL function */
    const char *source;         /**< WGSL source that defines the function */
<<<<<<< HEAD
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, BOX_TEXTURE_NODE_ANIMATED */
    const char *texture;        /**< ecs_entity_t member of the texture entity a node
                                 *   samples, NULL for none; which entity it is only binds the
                                 *   image, it is not part of the structure of the graph */
    ecs_size_t texture_offset;  /**< Offset of that member */
    const char *linear;         /**< Name of a bool member that reads the color channels of
                                 *   the texture as stored instead of decoded from display
                                 *   encoding, NULL to always decode them */
    ecs_size_t linear_offset;   /**< Offset of that member */
=======
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, BOX_TEXTURE_NODE_ANIMATED,
                                 *   BOX_TEXTURE_NODE_PLANE */
    int32_t passes;             /**< Passes of the function, 0 for 1, at most
                                 *   BOX_TEXTURE_NODE_PASSES; more than one makes the kind a
                                 *   neighbourhood kind */
>>>>>>> e676cfb85f792e412629ba6f2a7b7793c8e00917
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

/** Preview of the value of a node of a TextureGraph, which a tool such as tools/texgraph sets
 * on a node entity to see it. With the GPU backend the engine reads the value of the node back
 * at most rate times per second, scales it down to size and writes TextureNodePreviewStatus.
 *
 * Removing it stops the readbacks and removes the status. A node that no graph reaches has no
 * value to read, so it gets no status.
 */
ECS_STRUCT(TextureNodePreview, {
    int32_t size; /**< Largest dimension of the preview in pixels, 0 for 128 */
    float rate;   /**< Most readbacks per second, 0 for 4 */
});

/** Preview of a node written by the engine for its TextureNodePreview, read only for
 * applications: an opaque RGBA8 PNG of its value as the image of its graph would store it, a
 * scalar in gray. It is encoded again only when the value changed. */
ECS_STRUCT(TextureNodePreviewStatus, {
    int32_t width;    /**< Width of the preview in pixels */
    int32_t height;   /**< Height of the preview in pixels */
    int32_t revision; /**< Previews encoded, it increments when the value changed */
    char *png;        /**< The PNG file in base64 */
});

/** Statistics of the evaluation of the texture graphs on the GPU backend, a singleton the
 * engine writes when they change, read only for applications */
ECS_STRUCT(TextureGraphStats, {
    int32_t graphs;      /**< Texture graphs alive on the backend */
    int32_t programs;    /**< Generated stage programs alive, shared by graphs of one structure */
    int32_t evaluations; /**< Evaluations of texture graphs recorded */
    int32_t dispatches;  /**< Stage dispatches recorded */
    int64_t texels;      /**< Texels the stage dispatches computed */
    double gpu_ms;       /**< GPU time of the evaluations measured, 0 without timestamp
                          *   queries */
    int32_t timed;       /**< Packets of evaluations whose GPU time gpu_ms holds */
    int64_t pool_bytes;  /**< Bytes of the pooled intermediate textures now */
    int32_t reads;       /**< Readbacks delivered, previews included */
    int32_t previews;    /**< Nodes with a TextureNodePreview */
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
