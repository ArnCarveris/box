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

/** Slot of the material table a shape renders with, set by the engine and read only for
 * applications, inherited by instances.
 *
 * Rgb, PbrMaterial, Emissive, TextureTransform and AlphaCutoff bring it along, so the owner of a
 * material component owns a MaterialId. A prefab that owns one gets a slot its instances share, a
 * 3D shape that owns one a slot of its own, composed of what it owns and what it still inherits.
 *
 * Slot 0 is the default material, of a shape without any material component. An owned id that
 * was never handed a slot (a light, a 2D shape, a text) is UINT32_MAX.
 */
ECS_STRUCT(MaterialId, {
    uint32_t value; /**< Slot of the material table */
});

/** Base color, display encoded bytes, inherited by instances.
 *
 * An instance of a prefab reads the color of the prefab until it sets its own, and a change of
 * the color of the prefab reaches every instance that does not set its own.
 */
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
 * casts no shadow; at 1 it stays in its opaque batch. A shape with AlphaCutoff tests the
 * alpha against its cutoff in the opaque pass instead. In a UI tree an element with
 * children composites its subtree with the opacity instead.
 */
ECS_STRUCT(Opacity, {
    float value; /**< Opacity */
});

/** Cutoff a constructed AlphaCutoff tests against */
#define BOX_MATERIAL_CUTOFF (0.5f)

/** Alpha test of a shape, default 0.5: its fragments are kept or discarded, not blended.
 * Inherited by instances, like Rgb.
 *
 * The alpha a fragment is tested with is the alpha the shape draws with without it (its
 * Opacity times the Opacity of its ancestors) times the alpha of its albedo texture (PbrTextures);
 * a fragment whose alpha is below value is discarded and the others are written as opaque. The
 * value is clamped to 0..1.
 *
 * A shape with AlphaCutoff whose BlendMode is Opaque or missing draws in the opaque pass whatever
 * its Opacity, writes depth and casts shadows with the same holes; the probe and reflection
 * captures see the holes too. With another BlendMode it stays in the sorted blended pass and
 * casts no shadow, and the cutoff discards there as well. A back face drawn with BackFacePlain
 * keeps the holes of the albedo texture.
 *
 * Only 3D meshes are alpha tested; 2D shapes and text ignore AlphaCutoff. Picking does not read
 * textures, so an alpha tested shape is picked by its whole geometry, holes included.
 */
ECS_STRUCT(AlphaCutoff, {
    float value; /**< Alpha below which a fragment is discarded */
});

/** Color the display encoded base color (Rgb) mixes toward by the strength in a, before lighting.
 *
 * It is not transparency (Opacity) and applies to every shape below it through ChildOf and
 * Parent, composed from the root down: a child's tint mixes the result of its ancestors'. An
 * albedo texture multiplies the tinted color; emission is not tinted.
 */
ECS_STRUCT(Tint, {
    uint8_t r;  /**< Red of the tint color */
    uint8_t g;  /**< Green of the tint color */
    uint8_t b;  /**< Blue of the tint color */
    uint8_t a;  /**< Tint strength, 0 leaves the color and 255 replaces it */
});

/** Metallic of a surface without PbrMaterial and of a constructed PbrMaterial: a dielectric */
#define BOX_MATERIAL_METALLIC (0.0f)

/** Roughness of a surface without PbrMaterial and of a constructed PbrMaterial: matte, as in
 * glTF. A smooth or glossy surface sets it explicitly. */
#define BOX_MATERIAL_ROUGHNESS (1.0f)

/** Physically based surface parameters, default metallic 0 and roughness 1 (matte), inherited
 * by instances, like Rgb.
 *
 * A field a scene does not set keeps its default, so a PbrMaterial that only sets metallic is as
 * rough as a shape without PbrMaterial.
 */
ECS_STRUCT(PbrMaterial, {
    float metallic;  /**< 0 dielectric to 1 metal */
    float roughness; /**< 0 mirror to 1 fully diffuse */
});

/** Emitted light, inherited by instances, like Rgb. Tints do not apply to it. */
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

/** Scale and offset applied to the UVs of a material, defaults to 1, 1, 0, 0, inherited by
 * instances, like Rgb.
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

/** Texture entities for each PBR layer, 0 for none, inherited by instances like Rgb. They are
 * part of the batch of a shape, not of the material table, so PbrTextures brings no MaterialId.
 *
 * A texture entity has a TexturePath (box/texture.h), or is the texture of a Surface, whose
 * image of the same frame it then samples; until it loaded, or when it is missing, the layer
 * has no effect. Back faces drawn with BackFacePlain do not sample the textures, except for the
 * albedo alpha of a shape with AlphaCutoff.
 *
 * The ORM layer holds occlusion (red), roughness (green) and metallic (blue) in one image, as
 * glTF's occlusion and metallic roughness textures share one: green multiplies
 * PbrMaterial.roughness, blue multiplies PbrMaterial.metallic and red is the ambient occlusion
 * of the indirect light, 1 for unoccluded. Its values are stored as they are, not display
 * encoded; a missing layer is white, which leaves the material as it is.
 *
 * Minified albedo and emissive textures average linear light, the data layers (ORM and normal)
 * the stored values; a texture shared by a color and a data layer is filtered as color in both.
 */
ECS_STRUCT(PbrTextures, {
    ecs_entity_t albedo;    /**< Multiplied with the base color and opacity, display encoded;
                             *   its alpha only shows with a blend mode or AlphaCutoff */
    ecs_entity_t emissive;  /**< Multiplied with the Emissive color times its strength */
    ecs_entity_t orm;       /**< Occlusion (red), roughness (green) and metallic (blue), stored */
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
