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

    ECS_META_COMPONENT(world, Rgb);
    ECS_META_COMPONENT(world, Opacity);
    ECS_META_COMPONENT(world, AlphaCutoff);
    ECS_META_COMPONENT(world, Tint);
    ECS_META_COMPONENT(world, PbrMaterial);
    ECS_META_COMPONENT(world, Emissive);
    ECS_META_COMPONENT(world, BlendMode);
    ECS_META_COMPONENT(world, BackFaceMode);
    ECS_META_COMPONENT(world, TextureWrap);
    ECS_META_COMPONENT(world, TextureTransform);
    ECS_META_COMPONENT(world, PbrTextures);
    ecs_set_hooks(world, Opacity, { .ctor = box_opacity_ctor });
    ecs_set_hooks(world, AlphaCutoff, { .ctor = box_alpha_cutoff_ctor });
    ecs_set_hooks(world, PbrMaterial, { .ctor = box_pbr_material_ctor });
    ecs_set_hooks(world, TextureTransform, { .ctor = box_texture_transform_ctor });
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);

    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
}
