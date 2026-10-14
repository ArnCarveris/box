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

/** Opacity from 0 (invisible) to 1 (opaque), default 1.
 *
 * Outside a UI tree the opacity multiplies into the alpha of every shape below the
 * entity, which needs no shape of its own, so one Opacity on a node fades a hierarchy.
 *
 * A shape whose resulting alpha is under 1 is drawn by the sorted blended pass and
 * casts no shadow; at 1 it stays in its opaque batch. In a UI tree an element with
 * children composites its subtree with the opacity instead.
 */
ECS_STRUCT(Opacity, {
    float value; /**< Opacity */
});

/** Color the surface mixes toward, composed with the tints down the hierarchy */
ECS_STRUCT(Tint, {
    uint8_t r;  /**< Red of the tint color */
    uint8_t g;  /**< Green of the tint color */
    uint8_t b;  /**< Blue of the tint color */
    uint8_t a;  /**< Tint strength, 0 leaves the color and 255 replaces it */
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
    BackFaceCull = 0,     /**< Nothing, the faces are culled */
    BackFacePlain = 1     /**< The material constants without any texture */
});

/** What the samplers of a shape's PbrTextures do with UVs outside the 0..1 range */
ECS_ENUM(TextureWrap, {
    WrapRepeat = 0, /**< The texture tiles */
    WrapClamp = 1,  /**< The edge texel is held */
    WrapMirror = 2  /**< Every other tile is mirrored */
});

/** Scale and offset applied to the UVs of a material, defaults to 1, 1, 0, 0.
 *
 * A scale of 4 tiles the texture four times across the shape (with WrapRepeat); the offset
 * shifts the texture after the scale, in texture widths. UV 0, 0 is the top left of an image.
 */
ECS_STRUCT(TextureTransform, {
    float scale_x;  /**< U multiplier */
    float scale_y;  /**< V multiplier */
    float offset_x; /**< U addend, applied after the scale */
    float offset_y; /**< V addend, applied after the scale */
});

/** Texture entities for each PBR layer, 0 for none, inherited by instances.
 *
 * A texture entity has a TexturePath (box/texture.h); until it loaded, or when it is missing,
 * the layer has no effect. Back faces drawn with BackFacePlain do not sample the textures.
 */
ECS_STRUCT(PbrTextures, {
    ecs_entity_t albedo;    /**< Multiplied with the base color and opacity, display encoded */
    ecs_entity_t emissive;  /**< Multiplied with the Emissive color times its strength */
    ecs_entity_t roughness; /**< Multiplied with the roughness, green channel */
    ecs_entity_t normal;    /**< Tangent space normal map, green points up the image */
});

/** Material properties change every frame, applies to descendants */
extern ECS_TAG_DECLARE(DynamicMaterial);

/** Keeps an entity and its descendants out of the shadow pass of every view */
extern ECS_TAG_DECLARE(NoShadow);

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
