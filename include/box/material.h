/**
 * @file box/material.h
 * @brief Material components: color, opacity, tint, PBR parameters, clear coat, anisotropy, sheen,
 * diffuse transmission, emission and transmission.
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
 * Rgb, PbrMaterial, ClearCoat, SpecularAnisotropy, Sheen, DiffuseTransmission, Emissive,
 * Transmission, TextureTransform, TextureMapping and AlphaCutoff bring it along. A prefab that
 * owns one gets a slot its instances share, a 3D shape that owns one a slot of what it owns and
 * still inherits.
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
 * Outside a UI tree it multiplies into the alpha of every shape below the entity, which needs
 * no shape of its own, so one Opacity on a node fades a hierarchy. In a UI tree an element with
 * children composites its subtree with the opacity instead.
 *
 * A shape whose resulting alpha is under 1 is drawn by the sorted blended pass and casts no
 * shadow; at 1 it stays in its opaque batch. A shape with AlphaCutoff tests the alpha against
 * its cutoff in the opaque pass instead. A shape with Transmission ignores it.
 */
ECS_STRUCT(Opacity, {
    float value; /**< Opacity */
});

/** Cutoff a constructed AlphaCutoff tests against */
#define BOX_MATERIAL_CUTOFF (0.5f)

/** Alpha test of a shape, default 0.5: its fragments are kept or discarded, not blended.
 * Inherited by instances, like Rgb. Only 3D meshes are alpha tested, 2D shapes and text ignore
 * it; picking does not read textures, so a shape is picked by its whole geometry, holes included.
 *
 * A fragment is tested with the alpha the shape draws with without it (its Opacity times that of
 * its ancestors) times the alpha of its albedo texture (PbrTextures). Below value, clamped to
 * 0..1, it is discarded; otherwise it is written as opaque.
 *
 * With an Opaque or missing BlendMode it draws in the opaque pass whatever its Opacity, with its
 * holes in depth, shadows, captures and BackFacePlain back faces; otherwise it discards in the
 * sorted blended pass and casts no shadow. A shape with Transmission ignores it.
 */
ECS_STRUCT(AlphaCutoff, {
    float value; /**< Alpha below which a fragment is discarded */
});

/** Color the display encoded base color (Rgb) mixes toward by the strength in a, before lighting.
 *
 * It is not transparency (Opacity) and applies to every shape below it through ChildOf and
 * Parent, composed from the root down: a child's tint mixes the result of its ancestors'. An
 * albedo texture multiplies the tinted color; emission is not tinted.
 *
 * Light and reflection bakes ignore it: they capture the color without the tint, and changing a
 * tint rebakes nothing.
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

/** Strength of a constructed ClearCoat: a full coat */
#define BOX_MATERIAL_CLEAR_COAT_STRENGTH (1.0f)

/** Roughness of a constructed ClearCoat: a mirror like lacquer */
#define BOX_MATERIAL_CLEAR_COAT_ROUGHNESS (0.0f)

/** Thin dielectric layer over a 3D mesh (car paint, lacquer), glTF's KHR_materials_clearcoat,
 * default strength 1 and roughness 0, inherited like Rgb; a field a scene does not set keeps its
 * default, both clamp to 0..1. Only 3D meshes have a coat, 2D shapes and text ignore it.
 *
 * A dielectric of f0 0.04 on the geometric normal, not the normal map: direct light adds its GGX
 * lobe times Fc = F(l.h) * strength over 1 - Fc of the base, indirect light its reflection by its
 * split sum albedo over the rest. Emission is not attenuated; an unlit view ignores the coat.
 *
 * Transitionable (Transition): strength and roughness interpolate linearly by the timing
 * function, through a strength of 0 too; adding or removing ClearCoat does not animate.
 */
ECS_STRUCT(ClearCoat, {
    float strength;  /**< 0 no coat to 1 full coat */
    float roughness; /**< Roughness of the coat, 0 mirror to 1 fully diffuse */
});

/** Strength of a constructed SpecularAnisotropy: a fully stretched highlight */
#define BOX_MATERIAL_ANISOTROPY_STRENGTH (1.0f)

/** Rotation of a constructed SpecularAnisotropy: along the +U direction of the texture */
#define BOX_MATERIAL_ANISOTROPY_ROTATION (0.0f)

