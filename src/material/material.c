/**
 * @file material/material.c
 * @brief Registers the components of the box.material module.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>

ECS_TAG_DECLARE(DynamicMaterial);
ECS_TAG_DECLARE(NoShadow);

/** Construct Opacity values that are fully opaque.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_opacity_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Opacity *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = 1.0f;
    }
}

/** Construct MaterialId values that were never handed a slot.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_material_id_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MaterialId *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = UINT32_MAX;
    }
}

/** Construct AlphaCutoff values that discard below BOX_MATERIAL_CUTOFF.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_alpha_cutoff_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AlphaCutoff *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = BOX_MATERIAL_CUTOFF;
    }
}

/** Construct PbrMaterial values: the material of a shape without PbrMaterial, a matte dielectric.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pbr_material_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    PbrMaterial *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (PbrMaterial){
            .metallic = BOX_MATERIAL_METALLIC, .roughness = BOX_MATERIAL_ROUGHNESS };
    }
}

/** Construct ClearCoat values: a full, smooth coat.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_clear_coat_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ClearCoat *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ClearCoat){
            .strength = BOX_MATERIAL_CLEAR_COAT_STRENGTH,
            .roughness = BOX_MATERIAL_CLEAR_COAT_ROUGHNESS };
    }
}

/** Construct SpecularAnisotropy values: a fully stretched highlight along +U.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_specular_anisotropy_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SpecularAnisotropy *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SpecularAnisotropy){
            .strength = BOX_MATERIAL_ANISOTROPY_STRENGTH,
            .rotation = BOX_MATERIAL_ANISOTROPY_ROTATION };
    }
}

/** Construct Sheen values: a white sheen of medium roughness.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_sheen_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Sheen *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Sheen){
            .color = { 255, 255, 255 },
            .roughness = BOX_MATERIAL_SHEEN_ROUGHNESS };
    }
}

/** Construct Transmission values of a surface without volume: no transmission, the index of
 * refraction of glass and no absorption.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transmission_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Transmission *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Transmission){
            .ior = BOX_MATERIAL_IOR,
            .attenuation_distance = BOX_MATERIAL_ATTENUATION_DISTANCE,
            .attenuation_color = { 255, 255, 255 }
        };
    }
}

/** Construct TextureTransform values that leave the texture coordinates unchanged.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_transform_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureTransform *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TextureTransform){ .scale_x = 1.0f, .scale_y = 1.0f };
    }
}

void BoxMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMaterial);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, MaterialId);
    ECS_META_COMPONENT(world, Rgb);
    ECS_META_COMPONENT(world, Opacity);
    ECS_META_COMPONENT(world, AlphaCutoff);
    ECS_META_COMPONENT(world, Tint);
    ECS_META_COMPONENT(world, PbrMaterial);
    ECS_META_COMPONENT(world, ClearCoat);
    ECS_META_COMPONENT(world, SpecularAnisotropy);
    ECS_META_COMPONENT(world, Sheen);
    ECS_META_COMPONENT(world, Emissive);
    ECS_META_COMPONENT(world, Transmission);
    ECS_META_COMPONENT(world, BlendMode);
    ECS_META_COMPONENT(world, BackFaceMode);
    ECS_META_COMPONENT(world, TextureWrap);
    ECS_META_COMPONENT(world, TextureTransform);
    ECS_META_COMPONENT(world, PbrTextures);
    ecs_set_hooks(world, Opacity, { .ctor = box_opacity_ctor });
    ecs_set_hooks(world, AlphaCutoff, { .ctor = box_alpha_cutoff_ctor });
    ecs_set_hooks(world, PbrMaterial, { .ctor = box_pbr_material_ctor });
    ecs_set_hooks(world, ClearCoat, { .ctor = box_clear_coat_ctor });
    ecs_set_hooks(world, SpecularAnisotropy, { .ctor = box_specular_anisotropy_ctor });
    ecs_set_hooks(world, Sheen, { .ctor = box_sheen_ctor });
    ecs_set_hooks(world, TextureTransform, { .ctor = box_texture_transform_ctor });
    ecs_set_hooks(world, Transmission, { .ctor = box_transmission_ctor });
    ecs_set_hooks(world, MaterialId, { .ctor = box_material_id_ctor });
    ecs_add_pair(world, ecs_id(MaterialId), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);

    ecs_entity_t materials[9] = {
        ecs_id(Rgb), ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(TextureTransform),
        ecs_id(AlphaCutoff), ecs_id(Transmission), ecs_id(ClearCoat), ecs_id(SpecularAnisotropy),
        ecs_id(Sheen)
    };
    int32_t i;
    for (i = 0; i < 9; i ++) {
        ecs_add_pair(world, materials[i], EcsOnInstantiate, EcsInherit);
        ecs_add_pair(world, materials[i], EcsWith, ecs_id(MaterialId));
    }

    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
}
