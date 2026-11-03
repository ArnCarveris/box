/**
 * @file box/texture.h
 * @brief Texture components: images loaded from files on the job system.
 *
 * A TexturePath loads a raster image (stb_image) or an SVG (resvg) on a worker thread; the
 * engine then writes Texture and TextureStatus, and PbrTextures (box/material.h) samples it.
 *
 * A TextureGen generates a seamless height, albedo or normal map from noise, blocks and cells
 * instead, on the job system as well.
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
    GenBlocks = 1, /**< Courses of blocks or planks: 1 on a face, 0 in the gaps, beveled */
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

/** What a TextureGen stores */
ECS_ENUM(TextureGenOutput, {
    GenHeight = 0, /**< The height field as a single channel texture (R8Unorm), the default */
    GenAlbedo = 1, /**< The height field mapped through the color ramp, RGBA8, opaque */
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
    float amount;           /**< Weight of the pattern; a layer with neither amount nor id_amount
                             *   weighs 1. May be negative */
    float id_amount;        /**< Weight of the random per block or per cell id in 0..1, which
                             *   varies the stones; GenNoise has no id */
    int32_t seed;           /**< Mixed with TextureGen.seed for the randomness of the layer */
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
 * It tiles and gives the same bytes on every platform. Surface.texture_quality caps its size; a
 * change of it or its source generates it again, keeping the previous image until the new one is
 * ready. A TextureGen and a TexturePath on one entity is an error.
 *
 * A source that exists without a TextureGen yet, such as one declared first and given its
 * TextureGen later in the script, keeps the texture in TextureLoading; it fails at the next frame
 * when none arrived. A source that loses its TextureGen or is deleted fails it the same way.
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
 */
ECS_STRUCT(TextureGen, {
    int32_t width;                  /**< Width in pixels, 0 for 512; ignored with a source */
    int32_t height;                 /**< Height in pixels, 0 for the width; ignored with a source */
    int32_t seed;                   /**< Seed of the randomness of every layer */
    ecs_vec(TextureGenLayer) layers; /**< Layers of the height field, in order */
    ecs_entity_t source;            /**< Generator whose height field this one derives from, 0
                                     *   to use its own layers */
    TextureGenOutput output;        /**< What is stored, default GenHeight */
    ecs_vec(TextureGenKey) colors;  /**< Color ramp of GenAlbedo by height; gray without keys */
    float strength;                 /**< Steepness of GenNormal, default 1: the slope is strength
                                     *   times the change of height per pixel of a 512 pixel wide
                                     *   texture, whatever its size */
});

/** Progress of the load of a TexturePath or a TextureGen */
ECS_ENUM(TextureState, {
    TextureLoading = 0, /**< A load is in flight */
    TextureReady = 1,   /**< The last load succeeded and Texture holds its size */
    TextureFailed = 2   /**< The last load failed, error says why */
});

/** Outcome of the loads of a TexturePath or a TextureGen, written by the engine and read only
 * for applications */
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
