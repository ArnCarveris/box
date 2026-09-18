/**
 * @file material/material.c
 * @brief Registers the box.material module and its defaults.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>
#include <math.h>
#include "material_internal.h"

ECS_TAG_DECLARE(DynamicMaterial);

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
    ECS_TAG_DEFINE(world, DynamicMaterial);

    ecs_set_hooks(world, Opacity, { .ctor = box_opacity_ctor });
    ecs_set_hooks(world, PbrMaterial, { .ctor = box_pbr_ctor });
    ecs_set_hooks(world, PbrTextures, { .ctor = flecs_default_ctor });

    ecs_add_pair(world, ecs_id(Rgb), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Tint), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrMaterial), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Emissive), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, DynamicMaterial, EcsOnInstantiate, EcsInherit);

    box_tint_init(world);
}
