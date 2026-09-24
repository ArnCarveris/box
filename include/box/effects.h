/**
 * @file box/effects.h
 * @brief Effect components: the post process passes a view chains together.
 *
 * An Effect is a fullscreen shader pass with the inputs it reads and a parameter
 * component. An EffectSetup runs a helper stage before the pass and an
 * EffectTexture gives the pass a 3D lookup table.
 */

#ifndef BOX_EFFECTS_H
#define BOX_EFFECTS_H

#include "types.h"

#ifdef BOX_EFFECTS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Stage an EffectSetup runs before its pass */
ECS_ENUM(EffectSetupKind, {
    EffectSetupNone = 0,     /**< No setup stage */
    EffectSetupCompute = 1,  /**< Compute shader over the input with a work and a state buffer */
    EffectSetupMipChain = 2, /**< Mip pyramid of the input, filtered by the setup shader */
    EffectSetupTemporal = 3  /**< Filter that blends the input with a history texture */
});

/** Texture format of the mip chain or the history of a setup */
ECS_ENUM(EffectTextureFormat, {
    EffectTextureRg16Float = 0,  /**< Two 16 bit float channels */
    EffectTextureRgba16Float = 1 /**< Four 16 bit float channels */
});

/** Helper stage an effect runs before its pass
 *
 * A parameter member indexes the float in the effect parameter component that steers the stage,
 * or -1 for none. A mip chain reads weight, maximum size, history and full resolution first mip
 * from parameter0 to parameter3; a temporal filter halves its resolution when parameter0 is set.
 *
 * A mip chain reads the low frequency boost, its curvature and the high pass frequency that shape
 * the weight of every level from parameter4 on, and blends every level with the weight without.
 */
ECS_STRUCT(EffectSetup, {
    ecs_entity_t shader;        /**< Shader entity of the stage, without one it is skipped */
    EffectSetupKind kind;       /**< Stage the setup runs */
    EffectTextureFormat format; /**< Format of the textures the stage writes */
    int32_t parameter0;         /**< First parameter index of the stage */
    int32_t parameter1;         /**< Second parameter index of the stage */
    int32_t parameter2;         /**< Third parameter index of the stage */
    int32_t parameter3;         /**< Fourth parameter index of the stage */
    int32_t parameter4;         /**< Fifth parameter index of the stage, default -1 */
    int32_t workgroup_size;     /**< Compute workgroup edge in pixels, 0 takes 1 */
    int32_t buffer0_size;       /**< Bytes of the compute work buffer */
    int32_t buffer1_size;       /**< Bytes of the compute state buffer, 0 takes 16 */
    vec4 initial;               /**< Values the compute state buffer starts with */
});

/** 3D lookup table an effect samples, loaded from a BOXLUT3D file */
ECS_STRUCT(EffectTexture, {
    char *path;        /**< Path of the table file */
    int32_t parameter; /**< Parameter index that selects the table, -1 always uses it */
    float value;       /**< Value of that parameter for which the table is used */
});

/** One post process pass of the graph, with the inputs it reads */
ECS_STRUCT(Effect, {
    ecs_entity_t shader; /**< Shader entity of the pass, without one it is skipped */
    ecs_entity_t params;  /**< Component whose value the pass reads as its parameters */
    ecs_entity_t setup;   /**< EffectSetup entity that runs before the pass, 0 for none */
    ecs_entity_t texture; /**< EffectTexture entity the pass samples, 0 for none */
    float scale;         /**< Resolution scale of its output, 0 and 1 are the view size */
    bool color;          /**< The pass reads the image so far, default true */
    bool depth;          /**< The pass reads the depth target of the view */
    bool normals;        /**< The pass reads the world normals of the view */
    bool motion;         /**< The pass reads the motion vectors of the view */
});

#define BOX_EFFECTS_BUILTINS "etc/box/effects.flecs"

#define BOX_EFFECTS_MAX (8)

/** Register the box.effects module.
 *
 * @param world The world
 */
void BoxEffectsImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