/** Highlight of a 3D mesh stretched along a direction in its surface (brushed metal, satin),
 * glTF's KHR_materials_anisotropy, default strength 1 and rotation 0, inherited like Rgb; a field
 * a scene does not set keeps its default. Only 3D meshes have it, 2D shapes and text ignore it.
 *
 * The base lobe only, not the clear coat: an anisotropic GGX lobe in direct light, its roughness
 * along the direction mixed toward 1 by strength squared, and in indirect light a reflection about
 * a normal bent toward the direction, as the glTF sample viewer does. An unlit view ignores it.
 *
 * Vertices have no tangents: the direction is +U of the screen space cotangent frame of the UVs,
 * turned by rotation, constant per triangle, so a continuous highlight needs a continuous UV
 * mapping. A triangle with degenerate (constant) UVs shades isotropic.
 *
 * Transitionable (Transition): strength and rotation interpolate linearly by the timing function,
 * through a strength of 0 too; the rotation takes no shortest path and wraps only when rendered.
 * Adding or removing SpecularAnisotropy does not animate.
 */
ECS_STRUCT(SpecularAnisotropy, {
    float strength; /**< 0 isotropic to 1 fully stretched, clamped to 0..1 */
    float rotation; /**< Radians counter clockwise from the +U direction of the texture, around
                     *   the normal, wrapped to 0..2 pi */
});

/** Roughness of a constructed Sheen: a rim halfway between tight and broad */
#define BOX_MATERIAL_SHEEN_ROUGHNESS (0.5f)

/** Soft bright rim at grazing angles of a 3D mesh (velvet, cloth, dust), glTF's
 * KHR_materials_sheen, default a white color and roughness 0.5, inherited like Rgb; a field a
 * scene does not set keeps its default. Only 3D meshes have it, 2D shapes and text ignore it.
 *
 * As three.js: direct light adds a Charlie lobe with the Neubelt visibility, indirect light the
 * diffuse light by a fit of the sheen albedo, and the base keeps 1 - 0.157 * max(color), with no
 * lookup table. It lies under the clear coat; emission is not scaled, an unlit view ignores it.
 *
 * Transitionable (Transition): color interpolates per channel like Rgb and roughness linearly by
 * the timing function, through a black color too; adding or removing Sheen does not animate.
 */
ECS_STRUCT(Sheen, {
    Rgb color;       /**< Color of the sheen, display encoded, black for none */
    float roughness; /**< 0 tight rim to 1 broad, clamped to 0..1 */
});

/** Factor of a constructed DiffuseTransmission: half of the diffuse light passes through */
#define BOX_MATERIAL_DIFFUSE_TRANSMISSION_FACTOR (0.5f)

/** Diffuse light passing through a thin 3D surface (leaf, paper, lampshade), glTF's
 * KHR_materials_diffuse_transmission, default factor 0.5, white, thickness 0 and blur 0,
 * inherited like Rgb; a field a scene does not set keeps its default. 2D shapes and text ignore
 * it.
 *
 * A Lambert lobe on the inverted normal of base color * color * factor, the front keeping 1 -
 * factor, none on metals. It stays in the opaque pass (depth, shadows, no snapshot), its shadow
 * is not tinted, and a single sided quad needs BackFacePlain to be seen from behind.
 *
 * The blur applies only to a shape that also has Transmission (frosted glass, a shower door):
 * what it shows of the opaque snapshot is blurred by the distance between its surface and what
 * is behind it, read from a depth copy of the snapshot, on top of the blur of its roughness. An
 * object touching the surface stays sharp and one further behind gets softer; the sky, with no
 * depth, takes the softest mip. A shape with DiffuseTransmission and no Transmission is opaque
 * and ignores the blur. Blended and transmissive shapes are not in the snapshot, so they are
 * neither seen nor blurred through it.
 *
 * Transitionable (Transition): factor, thickness and blur interpolate linearly by the timing
 * function and color per channel like Rgb, through a factor of 0 too; adding or removing
 * DiffuseTransmission does not animate.
 */
ECS_STRUCT(DiffuseTransmission, {
    float factor;    /**< 0 all diffuse light is reflected to 1 all of it passes through,
                      *   clamped to 0..1 */
    Rgb color;       /**< Tint of the light that passes through, display encoded */
    float thickness; /**< World units a ray travels through the surface for its shadow lookup,
                      *   0 for a thin sheet */
    float blur;      /**< World units of blur radius per world unit of distance behind the
                      *   surface (the tangent of the cone light spreads into) of a shape with
                      *   Transmission, 0 for none */
});

/** Emitted light, inherited by instances, like Rgb. Tints do not apply to it. */
ECS_STRUCT(Emissive, {
    float strength; /**< Emission intensity */
    Rgb color;      /**< Emission color */
});

/** Index of refraction of a constructed Transmission and of a surface without one */
#define BOX_MATERIAL_IOR (1.5f)

/** Attenuation distance of a constructed Transmission and of a surface without one: light is
 * not absorbed */
