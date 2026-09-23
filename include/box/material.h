/**
 * @file box/material.h
 * @brief Material components: color, opacity, tint, PBR parameters and emission.
 *
 * The box.material module resolves tints through the hierarchy and registers
 * the material layers a scene applies on top of the sampled surface.
 */

#ifndef BOX_MATERIAL_H
#define BOX_MATERIAL_H

#include "types.h"

#ifdef BOX_MATERIAL_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Base color, display encoded bytes */
ECS_STRUCT(Rgb, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
});

/** Opacity from 0 (invisible) to 1 (opaque), default 1 */
ECS_STRUCT(Opacity, {
    float value; /**< Opacity */
});

/** Blur of the scene seen through a surface, which makes the surface transparent */
ECS_STRUCT(BackdropBlur, {
    float radius; /**< Blur radius, 0 or less disables the blur */
});

/** Color the surface mixes toward, composed with the tints down the hierarchy */
ECS_STRUCT(Tint, {
    uint8_t r;  /**< Red of the tint color */
    uint8_t g;  /**< Green of the tint color */
    uint8_t b;  /**< Blue of the tint color */
    uint8_t a;  /**< Tint strength, 0 leaves the color and 255 replaces it */
});

/** Tint of an entity composed with those of its ancestors, written by the engine.
 *
 * A chain of tints resolves to one color and strength: the strength is one minus the product of
 * what every level leaves and the color is the summed contributions divided by it. Views mix the
 * surface color toward it by the strength; transparency stays with Opacity.
 */
ECS_STRUCT(ActualTint, {
    uint8_t r;  /**< Red of the composed color */
    uint8_t g;  /**< Green of the composed color */
    uint8_t b;  /**< Blue of the composed color */
    uint8_t a;  /**< Composed strength, 1 - the product of the strengths left */
});

/** Physically based surface parameters, default roughness 0.5 */
ECS_STRUCT(PbrMaterial, {
    float metallic;  /**< 0 dielectric to 1 metal */
    float roughness; /**< 0 mirror to 1 fully diffuse */
});

/** Emitted light */
ECS_STRUCT(Emissive, {
    float strength; /**< Emission intensity */
    Rgb color;      /**< Emission color */
});

/** Equation the fragments of a material are combined with, default Opaque */
ECS_ENUM(BlendMode, {
    Opaque = 0,             /**< No blending, the fragment replaces the target */
    AlphaBlend = 1,         /**< Source over, the fragment is weighted by its alpha */
    PremultipliedAlpha = 2, /**< Source over with a color that already holds its alpha */
    Additive = 3,           /**< The fragment is added to the target */
    Multiply = 4,           /**< The fragment multiplies the target */
    Screen = 5,             /**< Inverse multiply, the target is brightened */
    Subtractive = 6,        /**< The fragment is subtracted from the target */
    Min = 7,                /**< The darker of the fragment and the target */
    Max = 8                 /**< The brighter of the fragment and the target */
});

/** What the faces pointing away from the camera show, default BackFaceCull */
ECS_ENUM(BackFaceMode, {
    BackFaceTextured = 0, /**< The artwork of the front with the normal flipped */
    BackFacePlain = 1,    /**< The material constants without any texture */
    BackFaceCull = 2      /**< Nothing, the faces are culled */
});

/** Projection a TextureTiling derives its UVs from */
ECS_ENUM(TilingShape, {
    TilingBox = 0,      /**< Planar per face, picked by the dominant axis of the local normal */
    TilingCylinder = 1, /**< Arc length around the local Y axis and height along it */
    TilingRadial = 2,   /**< Distance from the local Y axis and arc length around it */
    TilingMeters = 3    /**< Mesh UVs stretched over the world size */
});

/** What a sampler does with UVs outside the 0..1 range */
ECS_ENUM(TextureWrap, {
    WrapRepeat = 0, /**< The texture tiles */
    WrapClamp = 1,  /**< The edge texel is held */
    WrapMirror = 2  /**< Every other tile is mirrored */
});

/** Scale and offset applied to the UVs of a material, defaults to 1, 1, 0, 0 */
ECS_STRUCT(TextureTransform, {
    float scale_x;  /**< U multiplier */
    float scale_y;  /**< V multiplier */
    float offset_x; /**< U addend, applied after the scale */
    float offset_y; /**< V addend, applied after the scale */
});

/** Tiles textures at a physical size in meters instead of stretching them over the mesh */
ECS_STRUCT(TextureTiling, {
    float meters_x;     /**< World meters one tile spans along U */
    float meters_y;     /**< World meters one tile spans along V */
    bool fit;           /**< Round the tile count so the pattern ends on a tile boundary */
    bool allow_partial; /**< Keep the fractional tile count that fit would round away */
    int32_t min_tiles;  /**< Lower bound of the tile count on both axes, 0 for none */
    TilingShape shape;  /**< Projection the UVs come from */
});

/** Texture entities for each PBR layer, 0 for none, inherited by instances */
ECS_STRUCT(PbrTextures, {
    ecs_entity_t albedo;    /**< Multiplied with the base color, display encoded */
    ecs_entity_t emissive;  /**< Added, scaled by the emission strength */
    ecs_entity_t roughness; /**< Multiplied with the roughness, green channel */
    ecs_entity_t normal;    /**< Tangent space normal perturbation */
});

/** Material layers a scene can register, one bit each in the instance layer mask */
#define BOX_MATERIAL_LAYER_MAX (16)

/** Material layer a scene registers, a shader body applied on top of the surface */
ECS_STRUCT(MaterialLayer, {
    ecs_entity_t shader; /**< Shader entity whose WGSL file holds the body of the layer */
    int32_t order;       /**< Position among the layers, lower applies first */
});

#define BOX_MATERIAL_LAYERS_BUILTINS "etc/box/material_layers.flecs"

/** Scene global parameters of a material layer, read as p.a and p.b by its body */
ECS_STRUCT(LayerParams, {
    vec4 a; /**< First parameter vector */
    vec4 b; /**< Second parameter vector */
});

/** Per instance strength and variation of the material layers, quantized to a byte each */
ECS_STRUCT(MaterialLayerData, {
    float amount0; /**< Strength of the first registered layer from 0 to 1, default 1 */
    float amount1; /**< Strength of the second registered layer, default 1 */
    float amount2; /**< Strength of the third registered layer, default 1 */
    float amount3; /**< Strength of the fourth registered layer, default 1 */
    float amount4; /**< Strength of the fifth registered layer, default 1 */
    float amount5; /**< Strength of the sixth registered layer, default 1 */
    float seed;    /**< Noise seed from 0 to 1 that moves the pattern of the layers */
    float scale;   /**< Frequency multiplier of the layer patterns up to 4, default 1 */
});

/** Enables a registered layer on a material as (MaterialLayerOn, layer), inherited */
extern ECS_TAG_DECLARE(MaterialLayerOn);

/** Material properties may change every frame, applies to descendants */
extern ECS_TAG_DECLARE(DynamicMaterial);

/** Keeps an entity and its descendants out of the shadow pass of every view */
extern ECS_TAG_DECLARE(NoShadow);

/** Convert a display encoded byte to a linear float.
 *
 * @param value The byte
 * @return Linear intensity in 0..1
 */
float box_srgb_to_linear(
    uint8_t value);

/** Register the box.material module.
 *
 * @param world The world
 */
void BoxMaterialImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
