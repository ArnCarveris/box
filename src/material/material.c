/**
 * @file material/material.c
 * @brief Registers the box.material module and its defaults.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>
#include <math.h>
#include "material_internal.h"

ECS_TAG_DECLARE(DynamicMaterial);
ECS_TAG_DECLARE(NoShadow);
ECS_TAG_DECLARE(MaterialLayerOn);

/** Table of linear values for every display encoded byte */
static float box_srgb_table[256];

/** Opacity constructor setting the value to one.
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
    (void)ti;
    Opacity *o = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        o[i].value = 1.0f;
    }
}

/** PbrMaterial constructor setting roughness to a half.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pbr_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    PbrMaterial *m = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        m[i].metallic = 0.0f;
        m[i].roughness = 0.5f;
    }
}

static void box_material_layer_data_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    MaterialLayerData *data = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        data[i].amount0 = 1;
        data[i].amount1 = 1;
        data[i].amount2 = 1;
        data[i].amount3 = 1;
        data[i].amount4 = 1;
        data[i].amount5 = 1;
        data[i].seed = 0.0f;
        data[i].scale = 1.0f;
    }
}

/** TextureTransform constructor setting the identity transform.
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
    (void)ti;
    TextureTransform *t = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        t[i].scale_x = 1.0f;
        t[i].scale_y = 1.0f;
        t[i].offset_x = 0.0f;
        t[i].offset_y = 0.0f;
    }
}

/** TextureTiling constructor setting one meter tiles of the box projection.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_tiling_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TextureTiling *t = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        t[i].meters_x = 1.0f;
        t[i].meters_y = 1.0f;
        t[i].fit = false;
        t[i].allow_partial = false;
        t[i].min_tiles = 0;
        t[i].shape = TilingBox;
    }
}

float box_srgb_to_linear(
    uint8_t value)
{
    return box_srgb_table[value];
}

void BoxMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMaterial);
    ECS_IMPORT(world, BoxTypes);

    int32_t i;
    for (i = 0; i < 256; i ++) {
        float c = (float)i / 255.0f;
        box_srgb_table[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }

    ECS_META_COMPONENT(world, Rgb);
    ECS_META_COMPONENT(world, Opacity);
    ECS_META_COMPONENT(world, Tint);
    ECS_META_COMPONENT(world, ActualTint);
    ECS_META_COMPONENT(world, PbrMaterial);
    ECS_META_COMPONENT(world, Emissive);
    ECS_META_COMPONENT(world, PbrTextures);
    ECS_META_COMPONENT(world, BlendMode);
    ECS_META_COMPONENT(world, BackFaceMode);
    ECS_META_COMPONENT(world, TilingShape);
    ECS_META_COMPONENT(world, TextureWrap);
    ECS_META_COMPONENT(world, TextureTransform);
    ECS_META_COMPONENT(world, TextureTiling);
    ECS_META_COMPONENT(world, MaterialLayer);
    ECS_META_COMPONENT(world, LayerParams);
    ECS_META_COMPONENT(world, MaterialLayerData);
    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
    ECS_TAG_DEFINE(world, MaterialLayerOn);

    ecs_set_hooks(world, Opacity, { .ctor = box_opacity_ctor });
    ecs_set_hooks(world, PbrMaterial, { .ctor = box_pbr_ctor });
    ecs_set_hooks(world, PbrTextures, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, TextureTransform, { .ctor = box_texture_transform_ctor });
    ecs_set_hooks(world, TextureTiling, { .ctor = box_texture_tiling_ctor });
    ecs_set_hooks(world, MaterialLayer, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, LayerParams, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, MaterialLayerData, { .ctor = box_material_layer_data_ctor });

    ecs_add_pair(world, ecs_id(TextureTransform), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TextureTiling), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TextureWrap), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Rgb), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Tint), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrMaterial), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Emissive), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(BlendMode), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(BackFaceMode), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(MaterialLayer), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LayerParams), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, DynamicMaterial, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, NoShadow, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, MaterialLayerOn, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(MaterialLayerData), EcsOnInstantiate, EcsInherit);

    box_tint_init(world);
}