#define BOX_MATERIAL_ATTENUATION_DISTANCE (1e10f)

/** Light passing through a 3D shape (glass), inherited by instances like Rgb, default factor 0,
 * ior 1.5, thickness 0, attenuation distance 1e10 and a white attenuation color; a field a scene
 * does not set keeps its default.
 *
 * A shape with it, whatever its factor, draws in the sorted blended pass, back to front with the
 * alpha blended and additive shapes, without blending or depth writes. It casts no shadow, is not
 * in the probe, reflection or bake captures and ignores Opacity, BlendMode and AlphaCutoff.
 *
 * It shows the image of its view before the blended pass (no blended or transmissive shapes)
 * where the ray refracted by ior leaves thickness_factor times the scale, blurred by roughness,
 * absorbed toward attenuation_color and tinted by Rgb, with its own specular light on top.
 *
 * Transitionable (Transition): the four floats interpolate linearly by the timing function and
 * attenuation_color per channel like Rgb. The shape keeps its transmissive batch while its
 * factor moves, through 0 too; adding or removing Transmission changes the batch at once.
 */
ECS_STRUCT(Transmission, {
    float transmission_factor;  /**< 0 opaque to 1 fully transmissive */
    float ior;                  /**< Index of refraction */
    float thickness_factor;     /**< World units a ray travels through the volume, times the
                                 *   scale */
    float attenuation_distance; /**< Distance at which light has the attenuation color */
    Rgb attenuation_color;      /**< Color of the light after attenuation_distance, display
                                 *   encoded */
});

/** Equation the fragments of a material are combined with, default Opaque. A shape with
 * Transmission ignores it. */
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

/** Where the texture coordinates of a material come from */
ECS_ENUM(TextureMappingMode, {
    MappingUv = 0,      /**< The UVs of the mesh, the default */
    MappingObject = 1,  /**< Projected from the object space position times the instance scale */
    MappingWorld = 2    /**< Projected from the world position, fixed in the world */
});

/** How the texture coordinates of a material are made, default MappingUv with a size of 1 and a
 * blend of 0, inherited by instances, like TextureTransform.
 *
 * MappingObject projects each fragment onto the plane of the dominant axis of its object space
 * normal, from its object space position times the scale of the instance (the lengths of the
 * columns of its model matrix, which hold the size of a Box), divided by size. The texture has
 * the same density in world units on every face and every size of a shape, and moves with it.
 * The projection is oriented as the faces of a Box: on the side faces (±x, ±z) u runs
 * horizontally seen from outside and v down the image, toward -y; the top has u along +z and v
 * along -x, the bottom u along -z and v along -x; no face is mirrored. A unit cube with a size
 * of 1 maps as its UVs do. TextureTransform applies after the mapping.
 *
 * MappingWorld projects the world position of each fragment, divided by size, onto the plane of
 * the dominant axis of its world normal, with the same orientation. The texture is fixed in the
 * world and does not move with the object: a shape that moves or turns slides under it, and
 * shapes that touch show one continuous texture across the seam, whatever their size, rotation
 * or shape. TextureTransform applies after the projection, so its offset moves the texture over
 * the world and its scale stretches it.
 *
 * With a blend of 0 the axis is chosen per fragment, so a smooth mesh changes axis inside a
 * triangle where its normal turns, with a seam there. A blend above 0 (up to 1) mixes the three
 * axis projections (triplanar mapping) with weights from the normal raised to the power
 * 1 / blend, so curved shapes have no hard seams; the larger the blend the wider the transition.
 * Each texture layer is sampled once per projection with a weight, so a flat face aligned with
 * an axis still samples once; the normal map of each projection turns to the world with the
 * axes of that projection before the results are blended. The blend applies to MappingObject
 * and MappingWorld; MappingUv ignores it.
 *
 * Parametric surfaces (Sweep, Extrude and the other shapes of box/geometry.h that evaluate a
 * surface in the vertex stage) keep their own UVs and ignore the mapping, except a Sweep with
 * MappingObject: its UVs, by arc length, become lengths over the size, around the profile and
 * along the path on its walls and the scaled profile on its caps (see Sweep). Picking onto a
 * surface texture (PbrTextures of a Surface) follows the mapping on the dominant axis, with the
 * normal of the mesh interpolated at the hit. Alpha tested shapes (AlphaCutoff) test and cast
 * shadows with the mapped texture.
 */
ECS_STRUCT(TextureMapping, {
    TextureMappingMode mode; /**< Source of the texture coordinates */
    float size;         /**< World units one repeat of the texture covers with MappingObject
                         *   and MappingWorld, default 1; a size that is not positive and finite
                         *   is 1 */
    float blend;        /**< Width of the triplanar blend, 0..1, default 0 for the hard
                         *   projection on the dominant axis; clamped to 0..1, not finite is 0 */
});

