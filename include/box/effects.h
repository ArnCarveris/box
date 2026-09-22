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

ECS_ENUM(EffectSetupKind, {
    EffectSetupNone = 0,
    EffectSetupCompute = 1,
    EffectSetupMipChain = 2,
    EffectSetupTemporal = 3
});

ECS_ENUM(EffectTextureFormat, {
    EffectTextureRg16Float = 0,
    EffectTextureRgba16Float = 1
});

ECS_STRUCT(EffectSetup, {
    ecs_entity_t shader;
    EffectSetupKind kind;
    EffectTextureFormat format;
    int32_t parameter0;
    int32_t parameter1;
    int32_t parameter2;
    int32_t parameter3;
    int32_t workgroup_size;
    int32_t buffer0_size;
    int32_t buffer1_size;
    vec4 initial;
});

ECS_STRUCT(EffectTexture, {
    char *path;
    int32_t parameter;
    float value;
});

/** One post process pass of the graph, with the inputs it reads */
ECS_STRUCT(Effect, {
    ecs_entity_t shader; /**< Shader entity of the pass, without one it is skipped */
    ecs_entity_t params;
    ecs_entity_t setup;
    ecs_entity_t texture;
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
