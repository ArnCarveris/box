/**
 * @file box/material.h
 * @brief Material components: color, opacity, tint, PBR parameters and emission.
 *
 * The box.material module resolves tints through the hierarchy.
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

/** Color multiplier, also applies to descendants that have no tint of their own */
ECS_STRUCT(Tint, {
    uint8_t r; /**< Red multiplier */
    uint8_t g; /**< Green multiplier */
    uint8_t b; /**< Blue multiplier */
    uint8_t a; /**< Opacity multiplier */
});

/** Resolved tint written by the engine from Tint on the entity or an ancestor */
ECS_STRUCT(ActualTint, {
    uint8_t r; /**< Red multiplier */
    uint8_t g; /**< Green multiplier */
    uint8_t b; /**< Blue multiplier */
    uint8_t a; /**< Opacity multiplier */
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

/** Material properties may change every frame, applies to descendants */
extern ECS_TAG_DECLARE(DynamicMaterial);

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