/** Texture entities for each PBR layer, 0 for none, inherited by instances like Rgb, part of the
 * batch of a shape, not the material table, so PbrTextures brings no MaterialId. Minified color
 * layers average linear light, ORM and normal stored values, a texture shared by both as color.
 *
 * A texture entity has a TexturePath (box/texture.h) or is a Surface's texture, sampling its
 * image of the same frame; until loaded or when missing the layer has no effect. BackFacePlain
 * back faces sample no textures, except the albedo alpha of a shape with AlphaCutoff.
 *
 * The ORM layer packs glTF's occlusion and metallic roughness textures, stored, not display
 * encoded: green multiplies PbrMaterial.roughness, blue PbrMaterial.metallic, red is the
 * indirect light occlusion (1 for unoccluded). A missing layer is white, a no-op.
 */
ECS_STRUCT(PbrTextures, {
    ecs_entity_t albedo;    /**< Multiplied with the base color and opacity, display encoded;
                             *   its alpha only shows with a blend mode or AlphaCutoff. Any layer
                             *   samples a TexturePath.single_channel one as (r, 0, 0, 1), stored */
    ecs_entity_t emissive;  /**< Multiplied with the Emissive color times its strength */
    ecs_entity_t orm;       /**< Occlusion (red), roughness (green) and metallic (blue), stored */
    ecs_entity_t normal;    /**< Tangent space normal map, green points up the image */
});

/** Blend of up to four texture layers weighed by a mask (terrain, a path worn into turf), for
 * 3D shapes, inherited by instances like PbrTextures and, like it, part of the batch of a shape,
 * not the material table. Shapes with the same TextureBlend (owned by the same entity, such as
 * a prefab) share their batches.
 *
 * The mask is sampled with the texture coordinates of the shape: its mesh UV, TextureMapping
 * and TextureTransform. Its red, green, blue and alpha weigh layers 0..3, read as stored
 * (linear) whatever TexturePath.linear says; a TexturePath.single_channel mask weighs layers 0
 * and 1 by 1 - r and r. The weights are normalized by their sum; where the sum is 0, and until
 * the mask is loaded, layer 0 shows alone.
 *
 * Each layer entity samples the albedo, ORM and normal textures of its own PbrTextures (its
 * emissive is ignored), with its own TextureTransform on top of the texture coordinates of the
 * shape, so a layer with a scale of 20 tiles 20 times across a mask that covers the mesh once.
 * A layer that is 0, deleted, has no PbrTextures or whose texture is not loaded yet contributes
 * white albedo and ORM and a flat normal, except that the layers after the last one that is not
 * 0 are unused: their mask channels are ignored, so an RGB mask weighs a 3 layer blend. A
 * TextureGen texture works as a mask or a layer.
 *
 * While a shape draws with a TextureBlend the albedo, ORM and normal of its own PbrTextures are
 * ignored; its PbrTextures.emissive, Rgb, PbrMaterial, Tint and Emissive apply to the blended
 * result. Changing the mask, a layer, the PbrTextures or TextureTransform of a layer, or a
 * reload of one of their images updates the shapes.
 *
 * The alpha of a blended shape is the weighted alpha of the albedos of its layers: AlphaCutoff
 * tests it in the forward pass, the depth prepass and the shadow pass, so a blended fence casts
 * the shadow of its holes, and a BlendMode, an Opacity under 1 and Transmission draw with it. A
 * MappingObject or MappingWorld TextureMapping with a blend above 0 (triplanar) samples the mask
 * and each layer with a weight once per projection with a weight: a face along an axis whose
 * mask weighs one layer samples each map once, a corner where three projections and four layers
 * meet twelve times. BackFacePlain back faces sample no layer, but the albedo of an alpha tested
 * shape. 2D shapes ignore a TextureBlend.
 *
 * Picking onto the texture of a blended shape (a mask, a layer albedo or the PbrTextures.emissive
 * that a Surface renders) reports the texture coordinates of the shape, those the mask is
 * sampled with, not those of a layer with its TextureTransform; it does not tell which layer
 * weighs most at the hit, as the mask is not read back from the GPU.
 */
ECS_STRUCT(TextureBlend, {
    ecs_entity_t mask;      /**< Texture entity whose red, green, blue and alpha weigh layers
                             *   0..3, 0 for layer 0 alone */
    ecs_entity_t layers[4]; /**< Entities with PbrTextures and an optional TextureTransform, 0
                             *   for an unused layer */
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
